#include "core/Reactor.hpp"
#include "core/ServerSocket.hpp"
#include "core/Connection.hpp"
#include "utils/Logger.hpp"
#include "utils/StringUtils.hpp"
#include <cerrno>

Reactor::Reactor() : _epollFd(-1), _running(false) {}

Reactor::~Reactor() {
    stop();
    for (std::map<int, EventHandler*>::iterator it = _handlers.begin(); it != _handlers.end(); ++it) {
        delete it->second;
    }
    _handlers.clear();
    if (_epollFd >= 0) {
        close(_epollFd);
    }
}

bool Reactor::init(const std::vector<ServerConfig>& servers) {
    _epollFd = epoll_create(1);
    if (_epollFd < 0) {
        LOG_ERROR("epoll_create failed");
        return false;
    }

    for (size_t i = 0; i < servers.size(); ++i) {
        const std::vector<std::pair<std::string, int> >& listens = servers[i].getListens();
        for (size_t j = 0; j < listens.size(); ++j) {
            ServerSocket* serverSocket = new ServerSocket(servers[i], j, *this);
            if (!serverSocket->bindAndListen()) {
                delete serverSocket;
                return false;
            }
            registerHandler(serverSocket->getFd(), serverSocket, EPOLLIN);
            LOG_INFO("Server listening on " + listens[j].first + ":" + StringUtils::intToString(listens[j].second));
        }
    }
    return true;
}

void Reactor::run() {
    _running = true;
    struct epoll_event events[MAX_EVENTS];

    while (_running) {
        int nfds = epoll_wait(_epollFd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll_wait failed");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            _dispatchEvent(events[i]);
        }

        _cleanupConnections();
    }
}

void Reactor::stop() {
    _running = false;
}

void Reactor::registerHandler(int fd, EventHandler* handler, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = handler;
    if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl ADD failed");
        return;
    }
    _handlers[fd] = handler;
}

void Reactor::modifyHandler(int fd, uint32_t events) {
    EventHandler* handler = _handlers[fd];
    if (!handler) return;

    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = handler;
    if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl MOD failed");
    }
}

void Reactor::modifyHandlerEvents(int fd, uint32_t events) {
    // Same as modifyHandler - kept for API clarity
    modifyHandler(fd, events);
}

void Reactor::removeHandler(int fd) {
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, NULL);
    _handlers.erase(fd);
}

void Reactor::addConnection(int clientFd, const ServerConfig& config) {
    if (_handlers.size() >= static_cast<size_t>(MAX_CONNECTIONS)) {
        LOG_WARN("Connection limit reached, rejecting connection");
        close(clientFd);
        return;
    }
    Connection* conn = new Connection(clientFd, config, *this);
    registerHandler(clientFd, conn, EPOLLIN);
    LOG_INFO("New connection on fd " + StringUtils::intToString(clientFd));
}

bool Reactor::isRunning() const {
    return _running;
}

void Reactor::_dispatchEvent(struct epoll_event& event) {
    EventHandler* handler = static_cast<EventHandler*>(event.data.ptr);
    if (!handler) return;

    if (event.events & EPOLLERR || event.events & EPOLLHUP) {
        handler->handleError();
    } else {
        if (event.events & EPOLLIN) {
            handler->handleRead();
        }
        if (event.events & EPOLLOUT) {
            handler->handleWrite();
        }
    }
}

void Reactor::_cleanupConnections() {
    std::vector<int> toRemove;
    for (std::map<int, EventHandler*>::iterator it = _handlers.begin(); it != _handlers.end(); ++it) {
        Connection* conn = dynamic_cast<Connection*>(it->second);
        if (conn) {
            if (conn->isTimedOut() || conn->getFd() < 0) {
                toRemove.push_back(it->first);
            }
        }
    }
    for (size_t i = 0; i < toRemove.size(); ++i) {
        int fd = toRemove[i];
        delete _handlers[fd];
        removeHandler(fd);  // Does epoll_ctl(DEL) + erase from map; dtor already closed fd
    }
}
