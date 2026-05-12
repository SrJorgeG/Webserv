#ifndef DELETE_HANDLER_HPP
#define DELETE_HANDLER_HPP

#include "http/IHttpMethodHandler.hpp"

class DeleteHandler : public IHttpMethodHandler {
public:
    DeleteHandler();
    ~DeleteHandler();

    void handle(const Request& request, Response& response,
                const RouteConfig& route, const ServerConfig& server);

private:
    DeleteHandler(const DeleteHandler& other);
    DeleteHandler& operator=(const DeleteHandler& other);
};

#endif
