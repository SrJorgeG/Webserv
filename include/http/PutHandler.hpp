#ifndef PUT_HANDLER_HPP
#define PUT_HANDLER_HPP

#include "http/IHttpMethodHandler.hpp"

class PutHandler : public IHttpMethodHandler
{
public:
    PutHandler();
    ~PutHandler();

    void handle(const Request& request, Response& response,
                const RouteConfig& route, const ServerConfig& server);

private:
    PutHandler(const PutHandler& other);
    PutHandler& operator=(const PutHandler& other);
};

#endif
