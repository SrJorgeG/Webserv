#include "core/Connection.hpp"
#include "core/Reactor.hpp"
#include "http/GetHandler.hpp"
#include "http/PostHandler.hpp"
#include "http/DeleteHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"
#include "utils/FileUtils.hpp"
#include "cgi/CgiHandler.hpp"

Connection::Connection(int clientFd, const ServerConfig& serverConfig, Reactor& reactor)
    : _clientFd(clientFd), _state(READING_HEADERS), _serverConfig(serverConfig),
      _reactor(reactor), _bytesWritten(0), _lastActivity(time(NULL)), _keepAlive(true),
      _isKeepAliveIdle(false), _cgiHandler(NULL), _cgiInputFd(-1), _cgiOutputFd(-1) {}

Connection::~Connection() {
    _unregisterCgiPipes();
    if (_cgiHandler) {
        delete _cgiHandler;
        _cgiHandler = NULL;
    }
    if (_clientFd >= 0) {
        close(_clientFd);
    }
}

void Connection::handleRead() {
    _isKeepAliveIdle = false;
    updateLastActivity();
    _processRead();
}

void Connection::handleWrite() {
    _isKeepAliveIdle = false;
    updateLastActivity();
    _processWrite();
}

void Connection::handleError() {
    LOG_WARN("Connection error on fd " + StringUtils::intToString(_clientFd));
    _state = CLOSING;
}

int Connection::getFd() const {
    return _clientFd;
}

void Connection::closeConnection() {
    _unregisterCgiPipes();
    if (_clientFd >= 0) {
        _reactor.removeHandler(_clientFd);
        close(_clientFd);
        _clientFd = -1;
    }
    _state = CLOSING;
}

bool Connection::isTimedOut() const {
    time_t timeout = _isKeepAliveIdle ? KEEP_ALIVE_TIMEOUT : CONNECTION_TIMEOUT;
    return (time(NULL) - _lastActivity) > timeout;
}

void Connection::updateLastActivity() {
    _lastActivity = time(NULL);
}

void Connection::_processRead() {
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead = recv(_clientFd, buffer, BUFFER_SIZE - 1, 0);

    if (bytesRead < 0) {
        handleError();
        return;
    }

    if (bytesRead == 0) {
        LOG_INFO("Client disconnected");
        _state = CLOSING;
        return;
    }

    buffer[bytesRead] = '\0';
    _readBuffer.append(buffer, bytesRead);

    if (_state == READING_HEADERS || _state == READING_BODY) {
        ParseResult result = _parser.parse(_readBuffer, _request);
        _readBuffer.clear();

        if (result == PARSE_ERROR) {
            _buildErrorResponse(400);
            return;
        }
        if (result == PARSE_OK) {
            // Check Content-Length against client_max_body_size
            if (_request.getMethod() == "POST" || _request.getMethod() == "PUT") {
                std::string contentLengthStr = _request.getHeader("Content-Length");
                if (!contentLengthStr.empty()) {
                    size_t contentLength = static_cast<size_t>(std::atoi(contentLengthStr.c_str()));
                    if (contentLength > _serverConfig.getClientMaxBodySize()) {
                        _buildErrorResponse(413);
                        return;
                    }
                }
                // Also check actual body size for chunked or accumulated body
                if (_request.getBody().size() > _serverConfig.getClientMaxBodySize()) {
                    _buildErrorResponse(413);
                    return;
                }
            }
            _state = PROCESSING;
            _processRequest();
        } else if (result == PARSE_INCOMPLETE) {
            if (_state == READING_HEADERS && _parser.headersComplete()) {
                _state = READING_BODY;
            }
        }
    }
}

void Connection::_processWrite() {
    if (_writeBuffer.empty() || _state != WRITING_RESPONSE) {
        return;
    }

    ssize_t bytesSent = send(_clientFd, _writeBuffer.c_str() + _bytesWritten,
                             _writeBuffer.size() - _bytesWritten, 0);

    if (bytesSent < 0) {
        handleError();
        // Ensure EPOLLOUT remains enabled so we get notified when socket is writable again
        _reactor.modifyHandler(_clientFd, EPOLLIN | EPOLLOUT);
        return;
    }

    _bytesWritten += bytesSent;

    if (_bytesWritten >= _writeBuffer.size()) {
        LOG_INFO("Response sent completely");
        if (_keepAlive) {
            _request.clear();
            _response.clear();
            _readBuffer.clear();
            _writeBuffer.clear();
            _bytesWritten = 0;
            _parser.reset();
            _state = READING_HEADERS;
            _isKeepAliveIdle = true;
            updateLastActivity();
            _reactor.modifyHandler(_clientFd, EPOLLIN);
        } else {
            _state = CLOSING;
        }
    }
}

void Connection::_processRequest() {
    // Parse cookies and manage session
    _request.parseCookies();
    std::string sessionId = _request.getCookie("webserv_session_id");
    if (sessionId.empty() || !SessionManager::getInstance().hasSession(sessionId)) {
        sessionId = SessionManager::getInstance().createSession();
    }
    _sessionId = sessionId;

    const RouteConfig* route = _findMatchingRoute(_request.getUri());

    if (!route) {
        _buildErrorResponse(404);
        return;
    }

    if (!route->getRedirect().empty()) {
        _response.setStatus(301);
        _response.setHeader("Location", route->getRedirect());
        _response.setHeader("Content-Length", "0");
        _response.setCookie("webserv_session_id", _sessionId);
        _response.setReady(true);
        _writeBuffer = _response.toString();
        _state = WRITING_RESPONSE;
        _reactor.modifyHandler(_clientFd, EPOLLIN | EPOLLOUT);
        return;
    }

    std::string path = _resolvePath(_request, *route);
    std::string ext = StringUtils::getExtension(path);

    if (route->hasCgiHandler(ext)) {
        // Start async CGI processing
        _cgiHandler = new CgiHandler();
        _cgiHandler->start(_request, *route, _serverConfig, _response);

        if (_response.isReady()) {
            // Error occurred during start
            _writeBuffer = _response.toString();
            _state = WRITING_RESPONSE;
            _reactor.modifyHandler(_clientFd, EPOLLIN | EPOLLOUT);
            delete _cgiHandler;
            _cgiHandler = NULL;
            return;
        }

        // Get pipe fds and register with epoll
        _cgiInputFd = _cgiHandler->getInputFd();
        _cgiOutputFd = _cgiHandler->getOutputFd();

        if (_request.getMethod() == "POST" && !_request.getBody().empty()) {
            _state = CGI_WRITING_TO_STDIN;
            if (_cgiInputFd >= 0) {
                _reactor.registerHandler(_cgiInputFd, this, EPOLLOUT);
            }
            if (_cgiOutputFd >= 0) {
                _reactor.registerHandler(_cgiOutputFd, this, EPOLLIN);
            }
        } else {
            // No body to write, just read
            _state = CGI_READING_FROM_STDOUT;
            if (_cgiInputFd >= 0) {
                close(_cgiInputFd);
                _cgiInputFd = -1;
            }
            if (_cgiOutputFd >= 0) {
                _reactor.registerHandler(_cgiOutputFd, this, EPOLLIN);
            }
        }
        return;
    }

    if (_request.getMethod() == "GET") {
        GetHandler handler;
        handler.handle(_request, _response, *route, _serverConfig);
    } else if (_request.getMethod() == "POST") {
        PostHandler handler;
        handler.handle(_request, _response, *route, _serverConfig);
    } else if (_request.getMethod() == "DELETE") {
        DeleteHandler handler;
        handler.handle(_request, _response, *route, _serverConfig);
    } else {
        _buildErrorResponse(405);
        return;
    }

    _response.setCookie("webserv_session_id", _sessionId);
    _writeBuffer = _response.toString();
    _state = WRITING_RESPONSE;
    _reactor.modifyHandler(_clientFd, EPOLLIN | EPOLLOUT);
}

void Connection::_processCgiWrite() {
    if (!_cgiHandler || _state != CGI_WRITING_TO_STDIN) {
        return;
    }

    size_t bytesWritten = 0;
    std::string body = _request.getBody();
    _cgiHandler->writeBodyChunk(body, bytesWritten);

    // Check if all body data has been written
    if (bytesWritten == 0 || _cgiHandler->getState() == CGI_READING) {
        // Done writing body, switch to reading
        if (_cgiInputFd >= 0) {
            _reactor.removeHandler(_cgiInputFd);
            close(_cgiInputFd);
            _cgiInputFd = -1;
        }
        _cgiHandler->finishBodyWrite();
        _state = CGI_READING_FROM_STDOUT;
    }
}

void Connection::_processCgiRead() {
    if (!_cgiHandler || (_state != CGI_READING_FROM_STDOUT && _state != CGI_WRITING_TO_STDIN)) {
        return;
    }

    char buffer[BUFFER_SIZE];
    ssize_t n = _cgiHandler->readOutputChunk(buffer, sizeof(buffer));

    if (n < 0) {
        // EAGAIN or would block - just return, epoll will notify us again
        return;
    }

    if (n == 0) {
        // EOF - CGI output complete
        if (_cgiOutputFd >= 0) {
            _reactor.removeHandler(_cgiOutputFd);
            close(_cgiOutputFd);
            _cgiOutputFd = -1;
        }
        if (_cgiInputFd >= 0) {
            _reactor.removeHandler(_cgiInputFd);
            close(_cgiInputFd);
            _cgiInputFd = -1;
        }

        _cgiHandler->finishOutputRead(_response);

        // Build response and switch to writing
        _response.setCookie("webserv_session_id", _sessionId);
        _writeBuffer = _response.toString();
        _bytesWritten = 0;
        _state = WRITING_RESPONSE;
        _reactor.modifyHandler(_clientFd, EPOLLIN | EPOLLOUT);

        // Cleanup CGI handler
        delete _cgiHandler;
        _cgiHandler = NULL;
    }
}

void Connection::_registerCgiPipes() {
    // CGI pipes are registered when CGI processing starts
    // This method is here for potential future use
}

void Connection::_unregisterCgiPipes() {
    if (_cgiInputFd >= 0) {
        _reactor.removeHandler(_cgiInputFd);
        close(_cgiInputFd);
        _cgiInputFd = -1;
    }
    if (_cgiOutputFd >= 0) {
        _reactor.removeHandler(_cgiOutputFd);
        close(_cgiOutputFd);
        _cgiOutputFd = -1;
    }
}

void Connection::handleCgiInputWrite() {
    _processCgiWrite();
}

void Connection::handleCgiOutputRead() {
    _processCgiRead();
}

void Connection::_buildErrorResponse(int statusCode) {
    _response.clear();
    std::string root;
    const std::vector<RouteConfig>& routes = _serverConfig.getRoutes();
    if (!routes.empty()) {
        root = routes[0].getRoot();
    }
    _response.buildError(statusCode, _serverConfig.getErrorPages(), root);
    _response.setCookie("webserv_session_id", _sessionId);
    _writeBuffer = _response.toString();
    _state = WRITING_RESPONSE;
    _reactor.modifyHandler(_clientFd, EPOLLIN | EPOLLOUT);
}

const RouteConfig* Connection::_findMatchingRoute(const std::string& uri) const {
    return _serverConfig.findRoute(uri);
}

std::string Connection::_resolvePath(const Request& request, const RouteConfig& route) {
    std::string uri = request.getUri();
    std::string routePath = route.getPath();

    if (routePath != "/" && StringUtils::startsWith(uri, routePath)) {
        uri = uri.substr(routePath.size());
    }
    if (uri.empty() || uri[0] != '/') {
        uri = "/" + uri;
    }

    return FileUtils::joinPath(route.getRoot(), uri);
}