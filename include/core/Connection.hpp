#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "Webserv.hpp"
#include "core/EventHandler.hpp"
#include "http/Request.hpp"
#include "http/Response.hpp"
#include "http/HttpParser.hpp"
#include "http/SessionManager.hpp"
#include "config/ServerConfig.hpp"
#include "config/RouteConfig.hpp"

class Reactor;

class Connection : public EventHandler {
public:
    Connection(int clientFd, const ServerConfig& serverConfig, Reactor& reactor);
    ~Connection();

    // EventHandler interface
    void handleRead();
    void handleWrite();
    void handleError();
    int getFd() const;

    void closeConnection();
    bool isTimedOut() const;
    void updateLastActivity();
    ConnectionState getState() const;
    CgiHandler* getCgiHandler() const;

    // CGI pipe handlers
    void handleCgiInputWrite();
    void handleCgiOutputRead();

private:
    Connection(const Connection& other);
    Connection& operator=(const Connection& other);

    int _clientFd;
    ConnectionState _state;
    const ServerConfig* _serverConfig;
    Reactor& _reactor;

    Request _request;
    Response _response;

    std::string _readBuffer;
    std::string _writeBuffer;
    size_t _bytesWritten;

    time_t _lastActivity;

    HttpParser _parser;
    bool _keepAlive;
    bool _isKeepAliveIdle;
    std::string _sessionId;

    // CGI handling
    CgiHandler* _cgiHandler;
    int _cgiInputFd;
    int _cgiOutputFd;

    void _processRead();
    void _processWrite();
    void _processRequest();
    void _processCgiWrite();
    void _processCgiRead();
    void _matchVirtualHost();
    void _buildErrorResponse(int statusCode);
    const RouteConfig* _findMatchingRoute(const std::string& uri) const;
    std::string _resolvePath(const Request& request, const RouteConfig& route);
    void _registerCgiPipes();
    void _unregisterCgiPipes();
};

#endif