#ifndef I_HTTP_METHOD_HANDLER_HPP
#define I_HTTP_METHOD_HANDLER_HPP

#include "http/Request.hpp"
#include "http/Response.hpp"
#include "config/RouteConfig.hpp"
#include "config/ServerConfig.hpp"

class IHttpMethodHandler {
public:
    virtual ~IHttpMethodHandler() {}
    virtual void handle(const Request& request, Response& response,
                        const RouteConfig& route, const ServerConfig& server) = 0;
};

#endif
