#include "config/RouteConfig.hpp"
#include "utils/StringUtils.hpp"

RouteConfig::RouteConfig()
    : _autoindex(false) {}

RouteConfig::~RouteConfig() {}

RouteConfig::RouteConfig(const RouteConfig& other) {
    *this = other;
}

RouteConfig& RouteConfig::operator=(const RouteConfig& other) {
    if (this != &other) {
        _path = other._path;
        _root = other._root;
        _index = other._index;
        _autoindex = other._autoindex;
        _allowedMethods = other._allowedMethods;
        _redirect = other._redirect;
        _uploadStore = other._uploadStore;
        _cgiHandlers = other._cgiHandlers;
    }
    return *this;
}

const std::string& RouteConfig::getPath() const { return _path; }
const std::string& RouteConfig::getRoot() const { return _root; }
const std::string& RouteConfig::getIndex() const { return _index; }
bool RouteConfig::getAutoindex() const { return _autoindex; }
const std::vector<std::string>& RouteConfig::getAllowedMethods() const { return _allowedMethods; }
const std::string& RouteConfig::getRedirect() const { return _redirect; }
const std::string& RouteConfig::getUploadStore() const { return _uploadStore; }
const std::map<std::string, std::string>& RouteConfig::getCgiHandlers() const { return _cgiHandlers; }

void RouteConfig::setPath(const std::string& path) { _path = path; }
void RouteConfig::setRoot(const std::string& root) { _root = root; }
void RouteConfig::setIndex(const std::string& index) { _index = index; }
void RouteConfig::setAutoindex(bool autoindex) { _autoindex = autoindex; }
void RouteConfig::addAllowedMethod(const std::string& method) { _allowedMethods.push_back(method); }
void RouteConfig::setRedirect(const std::string& redirect) { _redirect = redirect; }
void RouteConfig::setUploadStore(const std::string& path) { _uploadStore = path; }
void RouteConfig::addCgiHandler(const std::string& extension, const std::string& interpreter) {
    _cgiHandlers[extension] = interpreter;
}

bool RouteConfig::isMethodAllowed(const std::string& method) const {
    if (_allowedMethods.empty()) return true;
    for (size_t i = 0; i < _allowedMethods.size(); ++i) {
        if (_allowedMethods[i] == method) return true;
    }
    return false;
}

bool RouteConfig::hasCgiHandler(const std::string& extension) const {
    if (_cgiHandlers.find(extension) != _cgiHandlers.end()) return true;
    if (extension[0] == '.' && _cgiHandlers.find(extension.substr(1)) != _cgiHandlers.end()) return true;
    if (extension[0] != '.' && _cgiHandlers.find("." + extension) != _cgiHandlers.end()) return true;
    return false;
}

std::string RouteConfig::getCgiInterpreter(const std::string& extension) const {
    std::map<std::string, std::string>::const_iterator it = _cgiHandlers.find(extension);
    if (it != _cgiHandlers.end()) return it->second;
    if (extension[0] == '.') {
        it = _cgiHandlers.find(extension.substr(1));
        if (it != _cgiHandlers.end()) return it->second;
    } else {
        it = _cgiHandlers.find("." + extension);
        if (it != _cgiHandlers.end()) return it->second;
    }
    return "";
}

bool RouteConfig::isUploadEnabled() const {
    return !_uploadStore.empty();
}
