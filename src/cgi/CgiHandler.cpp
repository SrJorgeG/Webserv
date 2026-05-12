#include "cgi/CgiHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"
#include "utils/FileUtils.hpp"
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>

CgiHandler::CgiHandler()
    : _pid(-1), _startTime(0), _state(CGI_IDLE), _bodyBytesWritten(0) {
    _inputPipe[0] = -1;
    _inputPipe[1] = -1;
    _outputPipe[0] = -1;
    _outputPipe[1] = -1;
}

CgiHandler::~CgiHandler() {
    cleanup();
}

void CgiHandler::cleanup() {
    if (_pid > 0) {
        kill(_pid, SIGKILL);
        waitpid(_pid, NULL, 0);
        _pid = -1;
    }
    _closePipes();
    _state = CGI_IDLE;
}

void CgiHandler::start(const Request& request, const RouteConfig& route,
                       const ServerConfig& server, Response& response) {
    try {
        _setupPipes();
        _setupEnvironment(request, route, server);

        // Resolve script path
        std::string fullPath = StringUtils::resolvePath(request.getUri(), route.getPath(), route.getRoot());

        // Verify script exists
        if (!FileUtils::fileExists(fullPath)) {
            response.buildError(404, server.getErrorPages(), route.getRoot());
            return;
        }

        // Get interpreter
        std::string ext = StringUtils::getExtension(fullPath);
        std::string interpreter = route.getCgiInterpreter(ext);
        if (interpreter.empty()) {
            response.buildError(500, server.getErrorPages(), route.getRoot());
            return;
        }

        _forkAndExecute(interpreter, fullPath);

        if (_pid < 0) {
            response.buildError(500, server.getErrorPages(), route.getRoot());
            return;
        }

        _startTime = time(NULL);
        _state = CGI_RUNNING;
        _outputBuffer.clear();

        // Store body data for async writing
        if (request.getMethod() == "POST") {
            _bodyToWrite = request.getBody();
            _bodyBytesWritten = 0;
            _state = CGI_WRITING;
        } else {
            _bodyToWrite.clear();
            _bodyBytesWritten = 0;
            // Close stdin write end if no body to write
            if (_inputPipe[1] >= 0) {
                close(_inputPipe[1]);
                _inputPipe[1] = -1;
            }
            _state = CGI_READING;
        }

} catch (const std::exception& e) {
        LOG_ERROR(std::string("CGI execution failed: ") + e.what());
        response.buildError(500, server.getErrorPages(), route.getRoot());
        _closePipes();
        _state = CGI_IDLE;
        _pid = -1;
    }
}

int CgiHandler::getInputFd() const {
    return _inputPipe[1];  // Return write end for parent
}

int CgiHandler::getOutputFd() const {
    return _outputPipe[0];  // Return read end for parent
}

CgiState CgiHandler::getState() const {
    return _state;
}

pid_t CgiHandler::getPid() const {
    return _pid;
}

time_t CgiHandler::getStartTime() const {
    return _startTime;
}

bool CgiHandler::isRunning() const {
    return _state == CGI_WRITING || _state == CGI_READING;
}

void CgiHandler::writeBodyChunk(const std::string& body, size_t& bytesWritten) {
    if (_inputPipe[1] < 0 || body.empty()) {
        bytesWritten = 0;
        return;
    }

    ssize_t n = write(_inputPipe[1], body.c_str() + _bodyBytesWritten,
                      body.size() - _bodyBytesWritten);
    if (n > 0) {
        _bodyBytesWritten += n;
        bytesWritten = n;
    } else {
        bytesWritten = 0;
    }
}

ssize_t CgiHandler::readOutputChunk(char* buffer, size_t size) {
    if (_outputPipe[0] < 0) {
        return -1;
    }

    ssize_t n = read(_outputPipe[0], buffer, size);
    if (n > 0) {
        _outputBuffer.append(buffer, n);
        if (_outputBuffer.size() > CGI_MAX_OUTPUT_SIZE) {
            LOG_WARN("CGI output exceeded maximum size limit");
        }
    }
    return n;
}

void CgiHandler::finishBodyWrite() {
    if (_inputPipe[1] >= 0) {
        close(_inputPipe[1]);
        _inputPipe[1] = -1;
    }
    _state = CGI_READING;
}

void CgiHandler::finishOutputRead(Response& response) {
    // Reap the child process without blocking the event loop
    bool childFailed = false;
    if (_pid > 0) {
        int status;
        pid_t result = waitpid(_pid, &status, WNOHANG);
        if (result == 0) {
            // Child still running after EOF on pipe — kill it immediately.
            // A well-behaved CGI should have exited when its stdout closed.
            // Busy-waiting would violate the non-blocking requirement.
            kill(_pid, SIGKILL);
            waitpid(_pid, &status, 0);
        }
        // Check if the child exited with a non-zero status (e.g., execve failure)
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            childFailed = true;
        } else if (WIFSIGNALED(status)) {
            childFailed = true;
        }
        _pid = -1;
    }

    // If the child process failed and the output doesn't look like valid
    // CGI output (no Content-Type or Status header), return a 500 error
    if (childFailed && _outputBuffer.find("Content-Type") == std::string::npos
                     && _outputBuffer.find("Status") == std::string::npos) {
        response.buildError(500, std::map<int, std::string>(), "");
        _state = CGI_DONE;
        return;
    }

    _parseCgiOutput(response);
    _state = CGI_DONE;
}

void CgiHandler::checkTimeout() {
    if (!isRunning() || _startTime == 0) return;

    if (time(NULL) - _startTime > CGI_TIMEOUT) {
        LOG_WARN("CGI timeout, killing process");
        cleanup();
    }
}

void CgiHandler::_setupPipes() {
    if (pipe(_inputPipe) < 0 || pipe(_outputPipe) < 0) {
        throw std::runtime_error("pipe() failed");
    }
}

void CgiHandler::_closePipes() {
    if (_inputPipe[0] >= 0) { close(_inputPipe[0]); _inputPipe[0] = -1; }
    if (_inputPipe[1] >= 0) { close(_inputPipe[1]); _inputPipe[1] = -1; }
    if (_outputPipe[0] >= 0) { close(_outputPipe[0]); _outputPipe[0] = -1; }
    if (_outputPipe[1] >= 0) { close(_outputPipe[1]); _outputPipe[1] = -1; }
}

void CgiHandler::_setupEnvironment(const Request& request, const RouteConfig& route,
                                   const ServerConfig& server) {
    _envVars.clear();

    // CGI standard variables (RFC 3875)
    _envVars.push_back("GATEWAY_INTERFACE=CGI/1.1");
    _envVars.push_back("SERVER_PROTOCOL=HTTP/1.1");
    _envVars.push_back("SERVER_SOFTWARE=webserv/1.0");
    _envVars.push_back("REQUEST_METHOD=" + request.getMethod());

    // Extract QUERY_STRING from URI before setting SCRIPT_NAME
    std::string uri = request.getUri();
    std::string queryString;
    size_t qpos = uri.find('?');
    if (qpos != std::string::npos) {
        queryString = uri.substr(qpos + 1);
        uri = uri.substr(0, qpos);
    }

    // SCRIPT_NAME should NOT include the query string (RFC 3875)
    _envVars.push_back("SCRIPT_NAME=" + uri);

    // PATH_INFO: path after the script name (not yet extracted per-route)
    // For now, empty as we don't have detailed route-based PATH_INFO parsing
    std::string pathInfo;
    _envVars.push_back("PATH_INFO=" + pathInfo);
    _envVars.push_back("PATH_TRANSLATED=" + (pathInfo.empty() ? std::string("") : route.getRoot() + pathInfo));

    _envVars.push_back("QUERY_STRING=" + queryString);

    // Content-Length and Content-Type
    std::string contentLength = request.getHeader("Content-Length");
    if (contentLength.empty()) {
        contentLength = StringUtils::sizeToString(request.getBody().size());
    }
    _envVars.push_back("CONTENT_LENGTH=" + contentLength);

    std::string contentType = request.getHeader("Content-Type");
    if (contentType.empty()) {
        contentType = "application/x-www-form-urlencoded";
    }
    _envVars.push_back("CONTENT_TYPE=" + contentType);

    // Server info
    const std::vector<std::pair<std::string, int> >& listens = server.getListens();
    if (!listens.empty()) {
        _envVars.push_back("SERVER_NAME=" + listens[0].first);
        _envVars.push_back("SERVER_PORT=" + StringUtils::intToString(listens[0].second));
        // SERVER_ADDR: the address the server is listening on
        _envVars.push_back("SERVER_ADDR=" + listens[0].first);
    } else {
        _envVars.push_back("SERVER_NAME=localhost");
        _envVars.push_back("SERVER_PORT=8080");
        _envVars.push_back("SERVER_ADDR=127.0.0.1");
    }

    // Client info (actual client IP would require getpeername, fallback for now)
    _envVars.push_back("REMOTE_ADDR=127.0.0.1");
    _envVars.push_back("REMOTE_HOST=127.0.0.1");

    // Auth info (not implemented, required by RFC 3875)
    _envVars.push_back("AUTH_TYPE=");
    _envVars.push_back("REMOTE_USER=");

    // HTTP headers as environment variables
    const std::map<std::string, std::string>& headers = request.getHeaders();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin();
         it != headers.end(); ++it) {
        std::string key = it->first;
        std::string envKey = "HTTP_";
        for (size_t i = 0; i < key.size(); ++i) {
            if (key[i] == '-') {
                envKey += '_';
            } else {
                envKey += static_cast<char>(std::toupper(static_cast<unsigned char>(key[i])));
            }
        }
        _envVars.push_back(envKey + "=" + it->second);
    }

    // REDIRECT_STATUS required by PHP-CGI
    _envVars.push_back("REDIRECT_STATUS=200");

    // Build envp (char* array)
    _envp.clear();
    for (size_t i = 0; i < _envVars.size(); ++i) {
        _envp.push_back(const_cast<char*>(_envVars[i].c_str()));
    }
    _envp.push_back(NULL);
}

void CgiHandler::_forkAndExecute(const std::string& interpreter, const std::string& scriptPath) {
    _pid = fork();
    if (_pid < 0) {
        LOG_ERROR("fork() failed");
        return;
    }

    if (_pid == 0) {
        // Child process
        close(_inputPipe[1]);
        close(_outputPipe[0]);

        dup2(_inputPipe[0], STDIN_FILENO);
        dup2(_outputPipe[1], STDOUT_FILENO);
        dup2(_outputPipe[1], STDERR_FILENO);

        close(_inputPipe[0]);
        close(_outputPipe[1]);

        // chdir to script directory for relative paths
        std::string scriptDir = scriptPath.substr(0, scriptPath.rfind('/'));
        if (!scriptDir.empty()) {
            if (chdir(scriptDir.c_str()) != 0) {
                LOG_WARN("chdir to script directory failed: " + scriptDir);
            }
        }

        // Prepare argv
        std::string scriptName = scriptPath.substr(scriptPath.rfind('/') + 1);
        char* argv[3];
        argv[0] = const_cast<char*>(interpreter.c_str());
        argv[1] = const_cast<char*>(scriptName.c_str());
        argv[2] = NULL;

        execve(interpreter.c_str(), argv, &_envp[0]);

        // If execve fails
        perror("execve");
        exit(1);
    }

    // Parent process
    close(_inputPipe[0]);
    close(_outputPipe[1]);
    _inputPipe[0] = -1;
    _outputPipe[1] = -1;

    // Set FD_CLOEXEC on parent's pipe ends so they don't leak to future CGI children.
    // Also prevents accidental inheritance across execve.
    if (_inputPipe[1] >= 0) {
        fcntl(_inputPipe[1], F_SETFD, FD_CLOEXEC);
    }
    if (_outputPipe[0] >= 0) {
        fcntl(_outputPipe[0], F_SETFD, FD_CLOEXEC);
    }

    // Set non-blocking mode for pipes
    // Fresh pipes have no flags set, so we only need F_SETFL + O_NONBLOCK.
    // F_GETFL is not permitted on macOS per the subject.
    if (_inputPipe[1] >= 0) {
        fcntl(_inputPipe[1], F_SETFL, O_NONBLOCK);
    }
    if (_outputPipe[0] >= 0) {
        fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK);
    }
}

void CgiHandler::_parseCgiOutput(Response& response) {
    response.clear();

    if (_outputBuffer.empty()) {
        response.setStatus(200);
        response.setHeader("Content-Type", "text/html");
        response.setBody("");
        response.setReady(true);
        return;
    }

    // Separate headers from body
    size_t headerEnd = _outputBuffer.find("\r\n\r\n");
    size_t headerLen = 4;
    if (headerEnd == std::string::npos) {
        headerEnd = _outputBuffer.find("\n\n");
        headerLen = 2;
    }

    if (headerEnd == std::string::npos) {
        // No headers separated, everything is body
        response.setStatus(200);
        response.setHeader("Content-Type", "text/html");
        response.setBody(_outputBuffer);
        response.setReady(true);
        return;
    }

    std::string headers = _outputBuffer.substr(0, headerEnd);
    std::string body = _outputBuffer.substr(headerEnd + headerLen);

    // Parse headers
    std::vector<std::string> lines = StringUtils::split(headers, '\n');
    int statusCode = 200;
    bool hasContentType = false;
    bool hasLocation = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line = StringUtils::trim(lines[i]);
        if (line.empty()) continue;

        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = StringUtils::trim(line.substr(0, colonPos));
            std::string value = StringUtils::trim(line.substr(colonPos + 1));

            if (StringUtils::toLower(key) == "status") {
                statusCode = std::atoi(value.c_str());
            } else {
                if (StringUtils::toLower(key) == "content-type") {
                    hasContentType = true;
                }
                if (StringUtils::toLower(key) == "location") {
                    hasLocation = true;
                }
                response.setHeader(key, value);
            }
        }
    }

    // Handle CGI redirect (RFC 3875 section 6.2.4)
    // If a Location header is present and status is not a redirect (3xx),
    // change status to 302 Found for client redirect
    if (hasLocation && body.empty() && statusCode >= 200 && statusCode < 300) {
        statusCode = 302;
    }

    response.setStatus(statusCode);

    if (!hasContentType) {
        response.setHeader("Content-Type", "text/html");
    }

    response.setBody(body);
    response.setReady(true);
}