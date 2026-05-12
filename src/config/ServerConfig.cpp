#include "config/ServerConfig.hpp"
#include "utils/StringUtils.hpp"

ServerConfig::ServerConfig()
    : _clientMaxBodySize(DEFAULT_MAX_BODY_SIZE) {}

ServerConfig::~ServerConfig() {}

ServerConfig::ServerConfig(const ServerConfig& other) {
    *this = other;
}

ServerConfig& ServerConfig::operator=(const ServerConfig& other) {
    if (this != &other) {
        _listens = other._listens;
        _serverName = other._serverName;
        _clientMaxBodySize = other._clientMaxBodySize;
        _errorPages = other._errorPages;
        _routes = other._routes;
    }
    return *this;
}

const std::vector<std::pair<std::string, int> >& ServerConfig::getListens() const {
    return _listens;
}

const std::string& ServerConfig::getServerName() const {
    return _serverName;
}

size_t ServerConfig::getClientMaxBodySize() const {
    return _clientMaxBodySize;
}

const std::map<int, std::string>& ServerConfig::getErrorPages() const {
    return _errorPages;
}

const std::vector<RouteConfig>& ServerConfig::getRoutes() const {
    return _routes;
}

void ServerConfig::addListen(const std::string& host, int port) {
    _listens.push_back(std::make_pair(host, port));
}

void ServerConfig::setServerName(const std::string& name) {
    _serverName = name;
}

void ServerConfig::setClientMaxBodySize(size_t size) {
    _clientMaxBodySize = size;
}

void ServerConfig::addErrorPage(int code, const std::string& path) {
    _errorPages[code] = path;
}

void ServerConfig::addRoute(const RouteConfig& route) {
    _routes.push_back(route);
}

const RouteConfig* ServerConfig::findRoute(const std::string& uri) const {
    const RouteConfig* bestMatch = NULL;
    size_t bestLen = 0;

    // Normalize URI: replace // with / and remove trailing /
    std::string normalizedUri = uri;
    size_t pos = 0;
    while (pos < normalizedUri.size()) {
        if (normalizedUri[pos] == '/' && pos + 1 < normalizedUri.size() && normalizedUri[pos + 1] == '/') {
            normalizedUri.erase(pos + 1, 1);
        } else {
            ++pos;
        }
    }
    if (!normalizedUri.empty() && normalizedUri[normalizedUri.size() - 1] == '/' && normalizedUri.size() > 1) {
        normalizedUri.erase(normalizedUri.size() - 1);
    }

    for (size_t i = 0; i < _routes.size(); ++i) {
        const std::string& path = _routes[i].getPath();
        if (StringUtils::startsWith(normalizedUri, path)) {
            if (path.size() > bestLen) {
                bestLen = path.size();
                bestMatch = &_routes[i];
            }
        }
    }
    return bestMatch;
}

std::string ServerConfig::getErrorPage(int code) const {
    std::map<int, std::string>::const_iterator it = _errorPages.find(code);
    if (it != _errorPages.end()) {
        return it->second;
    }
    return "";
}
