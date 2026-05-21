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

    void _serveFile(const std::string& path, const Request& request, Response& response);
    void _serveDirectory(const std::string& path, const RouteConfig& route, Response& response);
    void _generateAutoindex(const std::string& path, const std::string& uri, Response& response);
    std::string _resolvePath(const std::string& uri, const RouteConfig& route);
    std::string _tryFilesResolve(const std::string& uri, const RouteConfig& route);

    // Range request: returns false if Range header is absent or invalid (fall through to 200)
    bool _handleRange(const std::string& path, size_t fileSize,
                      const std::string& rangeHeader, Response& response);
    // ETag helpers
    std::string _computeEtag(const struct stat& st) const;
    bool _checkConditional(const struct stat& st, const Request& request, Response& response);
};

#endif
