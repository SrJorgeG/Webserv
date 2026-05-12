#ifndef SERVER_SOCKET_HPP
#define SERVER_SOCKET_HPP

#include "Webserv.hpp"
#include "core/EventHandler.hpp"
#include "config/ServerConfig.hpp"

class Reactor;

class ServerSocket : public EventHandler {
public:
    ServerSocket(const ServerConfig& config, size_t listenIndex, Reactor& reactor);
    ~ServerSocket();

    bool bindAndListen();
    int acceptConnection();

    // EventHandler interface
    void handleRead();
    void handleWrite();
    void handleError();
    int getFd() const;

    const ServerConfig& getConfig() const;
    int getPort() const;

private:
    ServerSocket(const ServerSocket& other);
    ServerSocket& operator=(const ServerSocket& other);

    int _fd;
    int _port;
    std::string _host;
    const ServerConfig& _config;
    Reactor& _reactor;
};

#endif
