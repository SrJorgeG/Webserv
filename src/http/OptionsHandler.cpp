#include "http/OptionsHandler.hpp"
#include "utils/StringUtils.hpp"

OptionsHandler::OptionsHandler() {}
OptionsHandler::~OptionsHandler() {}

// OPTIONS replies with the methods allowed on the matched route.
// OPTIONS itself and HEAD (implied whenever GET is allowed) are always
// included. The response has no body; Content-Length: 0 is required
// so that HTTP/1.1 keep-alive clients don't wait for a body.
void OptionsHandler::handle(const Request& request, Response& response,
                            const RouteConfig& route, const ServerConfig& server) {
    (void)request; (void)server;

    const std::vector<std::string>& allowed = route.getAllowedMethods();
    std::string allow = "OPTIONS";

    bool hasGet = false;
    if (allowed.empty()) {
        allow += ", GET, POST, DELETE, HEAD, PUT";
    } else {
        for (size_t i = 0; i < allowed.size(); ++i) {
            allow += ", " + allowed[i];
            if (allowed[i] == "GET") hasGet = true;
        }
        // HEAD is always available when GET is — it shares the same logic
        if (hasGet) allow += ", HEAD";
    }

    response.setStatus(200);
    response.setHeader("Allow", allow);
    response.setHeader("Content-Length", "0");
    response.setReady(true);
}
