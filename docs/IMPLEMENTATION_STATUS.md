# Webserv - Implementation Status & Gap Analysis

> Analysis date: 2026-05-12 (v2 — full audit with sub-agent cross-verification)  
> Subject version: 24

---

## Table of Contents

1. [General Rules (Chapter II)](#1-general-rules-chapter-ii)
2. [Mandatory Requirements (Chapter IV.1)](#2-mandatory-requirements-chapter-iv1)
3. [MacOS Compatibility (Chapter IV.2)](#3-macos-compatibility-chapter-iv2)
4. [Configuration File (Chapter IV.3)](#4-configuration-file-chapter-iv3)
5. [CGI Implementation](#5-cgi-implementation)
6. [Bonus Part (Chapter VI)](#6-bonus-part-chapter-vi)
7. [README Requirements (Chapter V)](#7-readme-requirements-chapter-v)
8. [Summary: All Gaps](#8-summary-all-gaps)
9. [Priority Action Plan](#9-priority-action-plan)

---

## 1. General Rules (Chapter II)

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| 1 | No crashes under any circumstances | **WARN** | `try/catch` in `main.cpp:23-42` and `CgiHandler::start()` (`CgiHandler.cpp:33-98`). `signal(SIGPIPE, SIG_IGN)` in `main.cpp:8`. However: unbounded buffers (`Connection._readBuffer`, `HttpParser._buffer`, `CgiHandler._outputBuffer`) can cause OOM; `new` allocations (`Reactor.cpp:31,106`) throw `std::bad_alloc`; `Connection` objects in CLOSING state leak until timeout (60s) under error bursts. |
| 2 | Makefile with `$(NAME)`, `all`, `clean`, `fclean`, `re` | **FAIL** | All rules present (`Makefile:31,33,46,49,52`), but `.PHONY` is missing `directories`. Since `directories` is a prerequisite of `$(NAME)` and never creates a file named `directories`, Make considers it out of date on every invocation, causing **unnecessary relinking** every time `make` is called. Fix: add `directories` to `.PHONY`. |
| 3 | `-Wall -Wextra -Werror` | **PASS** | `Makefile:4`: `CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -I./include -MMD -MP` |
| 4 | C++98 standard compliance | **PASS** | `-std=c++98` present. No C++11+ features detected (no `auto`, `nullptr`, range-for, lambdas, etc.). `NULL` used consistently instead of `nullptr`. |
| 5 | Prefer C++ headers over C headers | **WARN** | All C++ style headers used (`<cstdlib>`, `<cstring>`, `<ctime>`, `<cerrno>`, `<cctype>`, `<cstdio>`). However: `StringUtils.cpp:112` uses `std::snprintf` which is C99, not C++98 (formally adopted in C++11). Compiles on most compilers but technically non-conforming. Also, `Reactor.hpp:19` uses `uint32_t` from `<stdint.h>` (C99) without explicit include — relies on transitive inclusion via `<sys/epoll.h>`. |
| 6 | No external/Boost libraries | **PASS** | Only standard library and POSIX system calls used. |

---

## 2. Mandatory Requirements (Chapter IV.1)

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| 1 | Config file as argument (with default) | **PASS** | `main.cpp:10-18`: accepts `[configuration file]` argument, defaults to `DEFAULT_CONFIG_PATH` ("conf/default.conf"). |
| 2 | Cannot execve another web server | **PASS** | `execve` only called in `CgiHandler.cpp:322` for CGI execution. No other `execve`/`system`/`popen` calls. |
| 3 | Non-blocking at all times | **PASS** | All sockets set to `O_NONBLOCK` (`ServerSocket.cpp:37,82`). CGI pipes set to `O_NONBLOCK` (`CgiHandler.cpp:336-343`). Epoll drives all I/O. |
| 4 | Single poll() equivalent for ALL I/O | **PASS** | Single `epoll` instance in `Reactor::_epollFd`. Server sockets and client connections all registered on it (`Reactor.cpp:36,107`). CGI pipes also registered (`Connection.cpp:209,212,221,222`). |
| 5 | Monitors both read AND write | **PASS** | Connections registered with `EPOLLIN`, switched to `EPOLLIN | EPOLLOUT` when ready to write (`Connection.cpp:196,245,303,348`). CGI input pipe: `EPOLLOUT` (`Connection.cpp:209`). CGI output pipe: `EPOLLIN` (`Connection.cpp:212,222`). |
| 6 | No read/write without poll() readiness | **PASS** | `recv()` only in `Connection::_processRead()` called from `handleRead()` (epoll EPOLLIN). `send()` only in `Connection::_processWrite()` called from `handleWrite()` (epoll EPOLLOUT). CGI pipe I/O also driven by epoll events (`Connection::handleCgiInputWrite()`, `handleCgiOutputRead()`). |
| 7 | No errno after read/write | **PASS** | `Connection.cpp:71`: `bytesRead < 0` → `handleError()` — no errno check. `Connection.cpp:128`: `bytesSent < 0` → `handleError()` — no errno check. `CgiHandler.cpp:131-138`: `write()` return checked directly — no errno. `CgiHandler.cpp:146-150`: `read()` return checked directly — no errno. |
| 8 | Disk files exempt from poll() | **PASS** | `GetHandler::_serveFile()` calls `FileUtils::readFile()` (synchronous `std::ifstream`). `Response::buildError()` calls `FileUtils::readFile()`. `PostHandler` uses `FileUtils::writeFile()`. None go through epoll. |
| 9 | No infinite hangs (timeouts) | **PASS** | `CONNECTION_TIMEOUT=60s`, `KEEP_ALIVE_TIMEOUT=10s`, `CGI_TIMEOUT=30s` (`Webserv.hpp:50-52`). `Connection::isTimedOut()` checked in `Reactor::_cleanupConnections()`. CGI timeout in `CgiHandler::checkTimeout()`. |
| 10 | Browser compatible | **PASS** | `Response::toString()` outputs proper `HTTP/1.1` with CRLF, `Date` header (line 96-100), `Server` header (line 104-106), `Content-Length`, `Set-Cookie`. |
| 11 | Accurate HTTP status codes | **PASS** | `StatusCodes` has: 100, 200, 201, 204, 301, 302, 400, 401, 403, 404, 405, 408, 411, 413, 414, 415, 500, 501, 502, 503, 504. Common codes are covered. |
| 12 | Default error pages | **PASS** | `Response::_getDefaultErrorPage()` generates HTML error pages. Custom pages loaded from disk in `Response::buildError()`. |
| 13 | Custom error pages served from file | **PASS** | `Response::buildError()` (lines 122-158): looks up custom error page, constructs full path, calls `FileUtils::readFile(fullPath)`, sets Content-Type based on extension. Falls back to default on failure. |
| 14 | fork only for CGI | **PASS** | Only `fork()` in `CgiHandler.cpp:289`. No other fork calls. |
| 15 | Serve fully static website | **PASS** | `GetHandler` serves files and directories with autoindex. `www/default/index.html` exists. |
| 16 | File upload | **PASS** | `PostHandler` handles multipart/form-data, writes to `upload_store` directory. |
| 17 | GET, POST, DELETE | **PASS** | All three dispatched in `Connection.cpp:228-240`. Returns 405 for unsupported methods. |
| 18 | Stress test resilience | **WARN** | `MAX_CONNECTIONS=1024` limits connections (`Reactor.cpp:101`). However: CLOSING-state connections linger until timeout (60s) rather than being cleaned immediately. `_cleanupConnections()` only checks `isTimedOut()` and `getFd() < 0`, not `_state == CLOSING`. Under a burst of client disconnections, this wastes connection slots. |
| 19 | Multiple ports | **PASS** | `Reactor::init()` iterates over all server blocks and listen directives (`Reactor.cpp:28-39`). `conf/tests/multi_port.conf` demonstrates this. |
| 20 | Multiple server blocks (virtual hosts) | **PARTIAL** | `server_name` is parsed and stored, but there's no logic to match incoming requests by `Host` header to different `ServerConfig` objects. All connections on the same port use the same config regardless of the `Host` header. |
| 21 | client_max_body_size enforcement | **PASS** | `Connection.cpp:95-108`: checks both `Content-Length` header and actual body size against `_serverConfig.getClientMaxBodySize()`. Returns 413 if exceeded. |

---

## 3. MacOS Compatibility (Chapter IV.2)

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| 1 | `fcntl()` only with `F_SETFL`, `O_NONBLOCK`, `FD_CLOEXEC` | **FAIL** | `CgiHandler.cpp:337`: `fcntl(_inputPipe[1], F_GETFL, 0)` — **F_GETFL is used but NOT in the permitted set**. Same at line 341 for output pipe. The subject explicitly limits macOS fcntl() to `F_SETFL`, `O_NONBLOCK`, and `FD_CLOEXEC`. `F_GETFL` is not in that list. |
| 2 | `FD_CLOEXEC` on file descriptors | **FAIL** | `FD_CLOEXEC` is **never used** anywhere in the codebase. `pipe()` is called without `O_CLOEXEC` (`CgiHandler.cpp:196`). The client socket, epoll fd, and other connection file descriptors leak into every CGI child process. The subject lists `FD_CLOEXEC` as an allowed flag, and it's important for preventing fd leaks in forked CGI processes. |

---

## 4. Configuration File (Chapter IV.3)

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| 1 | Multiple interface:port pairs | **PASS** | `ServerConfig._listens` is a `vector<pair<string,int>>`. `ConfigParser::_parseListenDirective()` supports both `host:port` and bare `port`. `Reactor::init()` binds all. |
| 2 | Default error pages | **PASS** | `error_page 404 /errors/404.html;` syntax parsed. `Response::buildError()` loads custom pages from disk (`Response.cpp:122-158`). Custom pages exist: `www/default/errors/404.html`, `www/default/errors/50x.html`. |
| 3 | Max body size | **PASS** | `client_max_body_size 10M;` parsed with K/M/G suffixes. Enforced in `Connection.cpp:95-108`. |
| 4a | Accepted HTTP methods per route | **CRITICAL** | Parsed correctly by `ConfigParser::_parseAllowMethodsDirective()`. `RouteConfig::isMethodAllowed()` implemented. **BUT**: `Connection::_processRequest()` **NEVER calls `route->isMethodAllowed()`**. It unconditionally dispatches GET/POST/DELETE regardless of the config. The CGI path also skips the method check. The `allow_methods` directive is parsed but **completely ignored at runtime**. |
| 4b | HTTP redirection | **PASS** | `redirect /;` parsed. `Connection.cpp:172-181` returns 301 with `Location` header. Status code is hardcoded to 301. |
| 4c | Root directory | **PASS** | `root www/default;` parsed. `GetHandler::_resolvePath()` and `DeleteHandler` use it with path-traversal protection (`FileUtils::normalizePath()`). |
| 4d | Directory listing (autoindex) | **PASS** | `autoindex on/off;` parsed. `GetHandler::_serveDirectory()` checks flag and generates HTML listing. |
| 4e | Default file (index) | **PASS** | `index index.html;` parsed. `GetHandler::_serveDirectory()` serves index file if it exists. |
| 4f | Upload authorization & location | **PASS** | `upload_store www/upload;` parsed. `PostHandler::handle()` checks `route.isUploadEnabled()`. |
| 4g | CGI by extension | **PASS** | `cgi_extension .py /usr/bin/python3;` maps extension to interpreter. Multiple extensions supported per route via `RouteConfig._cgiHandlers` map. |

---

## 5. CGI Implementation

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| 1 | CGI env vars (RFC 3875) | **WARN** | Present: `GATEWAY_INTERFACE`, `SERVER_PROTOCOL`, `SERVER_SOFTWARE`, `REQUEST_METHOD`, `SCRIPT_NAME`, `QUERY_STRING`, `PATH_INFO` (empty), `PATH_TRANSLATED`, `CONTENT_LENGTH`, `CONTENT_TYPE`, `SERVER_NAME`, `SERVER_PORT`, `REMOTE_ADDR` (hardcoded 127.0.0.1), `REMOTE_HOST` (hardcoded), `REDIRECT_STATUS`, all `HTTP_*` headers. **Missing**: `AUTH_TYPE`, `REMOTE_USER`, `SERVER_ADDR`. **Issue**: `SCRIPT_NAME` includes query string — should not per RFC 3875. |
| 2 | Chunked request un-chunking | **PASS** | `HttpParser::_parseChunkedBody()` correctly decodes chunked transfer encoding (`HttpParser.cpp:133-199`). |
| 3 | CGI output EOF handling | **PASS** | `CgiHandler::readOutputChunk()` returns 0 on EOF. `Connection::_processCgiRead()` detects EOF and calls `finishOutputRead()`. CGI output is accumulated in `_outputBuffer` and parsed without requiring `Content-Length`. |
| 4 | CGI runs in correct directory | **PASS** | `CgiHandler.cpp:308-313`: `chdir(scriptDir.c_str())` before `execve()`. |
| 5 | At least one CGI supported | **PASS** | Python3 CGI works. Scripts: `hello.py`, `echo.py`, `session.py`. |
| 6 | Multiple CGI types | **PASS** | `cgi_extension` directive supports `.py`, `.php`, `.sh`. Config `conf/tests/multi_cgi.conf` maps all three. Scripts exist: `hello.py`, `info.php`, `env.sh`. |
| 7 | Proper fork/execve pattern | **WARN** | Pattern is mostly correct: `pipe()`, `fork()`, child does `dup2()`, `close()`, `chdir()`, `execve()`. **Issues**: (a) **FD_CLOEXEC never set** — client socket, epoll fd, and other connection fds leak to CGI child. (b) `F_GETFL` used in parent violates MacOS restriction. |
| 8 | CGI timeout | **WARN** | `CgiHandler::checkTimeout()` exists (`CgiHandler.cpp:186-193`). `CGI_TIMEOUT=30s`. **BUT**: `checkTimeout()` is **never called anywhere** in the codebase. The only timeout protection comes indirectly from `Connection::isTimedOut()` (60s). |
| 9 | POST data piping to CGI | **PASS** | Body stored in `_bodyToWrite`, written asynchronously via `CgiHandler::writeBodyChunk()` when `EPOLLOUT` fires on input pipe (`Connection.cpp:248-268`). |
| 10 | CGI response parsing | **PASS** | `CgiHandler::_parseCgiOutput()` separates headers/body, parses `Status:` header, handles `Location:` header for CGI redirects (302 when body empty, `CgiHandler.cpp:406-411`). |
| 11 | CGI is non-blocking (via epoll) | **PASS** | CGI pipe fds registered with epoll. `Connection` handles `handleCgiInputWrite()` and `handleCgiOutputRead()` asynchronously through epoll events. **EXCEPT**: `CgiHandler::finishOutputRead()` has a **2-second busy-wait spin loop** (`CgiHandler.cpp:168-177`) that blocks the event loop. |
| 12 | CGI `allow_methods` check | **FAIL** | Same as config #4a: CGI path bypasses the `isMethodAllowed()` check entirely. A POST request to a `GET-only` CGI route would still execute. |

---

## 6. Bonus Part (Chapter VI)

### Bonus 1: Cookies and Session Management

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| 1 | `Set-Cookie` header support | **PASS** | `Response::setCookie()` (`Response.cpp:56-84`) with full attributes: name, value, path, domain, maxAge, httpOnly, secure. `Response::toString()` emits `Set-Cookie` headers (`Response.cpp:113-115`). |
| 2 | Cookie header parsing | **PASS** | `Request::parseCookies()` (`Request.cpp:106-153`) parses `Cookie:` header into `std::map<string,string>`. `Request::getCookie(name)` retrieves individual cookies. |
| 3 | Session ID generation | **PASS** | `SessionManager::_generateSessionId()` creates 16-char hex IDs using `rand()` + `time()` + pointer address. |
| 4 | Session storage | **PASS** | `SessionManager` singleton with in-memory `std::map<string, Session>`. Session data supports `key=value` pairs. |
| 5 | Session timeout | **PASS** | `SESSION_TIMEOUT = 1800` (30 minutes). `SessionManager::_isExpired()` checks. `cleanExpired()` method available. |
| 6 | Session integration in request handling | **PASS** | `Connection::_processRequest()` (`Connection.cpp:157-163`) parses cookies, checks for session ID, creates new session if missing, and sets `Set-Cookie` header on every response path (normal, redirect, CGI, error). |
| 7 | Example pages for cookies/sessions | **PASS** | `www/default/login.html` — login form posting to `/cgi-bin/session.py`. `www/default/welcome.html` — welcome page with logout link. `cgi-bin/session.py` — Python CGI that handles login/logout actions. |

### Bonus 2: Multiple CGI Types

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| 1 | Per-extension CGI mapping | **PASS** | `cgi_extension .py /usr/bin/python3;` config. `RouteConfig._cgiHandlers` is a `map<string,string>`. Multiple extensions per route supported. |
| 2 | Multiple CGI programs | **PASS** | Three CGI types configured: `.py` → python3, `.php` → php-cgi, `.sh` → bash. `conf/tests/multi_cgi.conf` demonstrates all three. |
| 3 | Multiple test scripts | **PASS** | `cgi-bin/hello.py`, `cgi-bin/echo.py`, `cgi-bin/session.py` (Python), `cgi-bin/info.php` (PHP), `cgi-bin/env.sh` (Shell). |
| 4 | CgiHandler dispatch by extension | **PASS** | `CgiHandler::start()` line 58-63: `StringUtils::getExtension(fullPath)` then `route.getCgiInterpreter(ext)`. |

### Bonus Prerequisite: Mandatory Must Be Complete

The following mandatory gaps could block bonus evaluation:

1. **`allow_methods` not enforced** — Parsed but completely ignored at runtime. This is a **CRITICAL** failure.
2. **CGI busy-wait in `finishOutputRead()`** — 2-second spin loop blocks event loop.
3. **`FD_CLOEXEC` never used** — FDs leak to CGI children.
4. **`F_GETFL` in CgiHandler** — Violates MacOS fcntl() restriction.
5. **Missing CGI env vars** — `AUTH_TYPE`, `REMOTE_USER`, `SERVER_ADDR`, proper `PATH_INFO`.

---

## 7. README Requirements (Chapter V)

| # | Requirement | Status | Evidence |
|---|-------------|--------|----------|
| 1 | First line italicized attribution | **PASS** | `README.md:1`: `_This project has been created as part of the 42 curriculum by jorgedebian._` |
| 2 | Description section | **PASS** | Lines 5-6: project description with goal and overview. |
| 3 | Instructions section | **PASS** | Lines 92-129: compilation (`make`, `make clean`, `make fclean`, `make re`), execution (`./webserv [configuration file]`). |
| 4 | Resources section (with AI usage) | **PASS** | Lines 174-189: lists RFCs, epoll man page, NGINX docs, Stevens book, AND describes AI usage (Claude/OpenCode for architecture, debugging, boilerplate, optimization). |
| 5 | Written in English | **PASS** | Entire README in English. |
| 6 | Additional sections | **PASS** | Features, Architecture, Configuration, Directives table, Project Structure. |

---

## 8. Summary: All Gaps

### CRITICAL — Must Fix (Blocks Evaluation)

| # | Issue | Description | File:Line |
|---|-------|-------------|-----------|
| C1 | **`allow_methods` not enforced** | The config directive is parsed and stored but `Connection::_processRequest()` never calls `route->isMethodAllowed()`. All GET/POST/DELETE requests are accepted regardless of the `allow_methods` config. | `Connection.cpp:228-240` |
| C2 | **CGI busy-wait spin loop** | `CgiHandler::finishOutputRead()` has a `while` loop that spins for up to 2 seconds (`CgiHandler.cpp:168-177`), blocking the entire event loop. This violates "must remain non-blocking at all times." | `CgiHandler.cpp:168-177` |
| C3 | **`F_GETFL` violates MacOS restriction** | `fcntl(fd, F_GETFL, 0)` is used in `CgiHandler.cpp:337,341`. The subject limits macOS fcntl() to `F_SETFL`, `O_NONBLOCK`, and `FD_CLOEXEC`. | `CgiHandler.cpp:337,341` |

### HIGH — Should Fix (Significant Issues)

| # | Issue | Description | File:Line |
|---|-------|-------------|-----------|
| H1 | **`FD_CLOEXEC` never used** | `pipe()` called without `O_CLOEXEC` and `FD_CLOEXEC` never set on any fd. Client socket, epoll fd, and other fds leak to CGI child processes. | `CgiHandler.cpp:196` |
| H2 | **Missing CGI env vars** | `AUTH_TYPE`, `REMOTE_USER`, `SERVER_ADDR` not set. `PATH_INFO` always empty. `REMOTE_ADDR` hardcoded to 127.0.0.1. `SCRIPT_NAME` includes query string. | `CgiHandler.cpp:208-286` |
| H3 | **CLOSING connections not cleaned up immediately** | `_cleanupConnections()` only removes connections on timeout, not on `_state == CLOSING`. Errored connections linger up to 60s, wasting connection slots. | `Reactor.cpp:131-145` |
| H4 | **`checkTimeout()` never called** | `CgiHandler::checkTimeout()` exists but is never invoked from `Reactor` or `Connection`. CGI timeout protection is inactive. | `CgiHandler.cpp:186-193` |
| H5 | **Makefile unnecessary relinking** | `directories` target not in `.PHONY`, causing rebuild/relink every time. | `Makefile:54` |
| H6 | **Connection `recv()`/`send()` treats all errors as fatal** | On non-blocking sockets, `EAGAIN`/`EWOULDBLOCK` is a normal condition but `handleError()` is called regardless, closing the connection. While errno is not _checked_ (which is correct per the subject), the handling is overly aggressive. | `Connection.cpp:71-73,128-132` |

### MEDIUM — Nice to Have (Improvements)

| # | Issue | Description | File:Line |
|---|-------|-------------|-----------|
| M1 | **Unbounded buffers** | `_readBuffer`, `_outputBuffer`, body accumulation have no size limit. OOM risk under malicious requests. | `Connection.cpp`, `CgiHandler.cpp` |
| M2 | **`std::snprintf`** in C++98 | `StringUtils.cpp:112` uses `std::snprintf` (C99, formally C++11). Compiles with GCC but technically non-conforming. | `StringUtils.cpp:112` |
| M3 | **`gmtime()`/`localtime()` NULL check** | `Response.cpp:99` and `Logger.cpp:72` don't check return value of `gmtime()`/`localtime()`. | `Response.cpp:99`, `Logger.cpp:72` |
| M4 | **`size_t` overflow in size parsers** | `ConfigParser::_parseSize()` and `HttpParser::_parseChunkedBody()` could overflow on malicious input. | `ConfigParser.cpp:304-309`, `HttpParser.cpp:148-161` |
| M5 | **Virtual host matching** | `server_name` is parsed but not used to route requests by `Host` header. All connections on the same port share the same config. | `Reactor.cpp` |
| M6 | **Pipelined request data loss** | When in `WRITING_RESPONSE` state, data received from the client is added to `_readBuffer`, but `_readBuffer.clear()` at line 142 may discard pipelined data. | `Connection.cpp:139-148` |

---

## 9. Priority Action Plan

### Phase 1: Fix Critical Blockers (Must Do Before Evaluation)

1. **Enforce `allow_methods` in `Connection::_processRequest()`** — Add `if (!route->isMethodAllowed(request.getMethod()))` check **before** CGI dispatch, GET/POST/DELETE dispatch, and redirect handling. Return 405 if method not allowed. This affects:
   - `Connection.cpp:156-246` (main dispatch)
   - `Connection.cpp:187-225` (CGI dispatch)

2. **Remove CGI busy-wait loop in `CgiHandler::finishOutputRead()`** — Replace the `while (time(NULL) - waitStart < 2)` spin loop with a single `waitpid(_pid, &status, WNOHANG)`. If the child is still running, kill it with SIGKILL and do one blocking `waitpid`. Do NOT spin in a loop.

3. **Replace `F_GETFL` usage in `CgiHandler.cpp`** — Change:
   ```cpp
   int flags = fcntl(_inputPipe[1], F_GETFL, 0);
   fcntl(_inputPipe[1], F_SETFL, flags | O_NONBLOCK);
   ```
   to:
   ```cpp
   fcntl(_inputPipe[1], F_SETFL, O_NONBLOCK);
   ```
   Same for `_outputPipe[0]`. Fresh pipes have no other flags set.

### Phase 2: Fix High-Priority Issues (Strongly Recommended)

4. **Add `FD_CLOEXEC` to file descriptors before fork** — Use `fcntl(fd, F_SETFD, FD_CLOEXEC)` on the client socket and any other non-pipe fds before forking. This prevents fd leaks to CGI children.

5. **Fix missing CGI environment variables** — Add:
   - `AUTH_TYPE=""` (empty string, per RFC 3875 when no authentication)
   - `REMOTE_USER=""` (empty when no auth)
   - `SERVER_ADDR` derived from `getsockname()` on the client socket
   - Fix `REMOTE_ADDR` to use actual client IP (currently hardcoded to 127.0.0.1)
   - Fix `SCRIPT_NAME` to exclude query string
   - Fix `PATH_INFO` to extract path after script name

6. **Clean up CLOSING connections immediately** — In `Reactor::_cleanupConnections()`, also check for `conn->_state == CLOSING` and remove those connections right away instead of waiting for the timeout.

7. **Invoke `checkTimeout()` from the event loop** — Call `CgiHandler::checkTimeout()` from `Reactor::_cleanupConnections()` or from `Connection`'s timeout check.

8. **Fix Makefile `.PHONY`** — Add `directories` to `.PHONY`:
   ```makefile
   .PHONY: all clean fclean re directories
   ```

### Phase 3: Polish (Optional But Improves Robustness)

9. **Add buffer size limits** — Limit `_readBuffer`, `_outputBuffer`, and request body to `client_max_body_size` or a reasonable maximum.

10. **Fix `recv()`/`send()` error handling** — On non-blocking sockets, a return of -1 with `errno == EAGAIN` is normal. Currently `handleError()` is called, which closes the connection. The subject says don't check errno, but you need to distinguish EAGAIN from real errors. Consider letting epoll handle re-delivery instead of closing.

11. **Fix pipelined request handling** — Don't clear `_readBuffer` when transitioning from WRITING_RESPONSE back to READING_HEADERS; instead, preserve any data already accumulated.

12. **Virtual host routing** — Parse the `Host` header from the request and match it to server configs by `server_name` for virtual hosting support.

---

*End of analysis.*