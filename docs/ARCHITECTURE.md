# Architecture

## The Reactor pattern

The Reactor is the single object that owns the `epoll` file descriptor and the map from fd to `EventHandler*`. Every file descriptor in the server — listening sockets, accepted client sockets, CGI stdin pipes, CGI stdout pipes — is registered here.

```cpp
// Reactor.cpp: main loop
void Reactor::run() {
    _running = true;
    struct epoll_event events[MAX_EVENTS];

    while (_running) {
        int nfds = epoll_wait(_epollFd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < nfds; ++i) {
            _dispatchEvent(events[i]);
        }
        _cleanupConnections();
    }
}
```

`epoll_wait` blocks for at most 1000 ms. The timeout is not 0 (which would spin-poll) and not -1 (which would block forever). 1000 ms gives the cleanup loop a chance to check CGI timeouts and connection idle timeouts once per second even with no incoming events.

### Dispatch: pointer in event.data

Each `epoll_event` stores a `void* data.ptr` pointing to the `EventHandler` object responsible for that fd:

```cpp
void Reactor::registerHandler(int fd, EventHandler* handler, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = handler;          // no fd lookup needed at dispatch time
    epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev);
    _handlers[fd] = handler;
}

void Reactor::_dispatchEvent(struct epoll_event& event) {
    EventHandler* handler = static_cast<EventHandler*>(event.data.ptr);
    if (!handler) return;

    // IMPORTANT: EPOLLIN must be checked before EPOLLHUP.
    // When a CGI pipe's write end closes, epoll fires EPOLLHUP | EPOLLIN
    // simultaneously if data is buffered. Reading must happen first or
    // the CGI output is discarded.
    if (event.events & EPOLLIN)             handler->handleRead();
    if (event.events & EPOLLOUT)            handler->handleWrite();
    if (event.events & (EPOLLERR | EPOLLHUP)) handler->handleError();
}
```

The EPOLLIN-before-EPOLLHUP ordering is not an arbitrary style choice. It fixes a real bug: when the CGI child process exits, the kernel closes the write end of the output pipe. The read end (registered in epoll) gets `EPOLLHUP | EPOLLIN` if there is still buffered output. If EPOLLHUP is handled first, `handleError` closes the connection before `_processCgiRead` has consumed the data.

### The EventHandler interface

```cpp
class EventHandler {
public:
    virtual ~EventHandler() {}
    virtual void handleRead()  = 0;
    virtual void handleWrite() = 0;
    virtual void handleError() = 0;
    virtual int getFd() const  = 0;
};
```

Two concrete implementations: `ServerSocket` (handles `accept`) and `Connection` (handles client I/O and CGI pipe I/O — the same object registers for all three).

## Connection lifecycle

A `Connection` is a state machine. The state drives which code path executes when `epoll` fires on the client fd or either CGI pipe fd.

```
                      recv() returns 0
                      (client disconnected)
                              │
READING_HEADERS ──────────────┼──────────────► CLOSING
       │                      │
       │  headers complete     │
       ▼                      │
READING_BODY ─────────────────┘
       │
       │  body complete (PARSE_OK)
       ▼
   PROCESSING ────── no CGI ──────────────────► WRITING_RESPONSE
       │                                               │
       │  CGI, POST body                               │  all bytes sent, keep-alive
       ▼                                               ▼
CGI_WRITING_TO_STDIN ──── body written ──► CGI_READING_FROM_STDOUT  READING_HEADERS
                                                  │
                                                  │  read() == 0 (EOF on pipe)
                                                  ▼
                                           WRITING_RESPONSE
```

The state is stored as a `ConnectionState` enum in `Connection`. The Reactor never reads this state directly — it calls virtual methods on the `EventHandler` interface. The Connection's `handleRead` and `handleWrite` branch on state internally:

```cpp
void Connection::handleRead() {
    _isKeepAliveIdle = false;
    updateLastActivity();
    if ((_state == CGI_READING_FROM_STDOUT || _state == CGI_WRITING_TO_STDIN) && _cgiHandler) {
        _processCgiRead();    // reading from CGI pipe, not from client socket
    } else {
        _processRead();       // reading HTTP request from client
    }
}
```

The same `Connection` object is registered for the CGI pipe fds. When `epoll` fires on a pipe fd, `event.data.ptr` points to the same `Connection` instance, so `handleRead` is called on it. The state check at the top of `handleRead` routes to `_processCgiRead` instead of `_processRead`. The Connection knows which fd is which through `_cgiInputFd` and `_cgiOutputFd`.

## Non-blocking I/O

Every fd is set non-blocking before registration with epoll.

Client sockets: `accept` returns them blocking by default. `ServerSocket::acceptConnection` calls `fcntl(fd, F_SETFL, O_NONBLOCK)` immediately after `accept`.

CGI pipes: Created with `pipe()`, which creates blocking fds. After `fork`, the parent calls:

```cpp
fcntl(_inputPipe[1], F_SETFL, O_NONBLOCK);   // stdin write end
fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK);  // stdout read end
```

Note: `F_GETFL` followed by `F_SETFL` is the portable way to preserve existing flags, but the subject prohibits using `F_GETFL` on some platforms. Since these are fresh pipes with no pre-existing flags of interest, `F_SETFL` with `O_NONBLOCK` alone is safe.

`FD_CLOEXEC` is set on the parent's pipe ends so they are not inherited by subsequent `fork` calls for other CGI requests:

```cpp
fcntl(_inputPipe[1], F_SETFD, FD_CLOEXEC);
fcntl(_outputPipe[0], F_SETFD, FD_CLOEXEC);
```

### Why errno is not checked after recv/write

The subject explicitly requires that `errno` not be checked after non-blocking I/O. The implementation follows this:

```cpp
// Connection.cpp _processRead()
ssize_t bytesRead = recv(_clientFd, buffer, BUFFER_SIZE - 1, 0);

if (bytesRead < 0) {
    // On non-blocking sockets, recv() returning -1 can mean either a real error
    // or EAGAIN/EWOULDBLOCK (socket would block). We don't check errno per the
    // subject requirement. Instead, we simply return and let epoll re-notify
    // us when data is available. If it was a real error, the next epoll event
    // will report EPOLLERR/EPOLLHUP and trigger handleError().
    return;
}
```

This is not a simplification that loses correctness. EAGAIN means "no data right now, try again later" — returning from `handleRead` and letting epoll re-notify is exactly the right response. A genuine socket error will produce EPOLLERR on the next `epoll_wait`, routing to `handleError` which sets state to CLOSING.

## HTTP parsing

`HttpParser` is a stateful incremental parser. It receives whatever bytes arrived in a single `recv` call and returns one of three outcomes: `PARSE_OK`, `PARSE_INCOMPLETE`, `PARSE_ERROR`.

```
raw TCP bytes arriving in chunks:
  "GET /index.ht"   → PARSE_INCOMPLETE (request line not complete)
  "ml HTTP/1.1\r\n" → PARSE_INCOMPLETE (headers not complete)
  "Host: localh"    → PARSE_INCOMPLETE
  "ost\r\n\r\n"     → PARSE_OK
```

The parser maintains an internal `_buffer`. Each call to `parse` appends the new data and attempts to advance through the request line, then headers, then body. State persists between calls: `_requestLineComplete` and `_headersComplete` flags prevent re-parsing already-consumed sections.

After a successful parse, the Connection saves the leftover bytes (data that arrived beyond the end of this request) via `_parser.getLeftoverData()`. This handles pipelining: a client may send two requests back-to-back in the same TCP segment. The leftover is preserved in `_readBuffer` after the parser is reset, so the next request starts processing immediately without waiting for another `recv`.

Chunked transfer encoding is decoded inside the parser. Each chunk's hex size line is parsed, the data extracted, and appended to the request body. The terminating zero-length chunk triggers `PARSE_OK`. The decoded body — not the raw chunked wire format — is what CGI and handlers receive.

## Path resolution and security

`StringUtils::resolvePath` is the single function that maps a request URI to a filesystem path:

```cpp
std::string StringUtils::resolvePath(const std::string& uri,
                                     const std::string& routePath,
                                     const std::string& root) {
    std::string path = stripQueryString(uri);    // remove ?query before anything else

    if (routePath != "/" && startsWith(path, routePath)) {
        if (path.size() > routePath.size()) {
            path = path.substr(routePath.size()); // strip location prefix
        }
        // exact match: path.size() == routePath.size(), keep as-is
    }

    if (path.empty() || path[0] != '/') {
        path = "/" + path;
    }

    std::string fullPath = FileUtils::joinPath(root, path);
    std::string normalizedPath = FileUtils::normalizePath(fullPath);

    if (!startsWith(normalizedPath, root)) {
        return "";          // path traversal attempt — return empty to signal 403
    }

    return normalizedPath;
}
```

The key invariant: normalization must happen before the `startsWith(normalizedPath, root)` check, not after. If the check were applied to the un-normalized path, a URI like `/uploads/../etc/passwd` would pass the prefix check (`startsWith("/uploads/../etc/passwd", "/uploads")` is true) but resolve outside the root after normalization. By normalizing first, the check is applied to the actual filesystem path.

The query string (`?key=value`) is stripped before any path operation. This is not optional: if the URI contains a `?`, the query becomes part of the filename being looked up, and filesystem lookups fail silently or return wrong results.

The function returns an empty string for path-traversal attempts. Every call site checks for empty and returns 403.

## CGI integration in the event loop

CGI uses two pairs of pipes per request:

```
parent (server)                    child (CGI script)
                                   
_inputPipe[1]  ──── write ──────►  _inputPipe[0]  (stdin)
_outputPipe[0] ◄─── read  ──────   _outputPipe[1] (stdout + stderr)
```

After `fork`, the parent closes the child's ends (`_inputPipe[0]`, `_outputPipe[1]`). The child closes the parent's ends and `dup2`s its ends to STDIN/STDOUT before `execve`.

The parent registers the pipe fds in epoll immediately after fork:

```cpp
// Connection.cpp _processRequest() — CGI branch
if (_request.getMethod() == "POST" && !_request.getBody().empty()) {
    _state = CGI_WRITING_TO_STDIN;
    _reactor.registerHandler(_cgiInputFd, this, EPOLLOUT);   // write body to CGI stdin
    _reactor.registerHandler(_cgiOutputFd, this, EPOLLIN);   // read CGI stdout
} else {
    _state = CGI_READING_FROM_STDOUT;
    close(_cgiInputFd); _cgiInputFd = -1;                   // no body to send
    _reactor.registerHandler(_cgiOutputFd, this, EPOLLIN);
}
```

The `this` pointer in `registerHandler` means the same `Connection` handles events on the pipe fds. When epoll fires on `_cgiOutputFd` with EPOLLIN, the dispatch calls `handler->handleRead()` on the Connection, which branches to `_processCgiRead`.

Reading from the CGI output pipe uses a drain loop:

```cpp
void Connection::_processCgiRead() {
    char buffer[BUFFER_SIZE];
    while (true) {
        ssize_t n = _cgiHandler->readOutputChunk(buffer, sizeof(buffer));
        if (n < 0) break;    // EAGAIN: no more data right now
        if (n > 0) continue; // data appended to _outputBuffer, keep draining

        // n == 0: EOF — CGI exited and closed its stdout
        // unregister pipes, call finishOutputRead, build HTTP response
        ...
        break;
    }
}
```

Draining until EAGAIN in a single epoll notification avoids re-entering the event loop unnecessarily for large CGI outputs. The loop exits on EAGAIN (not an error, just "buffer empty for now") or on EOF (n==0), which signals the CGI process has exited.

## Cleanup loop

After every `epoll_wait` batch, `_cleanupConnections` runs:

```cpp
void Reactor::_cleanupConnections() {
    std::vector<int> toRemove;
    for (...) {
        Connection* conn = dynamic_cast<Connection*>(it->second);
        if (conn) {
            CgiHandler* cgi = conn->getCgiHandler();
            if (cgi) cgi->checkTimeout();           // kills CGI after 30s

            if (conn->isTimedOut() || conn->getFd() < 0 || conn->getState() == CLOSING) {
                toRemove.push_back(it->first);
            }
        }
    }
    for (...) {
        delete _handlers[fd];       // destructor closes fd, unregisters CGI pipes
        removeHandler(fd);          // epoll_ctl DEL + erase from map
    }
}
```

Connections are not deleted during dispatch — only marked CLOSING. Deletion happens here, after the dispatch loop completes, avoiding iterator invalidation and use-after-free on the handler pointer.

## Virtual host matching

The Host header is parsed in `Connection::_matchVirtualHost` after the request is fully received. The Reactor's `matchVirtualHost` does two passes:

1. Exact match: same port and same `server_name` as Host header value.
2. Port-only match: any server listening on the same port.

Fallback is the first config entry. This matches NGINX's behavior for the default server on a port.

## Session management

Every response sets a `webserv_session_id` cookie. The `SessionManager` is a singleton that maps session IDs to `std::map<std::string,std::string>` data stores. Session creation generates a UUID-like ID. Sessions expire after 300 seconds of inactivity; expired sessions are purged every 300 `epoll_wait` iterations (approximately every 5 minutes at idle).

Sessions are not persisted to disk and are lost on server restart. This is intentional for a 42 project.

## Component map

```
main.cpp
  └── ConfigParser::parse()        → vector<ServerConfig>
  └── Reactor::init()              → create epoll, bind ServerSockets
  └── Reactor::run()               → event loop

Reactor
  ├── epoll_wait()
  ├── _dispatchEvent()             → calls EventHandler virtual methods
  ├── _cleanupConnections()        → timeout + CLOSING state cleanup
  ├── matchVirtualHost()           → Host header → ServerConfig*
  └── std::map<int,EventHandler*>  _handlers

ServerSocket : EventHandler
  ├── bindAndListen()              → socket + bind + listen + fcntl O_NONBLOCK
  └── handleRead()                 → accept() → Reactor::addConnection()

Connection : EventHandler
  ├── _processRead()               → recv → HttpParser → _processRequest()
  ├── _processWrite()              → send → keep-alive or CLOSING
  ├── _processCgiWrite()           → write request body to CGI stdin pipe
  ├── _processCgiRead()            → read CGI stdout pipe until EAGAIN or EOF
  ├── _processRequest()            → route matching → method dispatch or CGI
  └── _matchVirtualHost()          → update _serverConfig per Host header

HttpParser
  └── parse()                      → PARSE_OK / PARSE_INCOMPLETE / PARSE_ERROR
      ├── _parseRequestLine()
      ├── _parseHeaders()
      └── _parseChunkedBody()

GetHandler     → static file, directory listing, 301 redirect
PostHandler    → multipart/form-data extraction, file write
DeleteHandler  → path resolution, permission check, unlink

CgiHandler
  ├── start()         → setupPipes + setupEnvironment + forkAndExecute
  ├── writeBodyChunk() → write to _inputPipe[1] (non-blocking)
  ├── readOutputChunk() → read from _outputPipe[0] (non-blocking)
  ├── finishOutputRead() → waitpid + _parseCgiOutput → Response
  └── checkTimeout()  → kill after CGI_TIMEOUT seconds

StringUtils::resolvePath()
  └── stripQueryString → strip route prefix → joinPath → normalizePath → startsWith check
```
