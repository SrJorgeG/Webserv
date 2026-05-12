_This project has been created as part of the 42 curriculum by jorgedebian._

# Webserv

A high-performance HTTP/1.1 web server implemented in C++98, inspired by NGINX architecture. This project implements a non-blocking, event-driven server using `epoll` for I/O multiplexing, with support for static file serving, CGI execution, file upload, and custom error pages.

## Features

### Mandatory
- **HTTP/1.1 compliant** — Supports GET, POST, and DELETE methods
- **Non-blocking I/O** — Event-driven architecture using `epoll` for handling multiple connections simultaneously
- **Configuration file** — NGINX-style configuration with server blocks and location directives
- **Virtual hosting** — Multiple server blocks with different host:port combinations
- **CGI execution** — Supports Python CGI scripts (and extensible to other interpreters)
- **File upload** — Multipart form-data handling with configurable upload directory
- **Directory listing** — Autoindex generation for directories without index files
- **Custom error pages** — Configurable per-status-code error page serving
- **HTTP redirection** — Configurable redirects via `redirect` directive
- **Chunked transfer encoding** — Full support for chunked request bodies
- **Connection timeouts** — Configurable keep-alive and connection timeouts

### Bonus
- **Session & Cookie management** — Built-in session tracking with cookie-based session IDs
- **Multiple CGI types** — Concurrent support for Python, PHP-CGI, and Shell CGI scripts
- **PHP-CGI support** — Dynamic content via PHP
- **Shell CGI scripts** — Server-side scripting using bash

## Architecture

```
┌──────────────────────────────────────┐
│              Reactor                 │
│         (epoll event loop)           │
├──────────┬───────────┬───────────────┤
│ Server   │ Connection│  Connection   │
│ Socket   │   (fd1)   │    (fd2)      │
│ (accept) │  (rw)     │    (rw)       │
└──────────┴───────────┴───────────────┘
```

The server uses the **Reactor pattern** with epoll for scalable I/O multiplexing. Each connection is managed by a `Connection` object that implements the `EventHandler` interface. CGI processes are integrated into the event loop through non-blocking pipe I/O.

## Configuration

The server reads a configuration file in NGINX-style format:

```nginx
server {
    listen 8080;
    server_name localhost;
    client_max_body_size 10M;

    error_page 404 /errors/404.html;

    location / {
        root www/default;
        index index.html;
        allow_methods GET POST;
    }

    location /uploads {
        root www/upload;
        allow_methods POST GET DELETE;
        upload_store www/upload;
        autoindex on;
    }

    location /cgi-bin {
        root cgi-bin;
        allow_methods GET POST;
        cgi_extension .py /usr/bin/python3;
    }
}
```

### Directives

| Directive | Context | Description |
|-----------|---------|-------------|
| `listen` | server | Host:port or just port to bind to |
| `server_name` | server | Server name for virtual hosting |
| `client_max_body_size` | server | Maximum request body size (supports K, M, G suffixes) |
| `error_page` | server | Custom error pages: `error_page 404 /errors/404.html` |
| `root` | location | Document root for the location |
| `index` | location | Default file to serve for directories |
| `autoindex` | location | Enable/disable directory listing (on/off) |
| `allow_methods` | location | Allowed HTTP methods |
| `redirect` | location | Redirect target URL |
| `upload_store` | location | Directory for file uploads |
| `cgi_extension` | location | Map file extension to CGI interpreter |

## Building

### Requirements
- C++ compiler with C++98 support (g++ or clang++)
- Make
- Linux (uses epoll)

### Compile

```bash
make
```

### Clean

```bash
make clean      # Remove object files
make fclean     # Remove objects and binary
make re         # Full rebuild
```

## Running

```bash
./webserv [configuration file]
```

If no configuration file is specified, the default `conf/default.conf` is used.

### Examples

```bash
# Run with default config
./webserv

# Run with custom config
./webserv conf/tests/multi_cgi.conf
```

## Testing

### Static Files
```bash
curl http://localhost:8080/
curl http://localhost:8080/index.html
```

### File Upload
```bash
curl -X POST -F "file=@test.txt" http://localhost:8080/uploads/
```

### CGI
```bash
curl http://localhost:8080/cgi-bin/hello.py
curl http://localhost:8080/cgi-bin/echo.py?name=test
```

### Error Pages
```bash
curl http://localhost:8080/nonexistent
```

## Project Structure

```
webserv/
├── include/           # Header files
│   ├── core/          # Reactor, Connection, ServerSocket
│   ├── http/          # Request, Response, Handlers, Parser
│   ├── config/        # ConfigParser, ServerConfig, RouteConfig
│   ├── cgi/           # CgiHandler
│   └── utils/         # Logger, StringUtils, FileUtils
├── src/               # Source files (mirrors include/)
├── conf/              # Configuration files
├── www/               # Static web content
│   └── default/       # Default site root
├── cgi-bin/           # CGI scripts
├── docs/              # Documentation
└── Makefile
```

## Resources

### References
- RFC 7230-7235 — HTTP/1.1 Message Syntax, Routing, Conditionals, Caching, Authentication, and Security
- RFC 3875 — The Common Gateway Interface (CGI) Version 1.1
- Linux `epoll(7)` man page — I/O multiplexing
- NGINX architecture documentation — Reactor pattern inspiration
- "UNIX Network Programming" by W. Richard Stevens — Socket programming reference

### AI Usage
AI tools (specifically Claude / OpenCode with Gentle AI SDD skills) were used during development for:
- Architecture design and code review discussions
- Debugging assistance for epoll event loop and CGI non-blocking I/O integration
- Generating boilerplate code for configuration parsing and HTTP response construction
- Code optimization suggestions and identifying potential issues in concurrent handling
- All AI-generated code was reviewed, understood, and tested before integration

## License

This project is part of the 42 school curriculum.