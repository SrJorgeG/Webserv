#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include "Webserv.hpp"
#include "config/RouteConfig.hpp"
#include <vector>
#include <map>

class ServerConfig {
public:
    ServerConfig();
    ~ServerConfig();
    ServerConfig(const ServerConfig& other);
    ServerConfig& operator=(const ServerConfig& other);

    // Getters
    const std::vector<std::pair<std::string, int> >& getListens() const;
    const std::string& getServerName() const;
    size_t getClientMaxBodySize() const;
    const std::map<int, std::string>& getErrorPages() const;
    const std::vector<RouteConfig>& getRoutes() const;

    // Setters
    void addListen(const std::string& host, int port);
    void setServerName(const std::string& name);
    void setClientMaxBodySize(size_t size);
    void addErrorPage(int code, const std::string& path);
    void addRoute(const RouteConfig& route);

    const RouteConfig* findRoute(const std::string& uri) const;
    std::string getErrorPage(int code) const;

private:
    std::vector<std::pair<std::string, int> > _listens;
    std::string _serverName;
    size_t _clientMaxBodySize;
    std::map<int, std::string> _errorPages;
    std::vector<RouteConfig> _routes;
};

#endif
