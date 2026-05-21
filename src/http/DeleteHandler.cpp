#include "http/DeleteHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"
#include "utils/StringUtils.hpp"

DeleteHandler::DeleteHandler() {}

DeleteHandler::~DeleteHandler() {}

void DeleteHandler::handle(const Request& request, Response& response,
                           const RouteConfig& route, const ServerConfig& server) {
    

    std::string decodedUri = StringUtils::decodeUrl(request.getUri());
    std::string normalizedPath = StringUtils::resolvePath(decodedUri, route.getPath(), route.getRoot());

    if (normalizedPath.empty()) {
        response.buildError(403, server.getErrorPages(), route.getRoot());
        return;
    }

    std::string filepath = normalizedPath;

    if (!FileUtils::fileExists(filepath)) {
        response.buildError(404, server.getErrorPages(), route.getRoot());
        return;
    }

    if (FileUtils::isDirectory(filepath)) {
        response.buildError(403, server.getErrorPages(), route.getRoot());
        return;
    }

    if (!FileUtils::isWritable(filepath)) {
        response.buildError(403, server.getErrorPages(), route.getRoot());
        return;
    }

    std::string parentDir = FileUtils::getParentDirectory(filepath);
    if (!FileUtils::isWritable(parentDir)) {
        response.buildError(403, server.getErrorPages(), route.getRoot());
        return;
    }

    if (FileUtils::deleteFile(filepath)) {
        response.setStatus(204);
        response.setHeader("Content-Length", "0");
        response.setReady(true);
    } else {
        response.buildError(500, server.getErrorPages(), route.getRoot());
    }
}
