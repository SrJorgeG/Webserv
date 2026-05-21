# CGI

CGI (Common Gateway Interface, RFC 3875) is the protocol that lets a web server execute external programs and use their stdout as the HTTP response body. The server sets environment variables that describe the request, the script reads them, writes headers followed by a blank line followed by content to stdout, and exits.

## Why fork/execve and not threads

The subject forbids threads. Beyond that constraint, `fork`/`execve` is architecturally cleaner for CGI: the child process has a completely separate address space. A misbehaving script cannot corrupt server memory. It inherits only the file descriptors explicitly kept open. When it exits, all its resources are reclaimed by the kernel — no cleanup code needed on the server side except `waitpid`.

The alternative would be dlopen-based embedding (running Python inside the server process), which would require a compatible C API, is interpreter-specific, and provides no isolation.

## Full execution flow

```
Connection::_processRequest()
  │
  ├── CgiHandler::start()
  │     ├── _setupPipes()           pipe(_inputPipe)   stdin of CGI
  │     │                           pipe(_outputPipe)  stdout of CGI
  │     │
  │     ├── _setupEnvironment()     builds envp[] from request + config
  │     │
  │     ├── _forkAndExecute()
  │     │     ├── fork()
  │     │     │
  │     │     ├── child:
  │     │     │     close(_inputPipe[1])      parent's write end not needed
  │     │     │     close(_outputPipe[0])     parent's read end not needed
  │     │     │     dup2(_inputPipe[0],  0)   script reads request body from stdin
  │     │     │     dup2(_outputPipe[1], 1)   script writes response to stdout
  │     │     │     dup2(_outputPipe[1], 2)   stderr also captured (not shown to client)
  │     │     │     close(_inputPipe[0])
  │     │     │     close(_outputPipe[1])
  │     │     │     chdir(script_directory)
  │     │     │     execve(interpreter, [interpreter, script, NULL], envp)
  │     │     │     exit(1)                   reached only if execve fails
  │     │     │
  │     │     └── parent:
  │     │           close(_inputPipe[0])      child's read end not needed
  │     │           close(_outputPipe[1])     child's write end not needed
  │     │           fcntl(_inputPipe[1],  F_SETFD, FD_CLOEXEC)
  │     │           fcntl(_outputPipe[0], F_SETFD, FD_CLOEXEC)
  │     │           fcntl(_inputPipe[1],  F_SETFL, O_NONBLOCK)
  │     │           fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK)
  │     │
  │     └── _startTime = time(NULL)
  │
  ├── Register _cgiInputFd  with epoll EPOLLOUT  (if POST with body)
  └── Register _cgiOutputFd with epoll EPOLLIN
      state = CGI_WRITING_TO_STDIN or CGI_READING_FROM_STDOUT
      return — event loop takes over

--- event loop ---

epoll fires EPOLLOUT on _cgiInputFd:
  Connection::_processCgiWrite()
    write(request body chunk to _inputPipe[1])
    if all body written:
      close(_inputPipe[1])       signals EOF to script's stdin
      state = CGI_READING_FROM_STDOUT

epoll fires EPOLLIN on _cgiOutputFd:
  Connection::_processCgiRead()
    loop:
      read(_outputPipe[0], buffer) → appended to _outputBuffer
      if EAGAIN: break
      if EOF (read returns 0):
        close(_cgiOutputFd)
        CgiHandler::finishOutputRead()
          waitpid(_pid, WNOHANG)
          if child still running: kill(SIGKILL) + waitpid(blocking)
          _parseCgiOutput() → fills Response
        state = WRITING_RESPONSE

epoll fires EPOLLHUP on _cgiOutputFd (no buffered data):
  Connection::handleError() → _processCgiRead() (read returns 0 immediately → EOF path)

--- cleanup loop (every epoll_wait iteration) ---

CgiHandler::checkTimeout():
  if time(NULL) - _startTime > CGI_TIMEOUT (30s):
    kill(_pid, SIGKILL)
    waitpid(_pid, NULL, 0)
    cleanup pipes, state = CGI_IDLE
    → Connection builds 504 response
```

## Why the pipe output fd is registered in epoll

The naive approach to reading CGI output is a blocking loop:

```cpp
// DO NOT DO THIS
while ((n = read(pipe_out, buf, sizeof(buf))) > 0)
    output += buf;
```

This blocks the entire event loop for the duration of the script. If the script takes 2 seconds, no other connection can be served for 2 seconds. For a concurrent server, this is equivalent to having a single-threaded blocking server.

Instead, the output pipe is registered in epoll with `EPOLLIN`. The event loop continues to serve other connections. When the script writes data, epoll fires and `_processCgiRead` reads whatever is available without blocking. When the script exits, the write end of the pipe closes, epoll fires `EPOLLHUP | EPOLLIN` (or just `EPOLLHUP`), and the read returns 0, signaling completion.

## CGI timeout: SIGKILL, not SIGTERM

The CGI timeout is 30 seconds, checked in `_cleanupConnections` after every `epoll_wait`:

```cpp
void CgiHandler::checkTimeout() {
    if (!isRunning() || _startTime == 0) return;
    if (time(NULL) - _startTime > CGI_TIMEOUT) {
        LOG_WARN("CGI timeout, killing process");
        cleanup();  // kill(_pid, SIGKILL); waitpid(_pid, NULL, 0);
    }
}
```

The implementation uses SIGKILL directly rather than SIGTERM-then-SIGKILL. SIGTERM gives the process a chance to clean up — but CGI scripts are not expected to do cleanup, they have no persistent state in the server, and a misbehaving or infinite script may ignore SIGTERM. SIGKILL is immediate and cannot be caught or ignored. The cost is that the child cannot clean up temporary files; this is acceptable because the 42 subject does not require it.

After kill, `waitpid` with no `WNOHANG` is used — a blocking wait is acceptable here because SIGKILL delivery is effectively instantaneous. The connection then builds a 504 Gateway Timeout response.

## CGI output parsing

CGI output is not a complete HTTP response. The protocol (RFC 3875 section 6) specifies that the script outputs CGI headers, a blank line, and then the body:

```
Content-Type: text/html\r\n
Status: 200\r\n
X-Custom: value\r\n
\r\n
<html>...</html>
```

The server parses the headers section, extracts:
- `Status:` — sets the HTTP response status code (default 200 if absent)
- `Content-Type:` — forwarded to the client (default `text/html` if absent)
- `Location:` — triggers a 302 if body is empty and status is 2xx (RFC 3875 client redirect)
- all other headers — forwarded to the client as-is

The body after the blank line becomes the HTTP response body.

If `execve` fails (interpreter not found, script not executable), the child exits with status 1. The parent detects this via `WEXITSTATUS` in `finishOutputRead`:

```cpp
if (childFailed && _outputBuffer.find("Content-Type") == std::string::npos
                && _outputBuffer.find("Status") == std::string::npos) {
    response.buildError(500, ...);
    return;
}
```

If the child failed but produced partial CGI-formatted output anyway (unlikely but possible), the output is still parsed. The heuristic is: if no `Content-Type` or `Status` header exists in the output, assume execve failure and return 500.

## CGI output size limit

`readOutputChunk` enforces a maximum output buffer size (`CGI_MAX_OUTPUT_SIZE`):

```cpp
if (_outputBuffer.size() > CGI_MAX_OUTPUT_SIZE) {
    LOG_WARN("CGI output exceeded maximum size limit, killing process");
    kill(_pid, SIGTERM);
    return -1;
}
```

This prevents a runaway script from filling server memory. The connection gets closed without a proper response.

## Environment variables (RFC 3875)

The full set of variables passed to each CGI script:

| Variable | Value | Source |
|----------|-------|--------|
| `GATEWAY_INTERFACE` | `CGI/1.1` | constant |
| `SERVER_PROTOCOL` | `HTTP/1.1` | constant |
| `SERVER_SOFTWARE` | `webserv/1.0` | constant |
| `REQUEST_METHOD` | `GET`, `POST`, etc. | request method |
| `SCRIPT_NAME` | URI path without query string | request URI |
| `PATH_INFO` | path after script name | empty (not yet implemented per-route) |
| `PATH_TRANSLATED` | `root + PATH_INFO` | empty |
| `QUERY_STRING` | everything after `?` in URI | request URI |
| `CONTENT_LENGTH` | body size in bytes | Content-Length header or body.size() |
| `CONTENT_TYPE` | body MIME type | Content-Type header |
| `SERVER_NAME` | bind address from config | first listen directive |
| `SERVER_PORT` | port from config | first listen directive |
| `SERVER_ADDR` | bind address from config | first listen directive |
| `REMOTE_ADDR` | `127.0.0.1` | hardcoded (getpeername not called) |
| `REMOTE_HOST` | `127.0.0.1` | hardcoded |
| `AUTH_TYPE` | empty | not implemented |
| `REMOTE_USER` | empty | not implemented |
| `REDIRECT_STATUS` | `200` | required by PHP-CGI |
| `HTTP_*` | all request headers | each header converted: hyphens→underscores, uppercase, prefixed with `HTTP_` |

Header conversion example: `User-Agent: curl/7.x` → `HTTP_USER_AGENT=curl/7.x`.

Known limitation: `REMOTE_ADDR` is hardcoded to `127.0.0.1`. The actual client IP requires calling `getpeername(clientFd)`. This does not affect functionality for the 42 evaluation.

## Functional CGI examples

### Basic GET with query string

```python
#!/usr/bin/env python3
# cgi-bin/hello.py
import os

print("Content-Type: text/html")
print("")
print("<html><body>")
print("<h1>CGI Test</h1>")
print("<h2>Environment</h2><ul>")
for key in sorted(os.environ.keys()):
    print("<li><strong>%s:</strong> %s</li>" % (key, os.environ[key]))
print("</ul></body></html>")
```

```bash
curl "http://localhost:8080/cgi-bin/hello.py?foo=bar&baz=123"
```

The script sees `QUERY_STRING=foo=bar&baz=123`. It does not need to parse the URI — the server has already extracted the query string.

### POST with form data

```python
#!/usr/bin/env python3
# cgi-bin/echo.py
import os
import sys

method = os.environ.get("REQUEST_METHOD", "GET")
content_length = int(os.environ.get("CONTENT_LENGTH", 0))

body = ""
if method == "POST" and content_length > 0:
    body = sys.stdin.read(content_length)

print("Content-Type: text/plain")
print("")
print("Method: " + method)
print("Query: " + os.environ.get("QUERY_STRING", ""))
print("Body: " + body)
```

```bash
curl -X POST -d "name=world&lang=python" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  http://localhost:8080/cgi-bin/echo.py
```

The script reads exactly `CONTENT_LENGTH` bytes from stdin. Reading more would block — the server closes the stdin pipe after writing the body, but reading past the content length tries to read from a closed pipe. Always use `CONTENT_LENGTH`.

### Script that returns a custom status code

```python
#!/usr/bin/env python3
import os

print("Content-Type: text/plain")
print("Status: 418")
print("")
print("I am a teapot")
```

The `Status:` header is consumed by the server and becomes the HTTP status line. It is not forwarded to the client.

### Script that issues a redirect

```python
#!/usr/bin/env python3
print("Location: https://example.com/")
print("")
```

Per RFC 3875 section 6.2.4: if `Location` is present and the body is empty and status is 2xx, the server changes the status to 302. The client is redirected.

## Shell CGI example

```bash
#!/bin/bash
# cgi-bin/env.sh
echo "Content-Type: text/plain"
echo ""
echo "Server: $SERVER_SOFTWARE"
echo "Method: $REQUEST_METHOD"
echo "Query: $QUERY_STRING"
env | sort
```

```bash
curl http://localhost:8080/cgi-bin/sh/env.sh
```

The `chdir` to the script's directory before `execve` means relative paths in shell scripts resolve correctly.

## Testing CGI behavior

```bash
# CGI script not found
curl -v http://localhost:8080/cgi-bin/nonexistent.py
# → 404

# Interpreter not installed
curl -v http://localhost:8080/cgi-bin/php/info.php
# → 500 (execve fails, child exits 1)

# Concurrent CGI requests
seq 5 | xargs -P 5 -I{} curl -s http://localhost:8080/cgi-bin/hello.py | grep -c "CGI Test"
# → 5

# POST to CGI
curl -v -X POST -d "key=value" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  http://localhost:8080/cgi-bin/echo.py
```
