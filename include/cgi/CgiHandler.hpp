#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include "Webserv.hpp"
#include "http/Request.hpp"
#include "http/Response.hpp"
#include "config/RouteConfig.hpp"
#include "config/ServerConfig.hpp"

// CGI handler state machine
enum CgiState {
    CGI_IDLE = 0,
    CGI_RUNNING = 1,
    CGI_WRITING = 2,
    CGI_READING = 3,
    CGI_DONE = 4
};

class CgiHandler {
public:
    CgiHandler();
    ~CgiHandler();

    // Start CGI process - returns input pipe fd for stdin, output pipe fd for stdout
    // Caller registers these fds with epoll
    void start(const Request& request, const RouteConfig& route,
               const ServerConfig& server, Response& response);

    // Get file descriptors for epoll registration
    int getInputFd() const;
    int getOutputFd() const;

    // Get the handler state
    CgiState getState() const;
    pid_t getPid() const;
    time_t getStartTime() const;

    // Check if CGI is still running
    bool isRunning() const;

    // Write chunk to CGI stdin (called when epoll signals EPOLLOUT on input pipe)
    void writeBodyChunk(const std::string& body, size_t& bytesWritten);

    // Read chunk from CGI stdout (called when epoll signals EPOLLIN on output pipe)
    // Returns: >0 bytes read, 0 = EOF, -1 = EAGAIN (would block)
    ssize_t readOutputChunk(char* buffer, size_t size);

    // Called when all body data has been written - close stdin pipe
    void finishBodyWrite();

    // Called when output pipe returns 0 (EOF) - parse accumulated output
    void finishOutputRead(Response& response);

    // Check for timeout
    void checkTimeout();

    // Cleanup resources
    void cleanup();

private:
    CgiHandler(const CgiHandler& other);
    CgiHandler& operator=(const CgiHandler& other);

    pid_t _pid;
    int _inputPipe[2];   // Parent writes to child stdin: [0]=read end, [1]=write end
    int _outputPipe[2];  // Parent reads from child stdout: [0]=read end, [1]=write end
    time_t _startTime;
    CgiState _state;
    std::string _outputBuffer;
    std::string _bodyToWrite;
    size_t _bodyBytesWritten;

    void _setupPipes();
    void _closePipes();
    void _setupEnvironment(const Request& request, const RouteConfig& route,
                           const ServerConfig& server);
    void _forkAndExecute(const std::string& interpreter, const std::string& scriptPath);
    void _parseCgiOutput(Response& response);

    std::vector<std::string> _envVars;
    std::vector<char*> _envp;
};

#endif
