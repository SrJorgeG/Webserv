#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include "Webserv.hpp"
#include "http/Request.hpp"

class HttpParser {
public:
    HttpParser();
    ~HttpParser();

    // Parsea datos raw. Puede llamarse multiples veces con datos parciales.
    ParseResult parse(const std::string& rawData, Request& outRequest);

    void reset();
    bool headersComplete() const;

    // Get leftover data in the internal buffer (for pipelining support)
    std::string getLeftoverData() const;

private:
    HttpParser(const HttpParser& other);
    HttpParser& operator=(const HttpParser& other);

    std::string _buffer;
    bool _headersComplete;
    bool _requestLineComplete;

    ParseResult _parseRequestLine(Request& request);
    ParseResult _parseHeaders(Request& request);
    ParseResult _parseBody(Request& request);
    ParseResult _parseChunkedBody(Request& request);

    bool _isValidMethod(const std::string& method) const;
    bool _isValidUri(const std::string& uri) const;
    bool _isValidVersion(const std::string& version) const;
};

#endif
