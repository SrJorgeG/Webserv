#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cerrno>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// Forward declarations de clases principales
class StringUtils;
class Logger;
class FileUtils;
class Request;
class Response;
class HttpParser;
class SessionManager;
class ServerConfig;
class RouteConfig;
class ConfigParser;
class Reactor;
class ServerSocket;
class Connection;
class CgiHandler;

// Macros utiles
#define DEFAULT_CONFIG_PATH "conf/default.conf"
#define DEFAULT_MAX_BODY_SIZE (1 * 1024 * 1024) // 1MB
#define CONNECTION_TIMEOUT 60 // segundos
#define KEEP_ALIVE_TIMEOUT 10 // segundos
#define CGI_TIMEOUT 30 // segundos
#define BUFFER_SIZE 8192
#define MAX_EVENTS 1024
#define BACKLOG 128
#define MAX_CONNECTIONS 1024

// Estados de conexion
enum ConnectionState {
    READING_HEADERS,
    READING_BODY,
    PROCESSING,
    CGI_WRITING_TO_STDIN,
    CGI_READING_FROM_STDOUT,
    WRITING_RESPONSE,
    CLOSING
};

// Resultados del parser
enum ParseResult {
    PARSE_OK,
    PARSE_INCOMPLETE,
    PARSE_ERROR
};

// Niveles de log
enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

#endif
