#ifndef POST_HANDLER_HPP
#define POST_HANDLER_HPP

#include "http/IHttpMethodHandler.hpp"

class PostHandler : public IHttpMethodHandler {
public:
    PostHandler();
    ~PostHandler();

    void handle(const Request& request, Response& response,
                const RouteConfig& route, const ServerConfig& server);

private:
    PostHandler(const PostHandler& other);
    PostHandler& operator=(const PostHandler& other);
};

#endif
