#include "http/HeadHandler.hpp"
#include "http/GetHandler.hpp"

HeadHandler::HeadHandler() {}
HeadHandler::~HeadHandler() {}

// HEAD is identical to GET but the response body must be empty.
// We run the full GET logic so Content-Length, Content-Type, ETag,
// Last-Modified and all caching headers are computed correctly, then
// wipe the body string without touching those headers.
void HeadHandler::handle(const Request& request, Response& response,
                         const RouteConfig& route, const ServerConfig& server) {
    GetHandler get;
    get.handle(request, response, route, server);
    // Preserve Content-Length (already set by GetHandler) but send no body.
    response.clearBody();
}
