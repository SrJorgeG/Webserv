#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include "Webserv.hpp"
#include <map>

class Response {
public:
    Response();
    ~Response();
    Response(const Response& other);
    Response& operator=(const Response& other);

    void clear();

    // Setters
    void setStatus(int code);
    void setHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);
    void appendBody(const std::string& data);
    void setCookie(const std::string& name, const std::string& value,
                   const std::string& path = "/", const std::string& domain = "",
                   int maxAge = -1, bool httpOnly = true, bool secure = false);

    // Getters
    int getStatusCode() const;
    const std::string& getBody() const;
    bool isReady() const;

    void setReady(bool ready);

    // Serializa la respuesta a string HTTP
    std::string toString() const;

    // Construye respuesta de error usando pagina por defecto
    void buildError(int statusCode, const std::map<int, std::string>& customErrorPages, const std::string& root = "");

private:
    int _statusCode;
    std::string _statusMessage;
    std::map<std::string, std::string> _headers;
    std::vector<std::string> _setCookies;
    std::string _body;
    bool _isReady;

    std::string _getDefaultErrorPage(int code) const;
};

#endif
