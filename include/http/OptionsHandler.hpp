#ifndef OPTIONS_HANDLER_HPP
#define OPTIONS_HANDLER_HPP

#include "http/IHttpMethodHandler.hpp"

class OptionsHandler : public IHttpMethodHandler
{
public:
    OptionsHandler();
    ~OptionsHandler();

    void handle(const Request& request, Response& response,
                const RouteConfig& route, const ServerConfig& server);

private:
    OptionsHandler(const OptionsHandler& other);
    OptionsHandler& operator=(const OptionsHandler& other);
};

#endif
