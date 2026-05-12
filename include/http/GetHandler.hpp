#ifndef GET_HANDLER_HPP
#define GET_HANDLER_HPP

#include "http/IHttpMethodHandler.hpp"

class GetHandler : public IHttpMethodHandler {
public:
    GetHandler();
    ~GetHandler();

    void handle(const Request& request, Response& response,
                const RouteConfig& route, const ServerConfig& server);

private:
    GetHandler(const GetHandler& other);
    GetHandler& operator=(const GetHandler& other);

    void _serveFile(const std::string& path, Response& response);
    void _serveDirectory(const std::string& path, const RouteConfig& route, Response& response);
    void _generateAutoindex(const std::string& path, const std::string& uri, Response& response);
    std::string _resolvePath(const Request& request, const RouteConfig& route);
};

#endif
