#include "http/DeleteHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/FileUtils.hpp"
#include "utils/StringUtils.hpp"

DeleteHandler::DeleteHandler() {}

DeleteHandler::~DeleteHandler() {}

void DeleteHandler::handle(const Request& request, Response& response,
                           const RouteConfig& route, const ServerConfig& server) {
    (void)server;

    std::string path = route.getRoot();
    std::string decodedUri = StringUtils::decodeUrl(request.getUri());
    size_t qpos = decodedUri.find('?');
    if (qpos != std::string::npos) {
        decodedUri = decodedUri.substr(0, qpos);
    }
    std::string routePath = route.getPath();

    if (routePath != "/" && StringUtils::startsWith(decodedUri, routePath)) {
        if (decodedUri.size() > routePath.size()) {
            decodedUri = decodedUri.substr(routePath.size());
        }
    }

    std::string fullPath = FileUtils::joinPath(path, decodedUri);
    std::string normalizedPath = FileUtils::normalizePath(fullPath);

    if (!StringUtils::startsWith(normalizedPath, route.getRoot())) {
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
