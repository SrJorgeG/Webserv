# Webserv

> An HTTP/1.1 web server in C++98 with epoll and CGI (42 Madrid, with alepinto and dcid-san).

## What it does
Single-threaded, non-blocking server handling many TCP connections through one `epoll` loop. Supports GET, POST, DELETE, HEAD and PUT, nginx-like config files, multiple ports, uploads and CGI (Python, shell, PHP) run as separate processes wired into the same loop via pipes.

## What I learned
- Sockets (`socket/bind/listen/accept`), `O_NONBLOCK`, reactor pattern
- HTTP parsing per RFC, CGI with `fork/execve` without blocking
- Config parsing and team architecture

## Tech stack
C++98, epoll, Python/PHP CGI

## Build & run
```bash
make
./webserv conf/default.conf
curl -i http://localhost:<port>/
```

## Structure
`src/` (`core/`, `http/`, `cgi/`, `config/`, `utils/`), `include/`, `conf/` (+ `conf/tests/`), `cgi-bin/`, `docs/` (architecture, CGI, testing).

---
*Built at 42 Madrid by jgomez-d (Jorge Gomez De Oliveira).*
