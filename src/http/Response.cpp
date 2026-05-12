#include "http/Response.hpp"
#include "http/StatusCodes.hpp"
#include "utils/StringUtils.hpp"
#include "utils/FileUtils.hpp"
#include <cstdio>

Response::Response()
    : _statusCode(200), _isReady(false) {}

Response::~Response() {}

Response::Response(const Response& other) {
    *this = other;
}

Response& Response::operator=(const Response& other) {
    if (this != &other) {
        _statusCode = other._statusCode;
        _statusMessage = other._statusMessage;
        _headers = other._headers;
        _setCookies = other._setCookies;
        _body = other._body;
        _isReady = other._isReady;
    }
    return *this;
}

void Response::clear() {
    _statusCode = 200;
    _statusMessage.clear();
    _headers.clear();
    _setCookies.clear();
    _body.clear();
    _isReady = false;
}

void Response::setStatus(int code) {
    _statusCode = code;
    _statusMessage = StatusCodes::getMessage(code);
}

void Response::setHeader(const std::string& key, const std::string& value) {
    _headers[key] = value;
}

void Response::setBody(const std::string& body) {
    _body = body;
    _headers["Content-Length"] = StringUtils::sizeToString(_body.size());
}

void Response::appendBody(const std::string& data) {
    _body.append(data);
    _headers["Content-Length"] = StringUtils::sizeToString(_body.size());
}

void Response::setCookie(const std::string& name, const std::string& value,
                         const std::string& path, const std::string& domain,
                         int maxAge, bool httpOnly, bool secure) {
    std::string cookie = name + "=" + value;

    if (!path.empty()) {
        cookie += "; Path=" + path;
    }

    if (!domain.empty()) {
        cookie += "; Domain=" + domain;
    }

    if (maxAge >= 0) {
        std::ostringstream oss;
        oss << "; Max-Age=" << maxAge;
        cookie += oss.str();
    }

    if (httpOnly) {
        cookie += "; HttpOnly";
    }

    if (secure) {
        cookie += "; Secure";
    }

    _setCookies.push_back(cookie);
}

int Response::getStatusCode() const { return _statusCode; }
const std::string& Response::getBody() const { return _body; }
bool Response::isReady() const { return _isReady; }
void Response::setReady(bool ready) { _isReady = ready; }

std::string Response::toString() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << _statusCode << " " << _statusMessage << "\r\n";

    // Add Date header (required by HTTP/1.1)
    if (_headers.find("Date") == _headers.end()) {
        std::time_t now = std::time(NULL);
        char dateBuf[128];
        std::strftime(dateBuf, sizeof(dateBuf), "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&now));
        oss << "Date: " << dateBuf << "\r\n";
    }

    // Add Server header
    if (_headers.find("Server") == _headers.end()) {
        oss << "Server: webserv/1.0\r\n";
    }

    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it) {
        oss << it->first << ": " << it->second << "\r\n";
    }

    for (size_t i = 0; i < _setCookies.size(); ++i) {
        oss << "Set-Cookie: " << _setCookies[i] << "\r\n";
    }

    oss << "\r\n";
    oss << _body;
    return oss.str();
}

void Response::buildError(int statusCode, const std::map<int, std::string>& customErrorPages, const std::string& root) {
    setStatus(statusCode);

    std::map<int, std::string>::const_iterator it = customErrorPages.find(statusCode);
    if (it != customErrorPages.end() && !root.empty()) {
        std::string errorPagePath = it->second;
        // Remove leading slash if root doesn't end with one
        if (!errorPagePath.empty() && errorPagePath[0] == '/') {
            errorPagePath = errorPagePath.substr(1);
        }
        std::string fullPath = root;
        if (!fullPath.empty() && fullPath[fullPath.size() - 1] != '/') {
            fullPath += '/';
        }
        fullPath += errorPagePath;

        try {
            std::string content = FileUtils::readFile(fullPath);
            setBody(content);
            // Determine content type from file extension
            std::string ext = FileUtils::getFileExtension(fullPath);
            if (ext == ".html" || ext == ".htm") {
                setHeader("Content-Type", "text/html");
            } else {
                setHeader("Content-Type", StringUtils::getMimeType(ext));
            }
            _isReady = true;
            return;
        } catch (const std::exception& e) {
            // Fall through to default error page
        }
    }

    setBody(_getDefaultErrorPage(statusCode));
    setHeader("Content-Type", "text/html");
    _isReady = true;
}

std::string Response::_getDefaultErrorPage(int code) const {
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n"
        << "<html>\n"
        << "<head><title>" << code << " " << _statusMessage << "</title></head>\n"
        << "<body>\n"
        << "<center><h1>" << code << " " << _statusMessage << "</h1></center>\n"
        << "<hr><center>webserv/1.0</center>\n"
        << "</body>\n"
        << "</html>\n";
    return oss.str();
}
