#include "config/ConfigParser.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"
#include <cctype>

ConfigParser::ConfigParser() : _pos(0) {}

ConfigParser::~ConfigParser() {}

std::vector<ServerConfig> ConfigParser::parse(const std::string& filepath) {
    _loadFile(filepath);
    _pos = 0;
    std::vector<ServerConfig> servers = _parseServers();
    _validateConfig(servers);
    return servers;
}

void ConfigParser::_loadFile(const std::string& filepath) {
    std::ifstream file(filepath.c_str());
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filepath);
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    _rawContent = oss.str();
    file.close();
}

void ConfigParser::_skipWhitespace() {
    while (_pos < _rawContent.size() && std::isspace(_rawContent[_pos])) {
        ++_pos;
    }
}

void ConfigParser::_skipComments() {
    _skipWhitespace();
    if (_pos < _rawContent.size() && _rawContent[_pos] == '#') {
        while (_pos < _rawContent.size() && _rawContent[_pos] != '\n') {
            ++_pos;
        }
        _skipComments();
    }
}

bool ConfigParser::_expect(char c) {
    _skipComments();
    if (_pos < _rawContent.size() && _rawContent[_pos] == c) {
        ++_pos;
        return true;
    }
    return false;
}

std::string ConfigParser::_parseToken() {
    _skipComments();
    std::string token;
    while (_pos < _rawContent.size() && !std::isspace(_rawContent[_pos]) &&
           _rawContent[_pos] != ';' && _rawContent[_pos] != '{' &&
           _rawContent[_pos] != '}') {
        token += _rawContent[_pos];
        ++_pos;
    }
    return token;
}

std::string ConfigParser::_parseValue() {
    _skipComments();
    std::string value;
    bool inQuotes = false;
    char quoteChar = 0;

    while (_pos < _rawContent.size()) {
        char c = _rawContent[_pos];
        if (!inQuotes && (c == ';' || c == '{' || c == '}')) {
            break;
        }
        if (c == '"' || c == '\'') {
            if (!inQuotes) {
                inQuotes = true;
                quoteChar = c;
            } else if (c == quoteChar) {
                inQuotes = false;
            } else {
                value += c;
            }
        } else {
            value += c;
        }
        ++_pos;
    }
    return StringUtils::trim(value);
}

std::vector<ServerConfig> ConfigParser::_parseServers() {
    std::vector<ServerConfig> servers;
    while (true) {
        _skipComments();
        if (_pos >= _rawContent.size()) break;

        std::string token = _parseToken();
        if (token == "server") {
            if (!_expect('{')) {
                throw std::runtime_error("Expected '{' after 'server'");
            }
            servers.push_back(_parseServerBlock());
            if (!_expect('}')) {
                throw std::runtime_error("Expected '}' to close server block");
            }
        } else if (!token.empty()) {
            throw std::runtime_error("Unexpected token: " + token);
        }
    }
    return servers;
}

ServerConfig ConfigParser::_parseServerBlock() {
    ServerConfig server;
    while (true) {
        _skipComments();
        if (_pos >= _rawContent.size()) {
            throw std::runtime_error("Unexpected end of file in server block");
        }
        if (_rawContent[_pos] == '}') break;

        std::string directive = _parseToken();
        if (directive == "listen") {
            _parseListenDirective(server);
        } else if (directive == "server_name") {
            _parseServerNameDirective(server);
        } else if (directive == "client_max_body_size") {
            _parseClientMaxBodySizeDirective(server);
        } else if (directive == "error_page") {
            _parseErrorPageDirective(server);
        } else if (directive == "location") {
            std::string locationPath = _parseValue();
            if (!_expect('{')) {
                throw std::runtime_error("Expected '{' after 'location'");
            }
            server.addRoute(_parseLocationBlock(locationPath));
            if (!_expect('}')) {
                throw std::runtime_error("Expected '}' to close location block");
            }
        } else if (directive.empty()) {
            break;
        } else {
            throw std::runtime_error("Unknown directive in server block: " + directive);
        }
    }
    return server;
}

RouteConfig ConfigParser::_parseLocationBlock(const std::string& path) {
    RouteConfig route;
    route.setPath(path);

    while (true) {
        _skipComments();
        if (_pos >= _rawContent.size() || _rawContent[_pos] == '}') break;

        std::string directive = _parseToken();
        if (directive == "root") {
            _parseRootDirective(route);
        } else if (directive == "index") {
            _parseIndexDirective(route);
        } else if (directive == "autoindex") {
            _parseAutoindexDirective(route);
        } else if (directive == "allow_methods") {
            _parseAllowMethodsDirective(route);
        } else if (directive == "redirect") {
            _parseRedirectDirective(route);
        } else if (directive == "upload_store") {
            _parseUploadStoreDirective(route);
        } else if (directive == "cgi_extension") {
            _parseCgiExtensionDirective(route);
        } else if (directive.empty()) {
            break;
        } else {
            throw std::runtime_error("Unknown directive in location block: " + directive);
        }
    }
    return route;
}

void ConfigParser::_parseListenDirective(ServerConfig& server) {
    std::string value = _parseValue();
    int port;
    size_t colonPos = value.find(':');
    if (colonPos != std::string::npos) {
        std::string host = value.substr(0, colonPos);
        port = std::atoi(value.substr(colonPos + 1).c_str());
        server.addListen(host, port);
    } else {
        port = std::atoi(value.c_str());
        server.addListen("0.0.0.0", port);
    }
    if (port < 0 || port > 65535) {
        throw std::runtime_error("Port out of range: " + value + " (valid range: 0-65535)");
    }
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after listen directive");
    }
}

void ConfigParser::_parseServerNameDirective(ServerConfig& server) {
    server.setServerName(_parseValue());
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after server_name directive");
    }
}

void ConfigParser::_parseClientMaxBodySizeDirective(ServerConfig& server) {
    server.setClientMaxBodySize(_parseSize(_parseValue()));
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after client_max_body_size directive");
    }
}

void ConfigParser::_parseErrorPageDirective(ServerConfig& server) {
    std::string value = _parseValue();
    std::vector<std::string> parts = StringUtils::split(value, ' ');
    if (parts.size() < 2) {
        throw std::runtime_error("Invalid error_page directive");
    }
    std::string uri = parts.back();
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        int code = std::atoi(parts[i].c_str());
        server.addErrorPage(code, uri);
    }
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after error_page directive");
    }
}

void ConfigParser::_parseRootDirective(RouteConfig& route) {
    route.setRoot(_parseValue());
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after root directive");
    }
}

void ConfigParser::_parseIndexDirective(RouteConfig& route) {
    route.setIndex(_parseValue());
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after index directive");
    }
}

void ConfigParser::_parseAutoindexDirective(RouteConfig& route) {
    std::string value = StringUtils::toLower(_parseValue());
    route.setAutoindex(value == "on");
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after autoindex directive");
    }
}

void ConfigParser::_parseAllowMethodsDirective(RouteConfig& route) {
    std::string value = _parseValue();
    std::vector<std::string> methods = StringUtils::split(value, ' ');
    for (size_t i = 0; i < methods.size(); ++i) {
        route.addAllowedMethod(StringUtils::toUpper(StringUtils::trim(methods[i])));
    }
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after allow_methods directive");
    }
}

void ConfigParser::_parseRedirectDirective(RouteConfig& route) {
    route.setRedirect(_parseValue());
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after redirect directive");
    }
}

void ConfigParser::_parseUploadStoreDirective(RouteConfig& route) {
    route.setUploadStore(_parseValue());
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after upload_store directive");
    }
}

void ConfigParser::_parseCgiExtensionDirective(RouteConfig& route) {
    std::string value = _parseValue();
    std::vector<std::string> parts = StringUtils::split(value, ' ');
    if (parts.size() != 2) {
        throw std::runtime_error("Invalid cgi_extension directive");
    }
    route.addCgiHandler(parts[0], parts[1]);
    if (!_expect(';')) {
        throw std::runtime_error("Expected ';' after cgi_extension directive");
    }
}

void ConfigParser::_validateConfig(const std::vector<ServerConfig>& servers) {
    if (servers.empty()) {
        throw std::runtime_error("No server blocks found in configuration");
    }
    // TODO: Add more validation (duplicate ports, etc.)
}

size_t ConfigParser::_parseSize(const std::string& value) {
    if (value.empty()) return 0;

    size_t num = 0;
    size_t i = 0;
    while (i < value.size() && std::isdigit(value[i])) {
        num = num * 10 + (value[i] - '0');
        ++i;
    }

    if (i < value.size()) {
        char suffix = std::toupper(value[i]);
        if (suffix == 'K') num *= 1024;
        else if (suffix == 'M') num *= 1024 * 1024;
        else if (suffix == 'G') num *= 1024 * 1024 * 1024;
    }

    return num;
}
