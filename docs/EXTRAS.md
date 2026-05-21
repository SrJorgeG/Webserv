# Extra features — beyond the 42 subject

This document covers every feature implemented in this webserv that goes
**beyond** the mandatory subject requirements and the official bonus list
(CGI, multiple servers, cookies/sessions). Each section explains what was
built, shows the relevant C++ code, explains the design rationale, and
provides curl commands to exercise it.

---

## 1. HEAD method

**File:** `src/http/HeadHandler.cpp`

HEAD is defined by RFC 9110 as "identical to GET in every way, except the
server MUST NOT send a body". The key constraint is that all headers —
`Content-Length`, `Content-Type`, `ETag`, `Last-Modified` — must be exactly
the same as they would be for the equivalent GET.

### Implementation

```cpp
// HEAD is identical to GET but the response body must be empty.
// We run the full GET logic so Content-Length, Content-Type, ETag,
// Last-Modified and all caching headers are computed correctly, then
// wipe the body string without touching those headers.
void HeadHandler::handle(const Request& request, Response& response,
                         const RouteConfig& route, const ServerConfig& server) {
    GetHandler get;
    get.handle(request, response, route, server);
    // Preserve Content-Length (already set by GetHandler) but send no body.
    response.clearBody();
}
```

`GetHandler::handle()` runs to completion — resolving the path, checking
permissions, computing ETags, setting all headers. Then `clearBody()` wipes
the body buffer without touching the header map. The wire result is a
complete, correct set of response headers with zero body bytes.

### Why not compute Content-Length manually?

If `GetHandler` would have returned a 403 or 404, the body is an error page
of non-trivial size. Computing that length correctly without running
`GetHandler` would require duplicating all of its logic. Running the full
handler and then discarding the body is the only approach that stays correct
for every response code.

### Design rationale

- Reuses `GetHandler` entirely — no duplicated path resolution, permission,
  or caching logic.
- `clearBody()` is a single `_body.clear()` call that does not touch headers,
  so the already-set `Content-Length` survives.

### curl examples

```bash
# HEAD on a static file — response headers only, no body
curl -v -X HEAD http://localhost:8080/index.html
# Expected: 200, Content-Length header present, zero bytes of body

# HEAD on a non-existent resource
curl -v -X HEAD http://localhost:8080/missing.html
# Expected: 404, Content-Length header present, zero bytes of body

# Compare GET and HEAD headers (should be identical except body)
curl -s -I http://localhost:8080/index.html
curl -s -D - -o /dev/null http://localhost:8080/index.html
```

---

## 2. OPTIONS method

**File:** `src/http/OptionsHandler.cpp`

OPTIONS is the HTTP method used by browsers to perform a CORS preflight
check before sending a cross-origin request. Without it, modern single-page
applications that POST to a different origin will fail silently at the
browser level.

### Implementation

```cpp
void OptionsHandler::handle(const Request& request, Response& response,
                            const RouteConfig& route, const ServerConfig& server) {
    (void)request; (void)server;

    const std::vector<std::string>& allowed = route.getAllowedMethods();
    std::string allow = "OPTIONS";

    bool hasGet = false;
    if (allowed.empty()) {
        allow += ", GET, POST, DELETE, HEAD, PUT";
    } else {
        for (size_t i = 0; i < allowed.size(); ++i) {
            allow += ", " + allowed[i];
            if (allowed[i] == "GET") hasGet = true;
        }
        // HEAD is always available when GET is — it shares the same logic
        if (hasGet) allow += ", HEAD";
    }

    response.setStatus(200);
    response.setHeader("Allow", allow);
    response.setHeader("Content-Length", "0");
    response.setReady(true);
}
```

`OPTIONS` itself is always listed. HEAD is injected automatically whenever
GET is in the route's `allow_methods` list, because `HeadHandler` is always
reachable via `GetHandler` at no extra cost. `Content-Length: 0` is required
by HTTP/1.1 so that keep-alive clients do not stall waiting for a body.

### Design rationale

- The `Allow` value is driven by the same `allow_methods` vector that the
  dispatcher uses for 405 checks, so it is always accurate.
- No `Access-Control-*` headers are emitted (that would require knowing the
  allowed origin from config). The `Allow` header alone is sufficient for
  non-CORS use cases and for testing.

### curl examples

```bash
# OPTIONS on the root location
curl -v -X OPTIONS http://localhost:8080/
# Expected: 200, Allow: OPTIONS, GET, POST, HEAD  (matches default.conf)

# OPTIONS on the uploads location (GET POST DELETE)
curl -v -X OPTIONS http://localhost:8080/uploads/
# Expected: 200, Allow: OPTIONS, POST, GET, DELETE, HEAD

# Simulate a CORS preflight
curl -v -X OPTIONS http://localhost:8080/ \
  -H "Origin: http://example.com" \
  -H "Access-Control-Request-Method: POST"
# Expected: 200, Allow header listing POST
```

---

## 3. PUT method

**File:** `src/http/PutHandler.cpp`

PUT creates or completely replaces a resource at a given URI. It differs
from POST in that PUT is **idempotent**: sending the same PUT request twice
leaves the server in the same state as sending it once. POST is not
idempotent and may create duplicate resources.

### Implementation

```cpp
void PutHandler::handle(const Request& request, Response& response,
                        const RouteConfig& route, const ServerConfig& server) {
    std::string decodedUri = StringUtils::decodeUrl(request.getUri());
    std::string normalizedPath = StringUtils::resolvePath(decodedUri,
                                     route.getPath(), route.getRoot());

    if (normalizedPath.empty()) {
        response.buildError(403, server.getErrorPages(), route.getRoot());
        return;
    }

    if (FileUtils::isDirectory(normalizedPath)) {
        // Cannot replace a directory with a file resource
        response.buildError(409, server.getErrorPages(), route.getRoot());
        return;
    }

    // Ensure parent directory exists
    std::string parent = FileUtils::getParentDirectory(normalizedPath);
    if (!FileUtils::fileExists(parent)) {
        response.buildError(409, server.getErrorPages(), route.getRoot());
        return;
    }

    bool existed = FileUtils::fileExists(normalizedPath);

    if (!FileUtils::writeFile(normalizedPath, request.getBody())) {
        response.buildError(500, server.getErrorPages(), route.getRoot());
        return;
    }

    response.setStatus(existed ? 200 : 201);
    response.setHeader("Content-Length", "0");
    if (!existed) {
        // Point to the newly created resource
        response.setHeader("Location", StringUtils::stripQueryString(request.getUri()));
    }
    response.setReady(true);
}
```

### Response codes

| Condition | Code |
|-----------|------|
| File did not exist, created | 201 Created + Location header |
| File existed, replaced | 200 OK |
| Target is a directory | 409 Conflict |
| Parent directory missing | 409 Conflict |
| Path traversal attempt | 403 Forbidden |
| Filesystem write error | 500 Internal Server Error |

### Config

```nginx
location /uploads {
    root www/upload;
    allow_methods GET PUT DELETE;
    autoindex on;
}
```

### curl examples

```bash
# Create a new resource
curl -v -X PUT http://localhost:8080/uploads/hello.txt \
  -H "Content-Type: text/plain" \
  --data-binary "hello world"
# Expected: 201 Created, Location: /uploads/hello.txt

# Replace the same resource (idempotent)
curl -v -X PUT http://localhost:8080/uploads/hello.txt \
  -H "Content-Type: text/plain" \
  --data-binary "updated content"
# Expected: 200 OK

# Verify it is served correctly after PUT
curl http://localhost:8080/uploads/hello.txt
# Expected: "updated content"

# Try to PUT a directory — must fail
curl -v -X PUT http://localhost:8080/uploads/ \
  --data-binary "data"
# Expected: 409 Conflict

# PUT without allow_methods PUT — must fail
curl -v -X PUT http://localhost:8080/ \
  --data-binary "data"
# Expected: 405 Method Not Allowed
```

---

## 4. Range requests (206 Partial Content)

**File:** `src/http/GetHandler.cpp` — `_handleRange()` and `_serveFile()`

Range requests let clients download a specific byte range of a file. This is
the mechanism used by video players (seek), download managers (resume),
and CDNs (shard large objects). RFC 9110 §14 defines the protocol.

### Implementation — advertising support

Every GET response includes:

```cpp
// Always advertise byte-range support
response.setHeader("Accept-Ranges", "bytes");
```

### Implementation — parsing and responding

```cpp
bool GetHandler::_handleRange(const std::string& path, size_t fileSize,
                              const std::string& rangeHeader, Response& response) {
    if (rangeHeader.substr(0, 6) != "bytes=") return false;

    std::string spec = rangeHeader.substr(6);
    size_t dashPos = spec.find('-');
    if (dashPos == std::string::npos) return false;

    std::string startStr = spec.substr(0, dashPos);
    std::string endStr   = spec.substr(dashPos + 1);

    size_t rangeStart = 0;
    size_t rangeEnd   = fileSize > 0 ? fileSize - 1 : 0;

    if (startStr.empty()) {
        // bytes=-N  →  last N bytes
        size_t suffix = static_cast<size_t>(std::atoi(endStr.c_str()));
        if (suffix == 0 || suffix > fileSize) return false;
        rangeStart = fileSize - suffix;
        rangeEnd   = fileSize - 1;
    } else {
        rangeStart = static_cast<size_t>(std::strtol(startStr.c_str(), NULL, 10));
        if (!endStr.empty())
            rangeEnd = static_cast<size_t>(std::strtol(endStr.c_str(), NULL, 10));
    }

    if (rangeStart > rangeEnd || rangeEnd >= fileSize) {
        // 416 Range Not Satisfiable
        response.setStatus(416);
        std::ostringstream cr;
        cr << "bytes */" << fileSize;
        response.setHeader("Content-Range", cr.str());
        response.setBody("");
        response.setReady(true);
        return true;
    }

    size_t length = rangeEnd - rangeStart + 1;
    std::string data = FileUtils::readFileRange(path, rangeStart, length);
    std::ostringstream cr;
    cr << "bytes " << rangeStart << "-" << rangeEnd << "/" << fileSize;

    response.setStatus(206);
    response.setHeader("Content-Range", cr.str());
    response.setBody(data);
    response.setReady(true);
    return true;
}
```

### Supported range formats

| Format | Meaning |
|--------|---------|
| `bytes=0-1023` | Bytes 0 through 1023 inclusive (first 1 KiB) |
| `bytes=1024-` | From byte 1024 to end of file |
| `bytes=-512` | Last 512 bytes of the file |

### curl examples

```bash
# Create a test file large enough to range
dd if=/dev/urandom bs=1K count=10 > /tmp/10k.bin
curl -X PUT http://localhost:8080/uploads/10k.bin \
  --data-binary @/tmp/10k.bin

# Request first 1024 bytes
curl -v -H "Range: bytes=0-1023" http://localhost:8080/uploads/10k.bin
# Expected: 206, Content-Range: bytes 0-1023/10240

# Request last 512 bytes
curl -v -H "Range: bytes=-512" http://localhost:8080/uploads/10k.bin
# Expected: 206, Content-Range: bytes 9728-10239/10240

# Request from offset 5000 to end
curl -v -H "Range: bytes=5000-" http://localhost:8080/uploads/10k.bin
# Expected: 206, Content-Range: bytes 5000-10239/10240

# Out-of-range request — should fail gracefully
curl -v -H "Range: bytes=99999-99999" http://localhost:8080/uploads/10k.bin
# Expected: 416 Range Not Satisfiable, Content-Range: bytes */10240

# Verify Accept-Ranges is sent on a normal GET
curl -sI http://localhost:8080/index.html | grep -i accept-ranges
# Expected: Accept-Ranges: bytes
```

---

## 5. ETag and conditional requests (304 Not Modified)

**File:** `src/http/GetHandler.cpp` — `_computeEtag()`, `_checkConditional()`,
and the static helpers `utcMktime()` and `parseHttpDate()`.

ETags and conditional headers (`If-None-Match`, `If-Modified-Since`) allow
clients to validate cached responses without re-downloading the body. A
cache hit returns 304 with no body, saving bandwidth and server I/O.

### ETag computation

```cpp
// ETag = "<mtime_hex>-<size_hex>". Using mtime+size avoids needing a hash
// function (which would require libcrypto or implementing MD5 in C++98).
// This is what Apache httpd uses by default.
std::string GetHandler::_computeEtag(const struct stat& st) const {
    return "\"" + StringUtils::toHex(static_cast<unsigned long>(st.st_mtime))
         + "-" + StringUtils::toHex(static_cast<unsigned long>(st.st_size)) + "\"";
}
```

The format `"<mtime_hex>-<size_hex>"` (quoted, as required by RFC 9110) is
identical to what Apache httpd emits by default. MD5 would be more collision-
resistant but requires either `libcrypto` or a C++98-compatible hash
implementation — mtime+size is sufficient to detect changes in the
overwhelming majority of real-world cases.

### Conditional check

```cpp
// Returns true and sends 304 if client cache is still fresh.
// Checks If-None-Match (ETag) first, then If-Modified-Since.
bool GetHandler::_checkConditional(const struct stat& st, const Request& request,
                                   Response& response) {
    std::string etag = _computeEtag(st);

    std::string inm = request.getHeader("If-None-Match");
    if (!inm.empty()) {
        if (inm == etag || inm == "*") {
            response.setStatus(304);
            response.setHeader("ETag", etag);
            response.setReady(true);
            return true;
        }
        return false;   // ETag present but does not match — send 200
    }

    std::string ims = request.getHeader("If-Modified-Since");
    if (!ims.empty()) {
        time_t clientTime = parseHttpDate(ims);
        if (clientTime != -1 && st.st_mtime <= clientTime) {
            response.setStatus(304);
            response.setHeader("ETag", etag);
            response.setReady(true);
            return true;
        }
    }
    return false;
}
```

`If-None-Match` takes precedence over `If-Modified-Since`, as required by
RFC 7232 §6.

### Portable date parsing

`parseHttpDate()` parses RFC 1123 dates (`Mon, 15 Apr 2024 12:00:00 GMT`)
using `sscanf` and converts them to `time_t` via `utcMktime()`. Both
`strptime` (POSIX extension) and `timegm` (GNU extension) are deliberately
avoided to keep the code portable under `-std=c++98`:

```cpp
static time_t utcMktime(struct tm* t) {
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int year = t->tm_year + 1900;
    long days = (year - 1970) * 365L;
    for (int y = 1970; y < year; ++y) {
        if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ++days;
    }
    for (int m = 0; m < t->tm_mon; ++m) {
        days += mdays[m];
        if (m == 1 && ((year % 4 == 0 && year % 100 != 0)
                       || year % 400 == 0)) ++days;
    }
    days += t->tm_mday - 1;
    return days * 86400L + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec;
}
```

### curl examples

```bash
# First request — note the ETag and Last-Modified in the response
curl -sI http://localhost:8080/index.html | grep -E "ETag|Last-Modified"
# Example output:
#   ETag: "67d1a4f0-1a3"
#   Last-Modified: Mon, 10 Mar 2025 12:00:00 GMT

# Second request using If-None-Match — should get 304
ETAG=$(curl -sI http://localhost:8080/index.html | grep -i etag | awk '{print $2}' | tr -d '\r')
curl -v -H "If-None-Match: $ETAG" http://localhost:8080/index.html
# Expected: 304 Not Modified, no body

# Using If-Modified-Since with a future date — should get 304
curl -v -H "If-Modified-Since: Tue, 01 Jan 2030 00:00:00 GMT" \
  http://localhost:8080/index.html
# Expected: 304 Not Modified

# Using If-Modified-Since with a past date — should get 200
curl -v -H "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT" \
  http://localhost:8080/index.html
# Expected: 200 OK with full body

# Wildcard If-None-Match always matches
curl -v -H "If-None-Match: *" http://localhost:8080/index.html
# Expected: 304 Not Modified
```

---

## 6. try_files directive

**File:** `src/http/GetHandler.cpp` — `_tryFilesResolve()`
**Config parsing:** `src/config/ConfigParser.cpp` — `_parseTryFilesDirective()`

`try_files` is the mechanism that makes Single Page Applications work behind
a web server. React, Vue, and Angular routers live entirely in the browser:
every URL (e.g. `/dashboard`, `/user/42`) must serve the same `index.html`
so that JavaScript can take over and render the correct page. Without
`try_files` the server would return 404 for every deep link.

### Implementation

```cpp
// try_files: replaces $uri with the actual request path, then probes each
// candidate in order. The last entry is a fallback path (if prefixed with /
// it is treated as an absolute URI redirect; otherwise as a filesystem path).
std::string GetHandler::_tryFilesResolve(const std::string& uri,
                                          const RouteConfig& route) {
    const std::vector<std::string>& tryFiles = route.getTryFiles();
    if (tryFiles.empty()) return "";

    // Try all entries except the last (which is the fallback)
    for (size_t i = 0; i + 1 < tryFiles.size(); ++i) {
        std::string candidate = tryFiles[i];
        // Replace $uri token with actual request path
        size_t pos = candidate.find("$uri");
        if (pos != std::string::npos)
            candidate.replace(pos, 4, uri);
        std::string resolved = _resolvePath(candidate, route);
        if (!resolved.empty() && FileUtils::fileExists(resolved))
            return resolved;
    }

    // Fallback: last entry — always returned even if it does not exist
    std::string fallback = tryFiles.back();
    size_t pos = fallback.find("$uri");
    if (pos != std::string::npos)
        fallback.replace(pos, 4, uri);
    return _resolvePath(fallback, route);
}
```

The function is called before `_resolvePath()` in `GetHandler::handle()`.
If it returns an empty string (no candidates, no fallback configured) normal
resolution continues as if `try_files` were absent.

### Config example

```nginx
# Single Page Application — React / Vue / Angular
location / {
    root www/app;
    index index.html;
    try_files $uri $uri/ /index.html;
    allow_methods GET;
}

# Static assets with try_files fallback to 404
location /assets {
    root www/app/assets;
    try_files $uri =404;
    allow_methods GET;
}
```

Evaluation order for `try_files $uri $uri/ /index.html`:

1. `$uri` — exact file on disk (e.g. `www/app/about.js`)
2. `$uri/` — treat as directory, look for index file inside
3. `/index.html` — fallback: always serve the SPA shell

### curl examples

```bash
# Assuming try_files $uri $uri/ /index.html; is set on /

# Deep link that does not exist as a file — SPA fallback
curl -v http://localhost:8080/dashboard
# Expected: 200 with content of /index.html

# Existing static asset — served directly, no fallback
curl -v http://localhost:8080/style.css
# Expected: 200 with actual CSS content

# Verify $uri substitution
curl -v http://localhost:8080/nonexistent/deep/path
# Expected: 200 with /index.html (fallback)
```

---

## 7. HTTP Basic Authentication

**File:** `src/core/Connection.cpp` — `_checkBasicAuth()`
**Base64 decoder:** `src/utils/StringUtils.cpp` — `StringUtils::base64Decode()`

HTTP Basic Auth (RFC 7617) is the simplest authentication scheme: the client
sends credentials as `Base64(username:password)` in an `Authorization`
header. It is trivial to implement, easy to test with curl, and sufficient
to demonstrate the authentication mechanism required by real deployments.

### Connection-level auth check

```cpp
// Validates HTTP Basic Auth credentials.
// The Authorization header value is "Basic <base64(user:password)>".
// We decode it, split on ':', and compare with the configured credentials.
bool Connection::_checkBasicAuth(const RouteConfig& route) {
    std::string authHeader = _request.getHeader("Authorization");
    if (authHeader.empty()) return false;
    if (authHeader.substr(0, 6) != "Basic ") return false;

    std::string decoded = StringUtils::base64Decode(authHeader.substr(6));
    size_t sep = decoded.find(':');
    if (sep == std::string::npos) return false;

    std::string user = decoded.substr(0, sep);
    std::string pass = decoded.substr(sep + 1);

    return user == route.getAuthUser() && pass == route.getAuthPassword();
}
```

The check runs after method validation and before any file handler or CGI
is invoked. On failure, the response is built immediately:

```cpp
if (route->requiresAuth() && !_checkBasicAuth(*route)) {
    _response.setStatus(401);
    _response.setHeader("WWW-Authenticate",
        "Basic realm=\"" + route->getAuthRealm() + "\"");
    _response.setHeader("Content-Length", "0");
    _response.setReady(true);
    return;
}
```

### Base64 decoder (no external library)

```cpp
std::string StringUtils::base64Decode(const std::string& encoded) {
    static const std::string table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, bits = -8;
    for (size_t i = 0; i < encoded.size(); ++i) {
        char c = encoded[i];
        if (c == '=') break;
        size_t pos = table.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            result += static_cast<char>((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return result;
}
```

The standard Base64 algorithm: each input character contributes 6 bits;
when 8 bits accumulate, one output byte is emitted. No `openssl`, no
`<base64.h>`, no external dependency.

### Why Basic and not Digest Auth?

Digest Auth (RFC 7616) requires MD5. MD5 is not part of the C++98 standard
library. Implementing MD5 from scratch is ~200 lines and adds complexity
without contributing to the learning goals of the project. Basic Auth
demonstrates the full request/response authentication flow and is production-
ready when used over HTTPS.

### Config example

```nginx
location /private {
    root www/default;
    allow_methods GET;
    auth_basic "Restricted Area";
    auth_basic_user admin;
    auth_basic_password secret123;
}
```

### curl examples

```bash
# Unauthenticated request — must get 401
curl -v http://localhost:8080/private/
# Expected: 401 Unauthorized
# Expected header: WWW-Authenticate: Basic realm="Restricted Area"

# Correct credentials
curl -v -u admin:secret123 http://localhost:8080/private/
# Expected: 200 OK

# Wrong password
curl -v -u admin:wrongpass http://localhost:8080/private/
# Expected: 401 Unauthorized

# Manually constructed Authorization header
CREDS=$(printf 'admin:secret123' | base64)
curl -v -H "Authorization: Basic $CREDS" http://localhost:8080/private/
# Expected: 200 OK

# No credentials in interactive mode — browser will prompt
# curl simulates this with --anyauth
curl -v --anyauth -u admin:secret123 http://localhost:8080/private/
```

---

## 8. Configurable keepalive_timeout

**Files:** `src/core/Connection.cpp` — `isTimedOut()`
**Config parsing:** `src/config/ConfigParser.cpp` — `_parseKeepaliveTimeoutDirective()`
**Default:** `include/Webserv.hpp` — `KEEP_ALIVE_TIMEOUT 10`

HTTP/1.1 keep-alive reuses TCP connections across multiple requests. The
server must close idle connections after some period to free file descriptors.
The timeout that is appropriate for a static-file CDN (long, to amortize
connection setup over many asset requests) differs from what is right for an
API endpoint (short, to release sockets quickly after each transaction).

### Implementation

```cpp
bool Connection::isTimedOut() const {
    time_t timeout = _isKeepAliveIdle
        ? static_cast<time_t>(_serverConfig->getKeepaliveTimeout())
        : static_cast<time_t>(CONNECTION_TIMEOUT);
    return (time(NULL) - _lastActivity) > timeout;
}
```

When the connection is between requests (`_isKeepAliveIdle == true`) the
per-server `keepalive_timeout` applies. During an active request the global
`CONNECTION_TIMEOUT` (60 s) applies instead so that slow uploads and CGI
scripts are not killed prematurely.

`getKeepaliveTimeout()` reads the value stored in `ServerConfig`:

```cpp
// ServerConfig.cpp
int ServerConfig::getKeepaliveTimeout() const { return _keepaliveTimeout; }
void ServerConfig::setKeepaliveTimeout(int seconds) { _keepaliveTimeout = seconds; }
```

The default is `KEEP_ALIVE_TIMEOUT` (10 s, defined in `Webserv.hpp`).
`ConfigParser::_parseKeepaliveTimeoutDirective()` overrides it per server
block.

### Config example

```nginx
# Fast API server — close idle connections quickly
server {
    listen 8080;
    server_name api.example.com;
    keepalive_timeout 5;
    # ...
}

# Static asset server — longer keepalive for browser parallelism
server {
    listen 9090;
    server_name assets.example.com;
    keepalive_timeout 30;
    # ...
}
```

### curl examples

```bash
# Default timeout (10 s) — open a connection and then go idle
curl -v --keepalive-time 15 http://localhost:8080/
# Send a second request after 12 s — server may have closed the connection
# (curl will reconnect transparently; check server logs for "connection closed")

# Verify keepalive works within the timeout window
curl -v --keepalive-time 5 \
  http://localhost:8080/ http://localhost:8080/index.html
# Expected: both responses on the same TCP connection (single TLS handshake
# if HTTPS were used), then connection closed

# Test with a short timeout in config (keepalive_timeout 2;)
# Open connection, wait 3 s, try a second request
curl -v http://localhost:8080/ &
sleep 3
curl -v http://localhost:8080/
# Second request should open a new connection

# Observe active connections
ss -tnp | grep 8080
# After keepalive_timeout elapses with no traffic,
# ESTABLISHED count should drop back to 0
```

---

## Quick reference — all extras

| # | Feature | RFC / Spec | Key file(s) |
|---|---------|-----------|------------|
| 1 | HEAD method | RFC 9110 §9.3.2 | `src/http/HeadHandler.cpp` |
| 2 | OPTIONS method | RFC 9110 §9.3.7 | `src/http/OptionsHandler.cpp` |
| 3 | PUT method | RFC 9110 §9.3.4 | `src/http/PutHandler.cpp` |
| 4 | Range requests / 206 | RFC 9110 §14 | `src/http/GetHandler.cpp` |
| 5 | ETag / 304 Not Modified | RFC 7232 | `src/http/GetHandler.cpp` |
| 6 | try_files directive | nginx convention | `src/http/GetHandler.cpp`, `src/config/ConfigParser.cpp` |
| 7 | HTTP Basic Auth | RFC 7617 | `src/core/Connection.cpp`, `src/utils/StringUtils.cpp` |
| 8 | Configurable keepalive_timeout | RFC 9112 §9.3 | `src/core/Connection.cpp`, `src/config/ServerConfig.cpp` |
