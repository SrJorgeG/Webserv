#ifndef ROUTE_CONFIG_HPP
#define ROUTE_CONFIG_HPP

#include "Webserv.hpp"
#include <vector>
#include <map>

class RouteConfig {
public:
    RouteConfig();
    ~RouteConfig();
    RouteConfig(const RouteConfig& other);
    RouteConfig& operator=(const RouteConfig& other);

    // Getters
    const std::string& getPath() const;
    const std::string& getRoot() const;
    const std::string& getIndex() const;
    bool getAutoindex() const;
    const std::vector<std::string>& getAllowedMethods() const;
    const std::string& getRedirect() const;
    const std::string& getUploadStore() const;
    const std::map<std::string, std::string>& getCgiHandlers() const;

    // Setters
    void setPath(const std::string& path);
    void setRoot(const std::string& root);
    void setIndex(const std::string& index);
    void setAutoindex(bool autoindex);
    void addAllowedMethod(const std::string& method);
    void setRedirect(const std::string& redirect);
    void setUploadStore(const std::string& path);
    void addCgiHandler(const std::string& extension, const std::string& interpreter);

    bool isMethodAllowed(const std::string& method) const;
    bool hasCgiHandler(const std::string& extension) const;
    std::string getCgiInterpreter(const std::string& extension) const;
    bool isUploadEnabled() const;

private:
    std::string _path;
    std::string _root;
    std::string _index;
    bool _autoindex;
    std::vector<std::string> _allowedMethods;
    std::string _redirect;
    std::string _uploadStore;
    std::map<std::string, std::string> _cgiHandlers;
};

#endif
