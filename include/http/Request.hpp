#ifndef REQUEST_HPP
#define REQUEST_HPP

#include "Webserv.hpp"
#include <map>

class Request {
public:
    Request();
    ~Request();
    Request(const Request& other);
    Request& operator=(const Request& other);

    void clear();
    bool isComplete() const;

    // Getters
    const std::string& getMethod() const;
    const std::string& getUri() const;
    const std::string& getVersion() const;
    const std::map<std::string, std::string>& getHeaders() const;
    const std::string& getBody() const;
    const std::string& getQueryString() const;
    size_t getContentLength() const;
    bool isChunked() const;

    // Setters
    void setMethod(const std::string& method);
    void setUri(const std::string& uri);
    void setVersion(const std::string& version);
    void addHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);
    void appendBody(const std::string& data);
    void setContentLength(size_t length);
    void setChunked(bool chunked);
    void setQueryString(const std::string& query);

    std::string getHeader(const std::string& key) const;
    bool hasHeader(const std::string& key) const;

    // Cookie methods
    void parseCookies();
    const std::map<std::string, std::string>& getCookies() const;
    std::string getCookie(const std::string& name) const;

private:
    std::string _method;
    std::string _uri;
    std::string _version;
    std::map<std::string, std::string> _headers;
    std::string _body;
    std::string _queryString;
    size_t _contentLength;
    bool _isChunked;
    bool _isComplete;
    std::map<std::string, std::string> _cookies;
};

#endif
