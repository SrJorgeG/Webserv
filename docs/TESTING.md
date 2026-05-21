# Testing

All tests assume the server is running with the default config:

```bash
make && ./webserv conf/default.conf
```

For multi-CGI tests use:

```bash
./webserv conf/tests/multi_cgi.conf
```

## Static file serving

```bash
# Root index
curl -v http://localhost:8080/
# Expected: 200 with HTML body

# Explicit file
curl -v http://localhost:8080/index.html
# Expected: 200

# File does not exist
curl -v http://localhost:8080/nonexistent.html
# Expected: 404

# Directory without trailing slash — auto-redirect
curl -v http://localhost:8080/uploads
# Expected: 301 Location: /uploads/

# Follow the redirect
curl -vL http://localhost:8080/uploads
# Expected: 200 with directory listing (autoindex on)
```

## Method enforcement

```bash
# DELETE on a GET-only location
curl -v -X DELETE http://localhost:8080/
# Expected: 405

# POST to a GET-only location
curl -v -X POST -d "data" http://localhost:8080/login.html
# Expected: 405

# DELETE on the uploads location (allowed)
curl -v -X DELETE http://localhost:8080/uploads/somefile.txt
# Expected: 404 (file doesn't exist yet) or 204 if it does
```

## File upload and delete

```bash
# Upload a file via multipart/form-data
curl -v -X POST -F "file=@Makefile" http://localhost:8080/uploads/
# Expected: 201 with "File uploaded successfully: Makefile"

# Verify the file is listed
curl http://localhost:8080/uploads/
# Expected: autoindex HTML containing "Makefile"

# Retrieve the uploaded file
curl http://localhost:8080/uploads/Makefile
# Expected: 200 with file contents

# Delete the file
curl -v -X DELETE http://localhost:8080/uploads/Makefile
# Expected: 204

# Verify it's gone
curl -v http://localhost:8080/uploads/Makefile
# Expected: 404

# Upload with raw POST body (no multipart)
curl -v -X POST \
  -H "Content-Type: text/plain" \
  --data-binary "hello raw upload" \
  http://localhost:8080/uploads/
# Expected: 201, filename is auto-generated as upload_<timestamp>.txt
```

## Body size limit

```bash
# Generate a 15 MB request body to exceed the 10M limit
dd if=/dev/zero bs=1M count=15 2>/dev/null | \
  curl -v -X POST \
    -H "Content-Type: application/octet-stream" \
    -H "Content-Length: 15728640" \
    --data-binary @- \
    http://localhost:8080/uploads/
# Expected: 413 Payload Too Large

# Just under the limit — should succeed
dd if=/dev/urandom bs=1M count=9 2>/dev/null > /tmp/test9m.bin
curl -v -X POST -F "file=@/tmp/test9m.bin" http://localhost:8080/uploads/
# Expected: 201
```

## Redirect

```bash
# Configured redirect: location /old { redirect /; }
curl -v http://localhost:8080/old
# Expected: 301 Location: /

# Follow redirect
curl -vL http://localhost:8080/old
# Expected: 200 (root index)
```

## CGI - GET

```bash
# Basic CGI — dumps all environment variables
curl -v http://localhost:8080/cgi-bin/hello.py
# Expected: 200 HTML with list of env vars

# CGI with query string
curl -v "http://localhost:8080/cgi-bin/hello.py?foo=bar&baz=42"
# Expected: 200 with QUERY_STRING=foo=bar&baz=42 visible in output

# CGI script not found
curl -v http://localhost:8080/cgi-bin/missing.py
# Expected: 404

# Interpreter not installed (PHP)
curl -v http://localhost:8080/cgi-bin/info.php
# Expected: 500 (execve fails)
```

## CGI - POST

```bash
# POST form data to CGI
curl -v -X POST \
  -d "username=alice&password=secret" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  http://localhost:8080/cgi-bin/echo.py
# Expected: 200 with REQUEST_METHOD=POST and body content

# POST JSON
curl -v -X POST \
  -H "Content-Type: application/json" \
  -d '{"key":"value"}' \
  http://localhost:8080/cgi-bin/echo.py
# Expected: 200 with CONTENT_TYPE=application/json
```

## CGI - Multi-interpreter (multi_cgi.conf)

```bash
# Python
curl http://localhost:8080/cgi-bin/py/hello.py

# Shell
curl http://localhost:8080/cgi-bin/sh/env.sh

# PHP (500 if php-cgi not installed)
curl http://localhost:8080/cgi-bin/php/info.php
```

## Chunked transfer encoding

```bash
# Send chunked request body
curl -v -X POST \
  -H "Transfer-Encoding: chunked" \
  -H "Content-Type: text/plain" \
  --data-binary "this is chunked data" \
  http://localhost:8080/uploads/
# Expected: 201 (server reassembles chunks before processing)

# Chunked POST to CGI
curl -v -X POST \
  -H "Transfer-Encoding: chunked" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data-binary "key=value" \
  http://localhost:8080/cgi-bin/echo.py
# Expected: 200 with correct CONTENT_LENGTH set to decoded body size
```

## Virtual host matching

```bash
# With multi-server config: second server on port 9090
# Or use Host header with a single-port config that has multiple server blocks

curl -v -H "Host: localhost" http://localhost:8080/
# Expected: 200 from default server

# Host header mismatch — falls back to first server on port
curl -v -H "Host: unknown.local" http://localhost:8080/
# Expected: 200 from first server block (fallback)
```

## Keep-alive and pipelining

```bash
# Two pipelined requests on one connection
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' \
  | nc localhost 8080
# Expected: two complete HTTP responses in the TCP stream

# Connection: close forces single request per connection
curl -v -H "Connection: close" http://localhost:8080/
# Expected: 200 with "Connection: close" in response headers
```

## Connection timeout

```bash
# Open a TCP connection and send nothing — should time out
nc localhost 8080
# (do not type anything — wait 60 seconds)
# Expected: server closes the connection after CONNECTION_TIMEOUT (60s)

# Send headers but no body — should time out waiting for body
printf 'POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\n' | nc localhost 8080
# Expected: server closes connection after timeout
```

## Concurrent connections

```bash
# 20 parallel requests
seq 20 | xargs -P 20 -I{} curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/
# Expected: 20 lines of "200"

# 10 concurrent CGI requests
seq 10 | xargs -P 10 -I{} curl -s -o /dev/null -w "%{http_code}\n" \
  http://localhost:8080/cgi-bin/hello.py
# Expected: 10 lines of "200"

# Apache Bench if installed
ab -n 100 -c 10 http://localhost:8080/
# Expected: no failed requests, consistent response times
```

## Malformed requests

```bash
# Bad request line
printf 'NOTAMETHOD\r\n\r\n' | nc localhost 8080
# Expected: 400 Bad Request

# Missing HTTP version
printf 'GET /\r\n\r\n' | nc localhost 8080
# Expected: 400

# URI not starting with /
printf 'GET http://example.com/ HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc localhost 8080
# Expected: 400

# Negative Content-Length
printf 'POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: -1\r\n\r\n' | nc localhost 8080
# Expected: 400
```

## Error pages

```bash
# Custom 404 page
curl -v http://localhost:8080/nonexistent
# Expected: 404 with content from www/default/errors/404.html (if it exists)
# or built-in HTML page

# 500 from CGI failure
# (assuming info.php exists but php-cgi is not installed)
curl -v http://localhost:8080/cgi-bin/info.php
# Expected: 500 with error page
```

## Session cookies

```bash
# First request — session created, Set-Cookie header in response
curl -v http://localhost:8080/ 2>&1 | grep -i "set-cookie"
# Expected: Set-Cookie: webserv_session_id=<uuid>; Path=/; HttpOnly

# Subsequent request with cookie — same session
SESSION=$(curl -s -c /tmp/cookies.txt http://localhost:8080/ > /dev/null && cat /tmp/cookies.txt | grep webserv_session_id | awk '{print $7}')
curl -v -b "webserv_session_id=$SESSION" http://localhost:8080/
# Expected: same session ID echoed back in Set-Cookie
```

## Autoindex

```bash
# Upload a few files
curl -X POST -F "file=@README.md" http://localhost:8080/uploads/
curl -X POST -F "file=@Makefile" http://localhost:8080/uploads/

# Directory listing
curl http://localhost:8080/uploads/
# Expected: HTML page with links to README.md and Makefile

# Autoindex on root location (need autoindex on in config)
# With default.conf autoindex is off for /
curl http://localhost:8080/
# Expected: serves index.html, not a listing
```

## CGI timeout

To test the 30-second timeout, create a script that loops forever:

```python
#!/usr/bin/env python3
# cgi-bin/infinite.py
import time
while True:
    time.sleep(1)
```

```bash
curl -v --max-time 35 http://localhost:8080/cgi-bin/infinite.py
# Expected: 504 Gateway Timeout after ~30 seconds
# (curl's --max-time is higher than CGI timeout to let server respond)
```

## Memory and resource checks

```bash
# Run valgrind (slow but thorough)
valgrind --leak-check=full --show-leak-kinds=all ./webserv conf/default.conf &
sleep 1
curl http://localhost:8080/
curl http://localhost:8080/cgi-bin/hello.py
curl -X POST -F "file=@README.md" http://localhost:8080/uploads/
curl -X DELETE http://localhost:8080/uploads/README.md
kill %1
# Expected: no definite leaks in valgrind output

# Check open file descriptors after load
./webserv &
SERVER_PID=$!
seq 50 | xargs -P 50 -I{} curl -s http://localhost:8080/ > /dev/null
lsof -p $SERVER_PID | wc -l
# Run again and compare — fd count should not grow indefinitely
kill $SERVER_PID
```

## Extra features testing

These tests cover the eight features that go beyond the 42 subject and the
official bonus. Start the server with the default config unless noted:

```bash
make && ./webserv conf/default.conf
```

### HEAD method

```bash
# HEAD returns headers but no body
curl -v -X HEAD http://localhost:8080/index.html
# Expected: 200, Content-Length header present, body is empty

# HEAD on missing resource — correct headers for error
curl -v -X HEAD http://localhost:8080/does_not_exist.html
# Expected: 404, Content-Length present, no body bytes

# HEAD and GET must return identical headers
curl -sI http://localhost:8080/index.html > /tmp/head_out.txt
curl -s -D - -o /dev/null http://localhost:8080/index.html > /tmp/get_out.txt
diff <(grep -v "^$" /tmp/head_out.txt) <(grep -v "^$" /tmp/get_out.txt)
# Expected: no differences in header lines
```

### OPTIONS method

```bash
# OPTIONS on root — should list GET, POST, HEAD, OPTIONS
curl -v -X OPTIONS http://localhost:8080/
# Expected: 200, Allow: OPTIONS, GET, POST, HEAD

# OPTIONS on uploads — should list POST, GET, DELETE, HEAD, OPTIONS
curl -v -X OPTIONS http://localhost:8080/uploads/
# Expected: 200, Allow header contains DELETE

# Content-Length must be 0 (no body)
curl -sI -X OPTIONS http://localhost:8080/ | grep Content-Length
# Expected: Content-Length: 0

# CORS preflight simulation
curl -v -X OPTIONS http://localhost:8080/ \
  -H "Origin: http://example.com" \
  -H "Access-Control-Request-Method: POST"
# Expected: 200, Allow header present
```

### PUT method

First, ensure `allow_methods PUT` is set on the target location (or use the
`/uploads` location if you add PUT to its allow list in `default.conf`):

```bash
# Create a new resource
curl -v -X PUT http://localhost:8080/uploads/test.txt \
  -H "Content-Type: text/plain" \
  --data-binary "first version"
# Expected: 201 Created, Location: /uploads/test.txt

# Verify the content
curl http://localhost:8080/uploads/test.txt
# Expected: "first version"

# Replace the resource (idempotent)
curl -v -X PUT http://localhost:8080/uploads/test.txt \
  -H "Content-Type: text/plain" \
  --data-binary "second version"
# Expected: 200 OK (not 201)

# PUT to a directory target — conflict
curl -v -X PUT http://localhost:8080/uploads/ \
  --data-binary "data"
# Expected: 409 Conflict

# PUT on a location that does not allow it
curl -v -X PUT http://localhost:8080/ \
  --data-binary "data"
# Expected: 405 Method Not Allowed

# Clean up
curl -X DELETE http://localhost:8080/uploads/test.txt
```

### Range requests (206 Partial Content)

```bash
# Create a 20 KiB test file
dd if=/dev/urandom bs=1K count=20 > /tmp/range_test.bin
curl -X PUT http://localhost:8080/uploads/range_test.bin \
  --data-binary @/tmp/range_test.bin

# Verify Accept-Ranges header on a normal GET
curl -sI http://localhost:8080/uploads/range_test.bin | grep -i accept-ranges
# Expected: Accept-Ranges: bytes

# First 1024 bytes
curl -v -H "Range: bytes=0-1023" \
  http://localhost:8080/uploads/range_test.bin -o /dev/null
# Expected: 206, Content-Range: bytes 0-1023/20480

# Last 512 bytes
curl -v -H "Range: bytes=-512" \
  http://localhost:8080/uploads/range_test.bin -o /dev/null
# Expected: 206, Content-Range: bytes 19968-20479/20480

# From offset 10000 to end
curl -v -H "Range: bytes=10000-" \
  http://localhost:8080/uploads/range_test.bin -o /dev/null
# Expected: 206, Content-Range: bytes 10000-20479/20480

# Out-of-range request
curl -v -H "Range: bytes=999999-999999" \
  http://localhost:8080/uploads/range_test.bin
# Expected: 416 Range Not Satisfiable, Content-Range: bytes */20480

# Clean up
curl -X DELETE http://localhost:8080/uploads/range_test.bin
```

### ETag and 304 Not Modified

```bash
# Get ETag and Last-Modified from an initial request
curl -sI http://localhost:8080/index.html | grep -E "ETag|Last-Modified"
# Example output:
#   ETag: "67d1a4f0-1a3"
#   Last-Modified: Mon, 10 Mar 2025 12:00:00 GMT

# Capture ETag value
ETAG=$(curl -sI http://localhost:8080/index.html \
  | grep -i "^etag:" | awk '{print $2}' | tr -d '\r')
echo "ETag: $ETAG"

# If-None-Match with matching ETag — should get 304, no body
curl -v -H "If-None-Match: $ETAG" http://localhost:8080/index.html
# Expected: 304 Not Modified

# If-None-Match with wrong ETag — should get 200 with body
curl -v -H 'If-None-Match: "wrongetag"' http://localhost:8080/index.html
# Expected: 200 OK

# Wildcard If-None-Match always matches
curl -v -H "If-None-Match: *" http://localhost:8080/index.html
# Expected: 304 Not Modified

# If-Modified-Since with future date — should get 304
curl -v -H "If-Modified-Since: Tue, 01 Jan 2030 00:00:00 GMT" \
  http://localhost:8080/index.html
# Expected: 304 Not Modified

# If-Modified-Since with epoch — should get 200
curl -v -H "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT" \
  http://localhost:8080/index.html
# Expected: 200 OK with full body
```

### try_files directive

Add this location to `conf/default.conf` for testing:

```nginx
location /app {
    root www/default;
    try_files $uri $uri/ /index.html;
    allow_methods GET;
}
```

```bash
# Deep link that has no file on disk — should fall back to index.html
curl -v http://localhost:8080/app/dashboard/settings
# Expected: 200 with content of /index.html

# Existing static file — served directly, no fallback
curl -v http://localhost:8080/app/index.html
# Expected: 200 with actual index.html

# Another non-existent path — same fallback
curl -v http://localhost:8080/app/user/profile
# Expected: 200 with content of /index.html

# Without try_files, a non-existent path returns 404
curl -v http://localhost:8080/nonexistent/path
# Expected: 404 Not Found
```

### HTTP Basic Authentication

Add this location to `conf/default.conf` for testing:

```nginx
location /private {
    root www/default;
    allow_methods GET;
    auth_basic "Test Area";
    auth_basic_user admin;
    auth_basic_password secret123;
}
```

```bash
# No credentials — must get 401 with WWW-Authenticate header
curl -v http://localhost:8080/private/
# Expected: 401 Unauthorized
# Expected header: WWW-Authenticate: Basic realm="Test Area"

# Correct credentials via -u flag
curl -v -u admin:secret123 http://localhost:8080/private/
# Expected: 200 OK

# Wrong password — still 401
curl -v -u admin:wrongpass http://localhost:8080/private/
# Expected: 401 Unauthorized

# Wrong username — still 401
curl -v -u nobody:secret123 http://localhost:8080/private/
# Expected: 401 Unauthorized

# Manual Authorization header (base64 of "admin:secret123")
CREDS=$(printf 'admin:secret123' | base64)
curl -v -H "Authorization: Basic $CREDS" http://localhost:8080/private/
# Expected: 200 OK

# Malformed Authorization header
curl -v -H "Authorization: Basic notbase64!!!" http://localhost:8080/private/
# Expected: 401 Unauthorized
```

### Configurable keepalive_timeout

Test with a config that sets a short timeout (add `keepalive_timeout 2;`
inside a server block in `conf/default.conf`):

```bash
# Verify keepalive works within the timeout window
# Both requests should reuse the same TCP connection
curl -v --keepalive-time 5 \
  http://localhost:8080/ http://localhost:8080/index.html 2>&1 \
  | grep -E "Connected to|Re-using"
# Expected: second request shows "Re-using existing connection"

# After the timeout elapses, connection is closed
# (with keepalive_timeout 2 in config)
curl -v http://localhost:8080/ &
sleep 3
curl -v http://localhost:8080/
# Expected: second curl opens a new connection

# Check Connection header in responses
curl -sI http://localhost:8080/ | grep -i connection
# Expected: Connection: keep-alive (server will reuse for next request)

curl -sI -H "Connection: close" http://localhost:8080/ | grep -i connection
# Expected: Connection: close (server honors client preference)

# Count open sockets — should drop after keepalive_timeout
ss -tnp | grep 8080
# Run a burst, wait for timeout, run again
seq 5 | xargs -P 5 -I{} curl -s -o /dev/null http://localhost:8080/
sleep 15
ss -tnp | grep 8080
# Expected: socket count drops to 0 after keepalive_timeout elapses
```

## Pre-evaluation checklist

```
[ ] make re — compiles with 0 warnings under -Wall -Wextra -Werror -std=c++98
[ ] ./webserv — starts with default config, serves http://localhost:8080/
[ ] GET / → 200
[ ] GET /nonexistent → 404
[ ] DELETE on GET-only route → 405
[ ] POST upload → 201, file appears in /uploads/
[ ] DELETE uploaded file → 204, file gone
[ ] GET /uploads (no slash) → 301 → /uploads/
[ ] GET /uploads/ → 200 with autoindex HTML
[ ] CGI GET → 200 with env vars
[ ] CGI POST → 200 with body echoed
[ ] Body > client_max_body_size → 413
[ ] Chunked POST → 201 or 200
[ ] Redirect location → 301
[ ] 10 concurrent requests → all 200
[ ] Server does not crash on malformed request
[ ] Server does not crash on abrupt client disconnect
[ ] Second config (multi_cgi.conf or multi_port.conf) works
```
