#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include "Webserv.hpp"
#include "config/ServerConfig.hpp"
#include <vector>

class ConfigParser {
public:
    ConfigParser();
    ~ConfigParser();

    std::vector<ServerConfig> parse(const std::string& filepath);

private:
    ConfigParser(const ConfigParser& other);
    ConfigParser& operator=(const ConfigParser& other);

    std::string _rawContent;
    size_t _pos;

    void _loadFile(const std::string& filepath);
    void _skipWhitespace();
    void _skipComments();
    bool _expect(char c);
    std::string _parseToken();
    std::string _parseValue();

    std::vector<ServerConfig> _parseServers();
    ServerConfig _parseServerBlock();
    RouteConfig _parseLocationBlock(const std::string& path);

    void _parseListenDirective(ServerConfig& server);
    void _parseServerNameDirective(ServerConfig& server);
    void _parseClientMaxBodySizeDirective(ServerConfig& server);
    void _parseErrorPageDirective(ServerConfig& server);
    void _parseRootDirective(RouteConfig& route);
    void _parseIndexDirective(RouteConfig& route);
    void _parseAutoindexDirective(RouteConfig& route);
    void _parseAllowMethodsDirective(RouteConfig& route);
    void _parseRedirectDirective(RouteConfig& route);
    void _parseUploadStoreDirective(RouteConfig& route);
    void _parseCgiExtensionDirective(RouteConfig& route);

    void _validateConfig(const std::vector<ServerConfig>& servers);
    size_t _parseSize(const std::string& value);
};

#endif
