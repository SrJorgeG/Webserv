#include "http/PutHandler.hpp"
#include "utils/StringUtils.hpp"
#include "utils/FileUtils.hpp"
#include "utils/Logger.hpp"

PutHandler::PutHandler() {}
PutHandler::~PutHandler() {}

// PUT creates or completely replaces a resource at the given URI path.
// This is semantically different from POST: PUT is idempotent — calling
// it twice with the same body leaves the server in the same state.
// POST is not idempotent (each call may create a new resource).
//
// Responses:
//   201 Created  — the resource did not exist and was created
//   200 OK       — the resource existed and was replaced
//   403          — path traversal attempt or directory target
//   500          — write failed (permissions, disk full, etc.)
void PutHandler::handle(const Request& request, Response& response,
                        const RouteConfig& route, const ServerConfig& server) {
    std::string decodedUri = StringUtils::decodeUrl(request.getUri());
    std::string normalizedPath = StringUtils::resolvePath(decodedUri, route.getPath(), route.getRoot());

    if (normalizedPath.empty()) {
        response.buildError(403, server.getErrorPages(), route.getRoot());
        return;
    }

    if (FileUtils::isDirectory(normalizedPath)) {
        // Cannot replace a directory with a file resource
        response.buildError(409, server.getErrorPages(), route.getRoot());
        return;
    }

    // Ensure parent directory exists
    std::string parent = FileUtils::getParentDirectory(normalizedPath);
    if (!FileUtils::fileExists(parent)) {
        response.buildError(409, server.getErrorPages(), route.getRoot());
        return;
    }

    bool existed = FileUtils::fileExists(normalizedPath);

    if (!FileUtils::writeFile(normalizedPath, request.getBody())) {
        LOG_ERROR("PUT: failed to write " + normalizedPath);
        response.buildError(500, server.getErrorPages(), route.getRoot());
        return;
    }

    response.setStatus(existed ? 200 : 201);
    response.setHeader("Content-Length", "0");
    if (!existed) {
        // Point to the newly created resource
        response.setHeader("Location", StringUtils::stripQueryString(request.getUri()));
    }
    response.setReady(true);
}
