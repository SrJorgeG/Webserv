#include "core/ServerSocket.hpp"
#include "core/Connection.hpp"
#include "core/Reactor.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"

ServerSocket::ServerSocket(const ServerConfig& config, size_t listenIndex, Reactor& reactor)
    : _fd(-1), _config(config), _reactor(reactor) {
    const std::vector<std::pair<std::string, int> >& listens = config.getListens();
    if (listenIndex < listens.size()) {
        _host = listens[listenIndex].first;
        _port = listens[listenIndex].second;
    }
}

ServerSocket::~ServerSocket() {
    if (_fd >= 0) {
        close(_fd);
    }
}

bool ServerSocket::bindAndListen() {
    _fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_fd < 0) {
        LOG_ERROR("socket() failed");
        return false;
    }

    int opt = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("setsockopt() failed");
        close(_fd);
        _fd = -1;
        return false;
    }

    if (fcntl(_fd, F_SETFL, O_NONBLOCK) < 0) {
        LOG_ERROR("fcntl() failed");
        close(_fd);
        _fd = -1;
        return false;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    if (_host.empty() || _host == "*" || _host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, _host.c_str(), &addr.sin_addr);
    }

    if (bind(_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind() failed on port " + StringUtils::intToString(_port));
        close(_fd);
        _fd = -1;
        return false;
    }

    if (listen(_fd, BACKLOG) < 0) {
        LOG_ERROR("listen() failed");
        close(_fd);
        _fd = -1;
        return false;
    }

    return true;
}

int ServerSocket::acceptConnection() {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    int clientFd = accept(_fd, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientFd < 0) {
        LOG_DEBUG("accept() returned -1 (EAGAIN expected on non-blocking socket)");
        return -1;
    }

    if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
        LOG_ERROR("fcntl() on client failed");
        close(clientFd);
        return -1;
    }

    // Set FD_CLOEXEC so client socket doesn't leak to CGI child processes
    if (fcntl(clientFd, F_SETFD, FD_CLOEXEC) < 0) {
        LOG_WARN("fcntl(FD_CLOEXEC) on client failed");
    }

    return clientFd;
}

void ServerSocket::handleRead() {
    while (true) {
        int clientFd = acceptConnection();
        if (clientFd < 0) {
            break;
        }
        _reactor.addConnection(clientFd, _config);
    }
}

void ServerSocket::handleWrite() {
    // Server sockets don't write
}

void ServerSocket::handleError() {
    LOG_ERROR("ServerSocket error on port " + StringUtils::intToString(_port));
}

int ServerSocket::getFd() const {
    return _fd;
}

const ServerConfig& ServerSocket::getConfig() const {
    return _config;
}

int ServerSocket::getPort() const {
    return _port;
}
