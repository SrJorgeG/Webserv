#include "http/Request.hpp"
#include <cctype>

Request::Request()
    : _contentLength(0), _isChunked(false), _isComplete(false) {}

Request::~Request() {}

Request::Request(const Request& other) {
    *this = other;
}

Request& Request::operator=(const Request& other) {
    if (this != &other) {
        _method = other._method;
        _uri = other._uri;
        _version = other._version;
        _headers = other._headers;
        _body = other._body;
        _queryString = other._queryString;
        _contentLength = other._contentLength;
        _isChunked = other._isChunked;
        _isComplete = other._isComplete;
        _cookies = other._cookies;
    }
    return *this;
}

void Request::clear() {
    _method.clear();
    _uri.clear();
    _version.clear();
    _headers.clear();
    _body.clear();
    _queryString.clear();
    _contentLength = 0;
    _isChunked = false;
    _isComplete = false;
    _cookies.clear();
}

bool Request::isComplete() const {
    return _isComplete;
}

const std::string& Request::getMethod() const { return _method; }
const std::string& Request::getUri() const { return _uri; }
const std::string& Request::getVersion() const { return _version; }
const std::map<std::string, std::string>& Request::getHeaders() const { return _headers; }
const std::string& Request::getBody() const { return _body; }
const std::string& Request::getQueryString() const { return _queryString; }
size_t Request::getContentLength() const { return _contentLength; }
bool Request::isChunked() const { return _isChunked; }

void Request::setMethod(const std::string& method) { _method = method; }
void Request::setUri(const std::string& uri) { _uri = uri; }
void Request::setVersion(const std::string& version) { _version = version; }
void Request::addHeader(const std::string& key, const std::string& value) { _headers[key] = value; }
void Request::setBody(const std::string& body) { _body = body; }
void Request::appendBody(const std::string& data) { _body.append(data); }

void Request::setContentLength(size_t length) { _contentLength = length; }
void Request::setChunked(bool chunked) { _isChunked = chunked; }
void Request::setQueryString(const std::string& query) { _queryString = query; }

std::string Request::getHeader(const std::string& key) const {
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it) {
        if (it->first.size() == key.size()) {
            bool match = true;
            for (size_t i = 0; i < key.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(it->first[i])) !=
                    std::tolower(static_cast<unsigned char>(key[i]))) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return it->second;
            }
        }
    }
    return "";
}

bool Request::hasHeader(const std::string& key) const {
    for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
         it != _headers.end(); ++it) {
        if (it->first.size() == key.size()) {
            bool match = true;
            for (size_t i = 0; i < key.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(it->first[i])) !=
                    std::tolower(static_cast<unsigned char>(key[i]))) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }
    return false;
}

void Request::parseCookies() {
    std::string cookieHeader = getHeader("Cookie");
    if (cookieHeader.empty()) {
        return;
    }

    _cookies.clear();

    size_t start = 0;
    while (start < cookieHeader.size()) {
        size_t semicolon = cookieHeader.find(';', start);
        std::string pair;
        if (semicolon == std::string::npos) {
            pair = cookieHeader.substr(start);
            start = cookieHeader.size();
        } else {
            pair = cookieHeader.substr(start, semicolon - start);
            start = semicolon + 1;
        }

        while (start < cookieHeader.size() && cookieHeader[start] == ' ') {
            ++start;
        }

        size_t equals = pair.find('=');
        if (equals != std::string::npos) {
            std::string name = pair.substr(0, equals);
            std::string value = pair.substr(equals + 1);

            while (!name.empty() && name[0] == ' ') {
                name = name.substr(1);
            }
            while (!name.empty() && name[name.size() - 1] == ' ') {
                name = name.substr(0, name.size() - 1);
            }
            while (!value.empty() && value[0] == ' ') {
                value = value.substr(1);
            }
            while (!value.empty() && value[value.size() - 1] == ' ') {
                value = value.substr(0, value.size() - 1);
            }

            if (!name.empty()) {
                _cookies[name] = value;
            }
        }
    }
}

const std::map<std::string, std::string>& Request::getCookies() const {
    return _cookies;
}

std::string Request::getCookie(const std::string& name) const {
    std::map<std::string, std::string>::const_iterator it = _cookies.find(name);
    if (it == _cookies.end()) {
        return "";
    }
    return it->second;
}
