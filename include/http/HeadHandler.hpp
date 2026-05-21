#ifndef HEAD_HANDLER_HPP
#define HEAD_HANDLER_HPP

#include "http/IHttpMethodHandler.hpp"

class HeadHandler : public IHttpMethodHandler {
public:
    HeadHandler();
    ~HeadHandler();

    void handle(const Request& request, Response& response,
                const RouteConfig& route, const ServerConfig& server);

private:
    HeadHandler(const HeadHandler& other);
    HeadHandler& operator=(const HeadHandler& other);
};

#endif
