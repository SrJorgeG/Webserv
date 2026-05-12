#include "http/HttpParser.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"

HttpParser::HttpParser()
    : _headersComplete(false), _requestLineComplete(false) {}

HttpParser::~HttpParser() {}

ParseResult HttpParser::parse(const std::string& rawData, Request& outRequest) {
    _buffer.append(rawData);

    if (!_requestLineComplete) {
        ParseResult result = _parseRequestLine(outRequest);
        if (result != PARSE_OK) {
            return result;
        }
    }

    if (!_headersComplete) {
        ParseResult result = _parseHeaders(outRequest);
        if (result != PARSE_OK) {
            return result;
        }
    }

    // Check if body is expected
    if (outRequest.getMethod() == "GET" || outRequest.getMethod() == "DELETE" ||
        outRequest.getMethod() == "HEAD") {
        outRequest.setBody("");
        return PARSE_OK;
    }

    std::string contentLength = outRequest.getHeader("Content-Length");
    std::string transferEncoding = outRequest.getHeader("Transfer-Encoding");

    if (contentLength.empty() && transferEncoding.empty()) {
        outRequest.setBody("");
        return PARSE_OK;
    }

if (!transferEncoding.empty() && StringUtils::toLower(transferEncoding).find("chunked") != std::string::npos) {
        outRequest.setChunked(true);
        return _parseChunkedBody(outRequest);
    }

    if (!contentLength.empty()) {
        size_t length = std::atoi(contentLength.c_str());
        outRequest.setContentLength(length);
        if (_buffer.size() >= length) {
            outRequest.setBody(_buffer.substr(0, length));
            _buffer.erase(0, length);
            return PARSE_OK;
        }
    }

    return PARSE_INCOMPLETE;
}

void HttpParser::reset() {
    _buffer.clear();
    _headersComplete = false;
    _requestLineComplete = false;
}

std::string HttpParser::getLeftoverData() const {
    return _buffer;
}

bool HttpParser::headersComplete() const {
    return _headersComplete;
}

ParseResult HttpParser::_parseRequestLine(Request& request) {
    size_t pos = _buffer.find("\r\n");
    if (pos == std::string::npos) {
        return PARSE_INCOMPLETE;
    }

    std::string line = _buffer.substr(0, pos);
    _buffer.erase(0, pos + 2);

    std::vector<std::string> parts = StringUtils::split(line, ' ');
    if (parts.size() != 3) {
        return PARSE_ERROR;
    }

    if (!_isValidMethod(parts[0]) || !_isValidUri(parts[1]) || !_isValidVersion(parts[2])) {
        return PARSE_ERROR;
    }

    request.setMethod(parts[0]);
    request.setUri(parts[1]);
    request.setVersion(parts[2]);

    size_t qpos = parts[1].find('?');
    if (qpos != std::string::npos) {
        request.setQueryString(parts[1].substr(qpos + 1));
    }

    _requestLineComplete = true;
    return PARSE_OK;
}

ParseResult HttpParser::_parseHeaders(Request& request) {
    while (true) {
        size_t pos = _buffer.find("\r\n");
        if (pos == std::string::npos) {
            return PARSE_INCOMPLETE;
        }

        std::string line = _buffer.substr(0, pos);
        _buffer.erase(0, pos + 2);

        if (line.empty()) {
            _headersComplete = true;
            return PARSE_OK;
        }

        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            return PARSE_ERROR;
        }

        std::string key = StringUtils::trim(line.substr(0, colonPos));
        std::string value = StringUtils::trim(line.substr(colonPos + 1));
        request.addHeader(key, value);
    }
}

ParseResult HttpParser::_parseBody(Request& request) {
    // TODO: Implement body parsing based on Content-Length
    (void)request;
    return PARSE_INCOMPLETE;
}

ParseResult HttpParser::_parseChunkedBody(Request& request) {
    while (true) {
        // Find the end of the chunk size line
        size_t crlfPos = _buffer.find("\r\n");
        if (crlfPos == std::string::npos) {
            return PARSE_INCOMPLETE;
        }

        // Extract chunk size (hex format)
        std::string sizeStr = _buffer.substr(0, crlfPos);
        if (sizeStr.empty()) {
            return PARSE_ERROR;
        }

        // Convert hex to number
        size_t chunkSize = 0;
        for (size_t i = 0; i < sizeStr.length(); ++i) {
            char c = sizeStr[i];
            if (c == ' ' || c == '\t') continue; // Allow trailing whitespace
            int digit = -1;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = c - 'A' + 10;
            } else {
                return PARSE_ERROR; // Invalid hex character
            }
            // Check for overflow before multiplication
            if (chunkSize > (SIZE_MAX - digit) / 16) {
                return PARSE_ERROR; // Chunk size too large
            }
            chunkSize = chunkSize * 16 + digit;
        }

        // Remove the size line from buffer (+2 for \r\n)
        _buffer.erase(0, crlfPos + 2);

        if (chunkSize == 0) {
            // Last chunk - remove trailing \r\n and we're done
            if (_buffer.size() < 2) {
                return PARSE_INCOMPLETE;
            }
            // Ignore any trailer headers (read until empty line)
            while (!_buffer.empty()) {
                size_t trailerCrlf = _buffer.find("\r\n");
                if (trailerCrlf == 0) {
                    // Empty line - end of trailers
                    _buffer.erase(0, 2);
                    break;
                }
                if (trailerCrlf == std::string::npos) {
                    return PARSE_INCOMPLETE;
                }
                _buffer.erase(0, trailerCrlf + 2);
            }
            return PARSE_OK;
        }

        // Need at least chunkSize + 2 bytes for data + CRLF
        if (_buffer.size() < chunkSize + 2) {
            return PARSE_INCOMPLETE;
        }

        // Extract chunk data and append to body
        std::string chunkData = _buffer.substr(0, chunkSize);
        request.appendBody(chunkData);

        // Remove chunk data + trailing \r\n
        _buffer.erase(0, chunkSize + 2);
    }
}

bool HttpParser::_isValidMethod(const std::string& method) const {
    return method == "GET" || method == "POST" || method == "DELETE" ||
           method == "HEAD" || method == "PUT";
}

bool HttpParser::_isValidUri(const std::string& uri) const {
    return !uri.empty() && uri[0] == '/';
}

bool HttpParser::_isValidVersion(const std::string& version) const {
    return version == "HTTP/1.0" || version == "HTTP/1.1";
}
