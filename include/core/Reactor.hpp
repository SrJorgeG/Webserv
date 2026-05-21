#ifndef REACTOR_HPP
#define REACTOR_HPP

#include "Webserv.hpp"
#include "core/EventHandler.hpp"
#include "config/ServerConfig.hpp"
#include <vector>
#include <map>

class Reactor {
public:
    Reactor();
    ~Reactor();

    bool init(const std::vector<ServerConfig>& servers);
    void run();
    void stop();

    void registerHandler(int fd, EventHandler* handler, uint32_t events);
    void modifyHandler(int fd, uint32_t events);
    void removeHandler(int fd);

    void addConnection(int clientFd, const ServerConfig& config);

    const ServerConfig& matchVirtualHost(int port, const std::string& hostName) const;

    bool isRunning() const;

private:
    Reactor(const Reactor& other);
    Reactor& operator=(const Reactor& other);

    int _epollFd;
    bool _running;
    std::map<int, EventHandler*> _handlers;
    std::vector<ServerConfig> _configs;

    void _dispatchEvent(struct epoll_event& event);
    void _cleanupConnections();
};

#endif
