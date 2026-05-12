#include "http/GetHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"
#include "utils/FileUtils.hpp"
#include "http/StatusCodes.hpp"

GetHandler::GetHandler() {}

GetHandler::~GetHandler() {}

void GetHandler::handle(const Request& request, Response& response,
                        const RouteConfig& route, const ServerConfig& server) {
    (void)server;

    std::string path = _resolvePath(request, route);

    if (!FileUtils::fileExists(path)) {
        response.buildError(404, server.getErrorPages(), route.getRoot());
        return;
    }

    if (FileUtils::isDirectory(path)) {
        _serveDirectory(path, route, response);
        return;
    }

    _serveFile(path, response);
}

void GetHandler::_serveFile(const std::string& path, Response& response) {
    try {
        std::string content = FileUtils::readFile(path);
        response.setStatus(200);
        response.setHeader("Content-Type", StringUtils::getMimeType(StringUtils::getExtension(path)));
        response.setBody(content);
        response.setReady(true);
    } catch (const std::exception& e) {
        (void)e;
        response.setStatus(403);
        response.setHeader("Content-Type", "text/html");
        response.setBody("<html><body><h1>403 Forbidden</h1></body></html>");
        response.setReady(true);
    }
}

void GetHandler::_serveDirectory(const std::string& path, const RouteConfig& route, Response& response) {
    if (!route.getIndex().empty()) {
        std::string indexPath = FileUtils::joinPath(path, route.getIndex());
        if (FileUtils::fileExists(indexPath) && !FileUtils::isDirectory(indexPath)) {
            _serveFile(indexPath, response);
            return;
        }
    }

    if (route.getAutoindex()) {
        _generateAutoindex(path, route.getPath(), response);
    } else {
        response.setStatus(403);
        response.setHeader("Content-Type", "text/html");
        response.setBody("<html><body><h1>403 Forbidden</h1></body></html>");
        response.setReady(true);
    }
}

void GetHandler::_generateAutoindex(const std::string& path, const std::string& uri, Response& response) {
    std::ostringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head><title>Index of " << uri << "</title></head>\n"
         << "<body>\n"
         << "<h1>Index of " << uri << "</h1>\n"
         << "<hr>\n"
         << "<table>\n";

    try {
        std::vector<std::string> entries = FileUtils::listDirectory(path);
        for (size_t i = 0; i < entries.size(); ++i) {
            std::string entryPath = FileUtils::joinPath(path, entries[i]);
            std::string displayName = entries[i];
            if (FileUtils::isDirectory(entryPath)) {
                displayName += "/";
            }
            html << "<tr><td><a href=\"" << StringUtils::htmlEscape(displayName) << "\">" << StringUtils::htmlEscape(displayName) << "</a></td></tr>\n";
        }
    } catch (const std::exception& e) {
        (void)e;
    }

    html << "</table>\n"
         << "<hr>\n"
         << "</body>\n"
         << "</html>\n";

    response.setStatus(200);
    response.setHeader("Content-Type", "text/html");
    response.setBody(html.str());
    response.setReady(true);
}

std::string GetHandler::_resolvePath(const Request& request, const RouteConfig& route) {
    std::string decodedUri = StringUtils::decodeUrl(request.getUri());
    std::string routePath = route.getPath();

    if (routePath != "/" && StringUtils::startsWith(decodedUri, routePath)) {
        decodedUri = decodedUri.substr(routePath.size());
    }
    if (decodedUri.empty() || decodedUri[0] != '/') {
        decodedUri = "/" + decodedUri;
    }

    std::string fullPath = FileUtils::joinPath(route.getRoot(), decodedUri);
    std::string normalizedPath = FileUtils::normalizePath(fullPath);

    if (!StringUtils::startsWith(normalizedPath, route.getRoot())) {
        return route.getRoot();
    }

    return normalizedPath;
}
