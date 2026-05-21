_This project has been created as part of the 42 curriculum by jgomez-d and dcid-san._

# webserv

An HTTP/1.1 server written in C++98. Single-threaded, non-blocking, event-driven via Linux `epoll`. Handles multiple concurrent connections with CGI support through `fork`/`execve` and non-blocking pipe I/O integrated into the same event loop.

## Feature status

| Feature | Status | Notes |
|---------|--------|-------|
| GET, POST, DELETE | done | per-location method enforcement, 405 on violation |
| Non-blocking I/O (epoll) | done | all fds set O_NONBLOCK; no blocking syscalls in the event loop |
| Configuration file | done | NGINX-style server/location blocks |
| Virtual hosting | done | Host header matching, fallback to first server on port |
| CGI (fork/execve) | done | Python, shell; PHP if php-cgi installed |
| File upload (multipart) | done | `upload_store` directive, saves to configurable dir |
| Directory listing (autoindex) | done | generated HTML table |
| Custom error pages | done | per-status-code file paths in config |
| HTTP redirects (301) | done | `redirect` directive; also auto-redirects dir URIs without trailing slash |
| Chunked request decoding | done | full chunked body reassembly before processing |
| Keep-alive / pipelining | done | leftover buffer preserved across requests on same connection |
| Session cookies | done | `webserv_session_id` cookie set on every response |
| Connection timeouts | done | 60 s active, 5 s keep-alive idle |
| CGI timeout | done | 30 s, SIGKILL + 504 |
| 413 Payload Too Large | done | checked on both Content-Length header and accumulated buffer |

## Architecture overview

```
                         ┌──────────────────────────────────────────────────────┐
                         │                      Reactor                         │
                         │  (single epoll_fd — all fds registered here)         │
                         │                                                      │
  accept()               │   epoll_wait(timeout=1000ms)                         │
 ──────────►  Server     │         │                                            │
             Socket ─────┼─► EPOLLIN ──► ServerSocket::handleRead()            │
                         │              └─ accept() → new Connection(fd)        │
                         │                 registerHandler(fd, conn, EPOLLIN)   │
                         │                                                      │
  client data            │   EPOLLIN  ──► Connection::handleRead()              │
 ──────────►  client fd ─┼─►             └─ _processRead() → parse → _processRequest() │
                         │                                                      │
  response ready         │   EPOLLOUT ──► Connection::handleWrite()             │
 ◄──────────  client fd ─┼─►             └─ _processWrite() → send()           │
                         │                                                      │
  CGI stdout             │   EPOLLIN  ──► Connection::handleRead()              │
 ──────────►  pipe[0] ───┼─►             └─ _processCgiRead() → collect output │
                         │                                                      │
  CGI stdin              │   EPOLLOUT ──► Connection::handleWrite()             │
 ◄──────────  pipe[1] ───┼─►             └─ _processCgiWrite() → write body    │
                         │                                                      │
                         │   _cleanupConnections() after every epoll_wait():    │
                         │     - CGI timeout check (checkTimeout)               │
                         │     - isTimedOut() → close stale connections         │
                         │     - CLOSING state → delete + removeHandler()       │
                         └──────────────────────────────────────────────────────┘
```

Every file descriptor — listening sockets, client sockets, CGI stdin pipes, CGI stdout pipes — is registered in the same `epoll` instance. `epoll_wait` is the only place the process sleeps.

## Design decisions

### Why epoll and not select or poll

`select` has a hard `FD_SETSIZE` limit (typically 1024) and requires rebuilding the fd set on every call. `poll` removes the size limit but still scans the entire array of fds on each call — O(n) per wakeup. `epoll` is O(1) per event: the kernel maintains the interest set, and `epoll_wait` returns only the fds that are ready. For a server that keeps long-lived CGI pipes open alongside hundreds of client connections, scanning every registered fd per loop iteration would be wasteful. `epoll` is also Linux-specific, which is a non-issue since the project runs on Linux.

The choice between `EPOLLET` (edge-triggered) and the default level-triggered mode matters. Edge-triggered fires once when a fd transitions from unready to ready, requiring the application to drain the fd completely or miss events. Level-triggered fires as long as the fd has data. This implementation uses level-triggered because it simplifies the read loop: if `recv` returns data, the connection advances; if it would block, we simply return and epoll re-notifies us on the next iteration.

### Why the Reactor pattern

The Reactor decouples event detection from event handling. `epoll_wait` returns a list of ready file descriptors; the Reactor looks up the `EventHandler*` stored in `event.data.ptr` and calls the appropriate virtual method (`handleRead`, `handleWrite`, `handleError`). Adding a new type of fd — for example, a CGI pipe — requires only implementing `EventHandler` and calling `registerHandler`. The Connection object already implements `EventHandler`, so it self-registers both its client socket and its CGI pipes under the same interface. The Reactor does not know whether it is dispatching to a server socket, a client connection, or a CGI pipe; the polymorphism handles it.

### Why C++98

The 42 subject mandates C++98 explicitly. Beyond compliance, the constraint is not harmful here: `epoll`, `fork`, `pipe`, `execve`, `fcntl`, and `waitpid` are all C POSIX APIs. The C++ layer provides RAII for file descriptors (closed in destructors), `std::string` for buffer management, and `std::map`/`std::vector` for configuration and handler lookup. None of these require post-98 features.

The main friction point is the absence of `std::to_string`, `nullptr`, range-based for, and lambdas. These are worked around: integer-to-string conversion uses `std::ostringstream`, NULL is used instead of `nullptr`, and loops use explicit iterators.

### Why fork/execve for CGI and not threads

The subject forbids threads. Even without that constraint, `fork`/`execve` is the correct model for CGI: the child process gets a clean environment, inherits only the file descriptors explicitly kept open, and is completely isolated — a misbehaving CGI script cannot corrupt server state. The pipe between parent and child is the only communication channel, and both ends are registered in `epoll`, so the server never blocks waiting for CGI output.

### Why not check errno after recv/write

The subject explicitly states that `errno` must not be checked after non-blocking I/O operations. The implementation follows this literally: if `recv` returns -1, `_processRead` simply returns. If the return value was EAGAIN/EWOULDBLOCK, `epoll` will re-fire EPOLLIN when data arrives again. If it was a genuine error, `epoll` will fire EPOLLERR or EPOLLHUP, which routes to `handleError` and sets state to CLOSING. The two cases resolve themselves without inspecting `errno`.

## Building

```
make          # compile with -Wall -Wextra -Werror -std=c++98
make re       # full rebuild
make clean    # remove object files
make fclean   # remove objects and binary
```

Requires: g++ or clang++ with C++98 support, Linux (epoll).

## Running

```bash
./webserv                          # uses conf/default.conf
./webserv conf/tests/multi_cgi.conf
./webserv /absolute/path/to/my.conf
```

The configuration path is relative to the working directory from which the binary is launched. Root-relative paths in `root` directives (e.g. `root www/default`) are also resolved relative to the working directory, not the config file location.

## Configuration example (annotated)

```nginx
# First server block — listens on port 8080
server {
    listen 8080;                        # "listen host:port" or just port; host defaults to 0.0.0.0
    listen 127.0.0.1:8081;             # same server config, second bind address
    server_name localhost;              # matched against Host header; empty matches anything on port

    client_max_body_size 10M;          # 413 if body exceeds this; accepts K, M, G suffixes

    error_page 404 /errors/404.html;   # served from location / root
    error_page 500 502 503 504 /errors/50x.html;

    # Static site root
    location / {
        root www/default;              # filesystem path relative to cwd
        index index.html;              # served when URI resolves to a directory
        autoindex off;                 # 403 if directory and no index found
        allow_methods GET POST;        # 405 for any other method
    }

    # Upload directory with autoindex
    location /uploads {
        root www/upload;
        allow_methods POST GET DELETE;
        upload_store www/upload;       # directory where POSTed files are saved
        autoindex on;                  # directory listing enabled
    }

    # Permanent redirect
    location /old {
        redirect /;                    # issues 301 with Location: /
        allow_methods GET;
    }

    # Python CGI
    location /cgi-bin {
        root cgi-bin;
        allow_methods GET POST;
        cgi_extension .py /usr/bin/python3;  # extension -> interpreter path
    }
}

# Second server block — different port, different content
server {
    listen 9090;
    server_name secondary.local;

    location / {
        root www/secondary;
        index index.html;
        allow_methods GET;
    }
}
```

## Testing with curl

### Static files

```bash
# Serve index
curl -v http://localhost:8080/

# 404
curl -v http://localhost:8080/nonexistent.html

# Directory without trailing slash — server issues 301
curl -v http://localhost:8080/uploads
# Follow redirect
curl -vL http://localhost:8080/uploads
```

### Method enforcement

```bash
# DELETE on a GET-only location -> 405
curl -v -X DELETE http://localhost:8080/

# 405 response includes Allow header
```

### File upload

```bash
# Multipart upload
curl -v -X POST -F "file=@/etc/hostname" http://localhost:8080/uploads/

# Verify file appeared
curl http://localhost:8080/uploads/

# Delete it
curl -v -X DELETE http://localhost:8080/uploads/hostname
```

### Body size limit

```bash
# Generate 20 MB body to trigger 413
dd if=/dev/zero bs=1M count=20 | curl -v -X POST \
  -H "Content-Type: application/octet-stream" \
  -H "Content-Length: 20971520" \
  --data-binary @- http://localhost:8080/uploads/
```

### CGI

```bash
# GET with query string
curl -v "http://localhost:8080/cgi-bin/hello.py?name=world"

# POST body to CGI
curl -v -X POST -d "key=value&other=123" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  http://localhost:8080/cgi-bin/echo.py

# Inspect environment variables the CGI receives
curl http://localhost:8080/cgi-bin/hello.py
```

### Chunked transfer encoding

```bash
curl -v -X POST \
  -H "Transfer-Encoding: chunked" \
  -H "Content-Type: text/plain" \
  --data-binary "hello chunked world" \
  http://localhost:8080/uploads/
```

### Virtual hosts

```bash
# Select second server by Host header
curl -v -H "Host: secondary.local" http://localhost:8080/
```

### Keep-alive pipelining

```bash
# Send two pipelined requests on one TCP connection
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
  | nc localhost 8080
```

### Concurrent connections

```bash
# 20 parallel requests using curl's built-in parallel mode (curl >= 7.66)
seq 20 | xargs -P 20 -I{} curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/
```

## Project structure

```
webserv/
├── include/
│   ├── core/         Reactor.hpp  Connection.hpp  ServerSocket.hpp  EventHandler.hpp
│   ├── http/         Request.hpp  Response.hpp  HttpParser.hpp  *Handler.hpp  SessionManager.hpp
│   ├── config/       ConfigParser.hpp  ServerConfig.hpp  RouteConfig.hpp
│   ├── cgi/          CgiHandler.hpp
│   └── utils/        Logger.hpp  StringUtils.hpp  FileUtils.hpp
├── src/              mirrors include/ structure
├── conf/
│   ├── default.conf
│   └── tests/        cgi.conf  multi_cgi.conf  multi_port.conf  simple.conf  upload.conf
├── www/default/      static site served by default config
├── cgi-bin/          hello.py  echo.py  session.py  env.sh  info.php
├── docs/             ARCHITECTURE.md  CGI.md  CONFIGURATION.md  TESTING.md
└── Makefile
```

## References

- RFC 7230 — HTTP/1.1 Message Syntax and Routing
- RFC 7231 — HTTP/1.1 Semantics and Content
- RFC 3875 — Common Gateway Interface 1.1
- RFC 6265 — HTTP State Management (cookies)
- RFC 7578 — multipart/form-data
- `epoll(7)`, `fork(2)`, `execve(2)`, `pipe(2)`, `fcntl(2)` — Linux man pages
