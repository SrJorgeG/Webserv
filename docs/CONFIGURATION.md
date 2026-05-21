# Configuration

The configuration file uses an NGINX-inspired syntax with `server` and `location` blocks. The parser is strict: missing semicolons, unclosed braces, and unknown directives all cause the server to exit with a non-zero code and an error message.

## Syntax rules

- Blocks open with `{` and close with `}`.
- Directives end with `;`.
- Comments begin with `#` and run to end of line.
- Tokens are whitespace-separated. Values with spaces are not supported (not required by the subject).
- There are no global directives outside of `server` blocks.

## Server block directives

### `listen`

```nginx
listen 8080;
listen 127.0.0.1:8080;
listen 0.0.0.0:8080;
```

Specifies the address and port to bind. A bare port number binds to `0.0.0.0` (all interfaces). A `host:port` form binds to a specific interface. A single `server` block can have multiple `listen` directives — each creates a separate `ServerSocket`.

If two server blocks listen on the same port, the first one acts as the default for that port. The active server config is selected per-request by the `Host` header (see virtual hosting below).

### `server_name`

```nginx
server_name localhost;
server_name example.com;
```

Used for virtual host matching. The value is compared case-sensitively to the hostname in the `Host` header (port stripped). If no server has a matching `server_name`, the first server block listening on the requested port is used.

An empty or absent `server_name` matches any host on the port — effectively the default server for that port.

### `client_max_body_size`

```nginx
client_max_body_size 10M;
client_max_body_size 512K;
client_max_body_size 1G;
client_max_body_size 1048576;   # plain bytes
```

Accepted suffixes: `K`/`k` (kilobytes), `M`/`m` (megabytes), `G`/`g` (gigabytes). No suffix means bytes. Default is 1 MB if omitted.

The check is applied at two points: when `Content-Length` exceeds the limit, the server returns 413 immediately without reading the body. When the accumulated `_readBuffer` exceeds the limit (e.g. chunked uploads), the buffer is cleared and 413 is returned.

### `error_page`

```nginx
error_page 404 /errors/404.html;
error_page 500 502 503 504 /errors/50x.html;
```

Multiple status codes can share one file. The path is relative to the `root` of the first location in the server block. If the error page file cannot be read, a built-in HTML error page is served instead. The built-in page is always available and does not depend on the filesystem.

## Location block directives

A `location` block defines how to handle requests whose URI starts with the given path prefix.

```nginx
location /prefix { ... }
```

Matching is longest-prefix: if `/cgi-bin/py` and `/cgi-bin` are both defined, a request for `/cgi-bin/py/script.py` matches `/cgi-bin/py`. If no location matches, 404 is returned. There is no implicit catch-all; a `location /` block must be present to handle all unmatched requests.

### `root`

```nginx
root www/default;
root /var/www/html;
```

The filesystem directory from which files are served. Paths are relative to the working directory of the process (where the binary was launched), not the config file's directory.

How the filesystem path is computed from a URI:

```
location /uploads { root www/upload; }
Request: GET /uploads/foo/bar.txt

step 1: strip location prefix   /uploads/foo/bar.txt → /foo/bar.txt
step 2: join with root          www/upload + /foo/bar.txt → www/upload/foo/bar.txt
step 3: normalize               resolve .. components, collapse //
step 4: security check          must still start with "www/upload"
```

Exact-match case: `GET /uploads` with `location /uploads` leaves an empty suffix after stripping, which becomes `/`, resolving to the root directory itself.

### `index`

```nginx
index index.html;
```

When a URI resolves to a directory, the server looks for this file inside that directory and serves it. If the file does not exist, behavior falls through to `autoindex`. Only one filename is supported.

### `autoindex`

```nginx
autoindex on;
autoindex off;
```

When `on`, requests for a directory that has no index file produce an HTML directory listing. When `off` (the default), such requests return 403.

### `allow_methods`

```nginx
allow_methods GET;
allow_methods GET POST;
allow_methods POST GET DELETE;
```

Whitelist of permitted HTTP methods for this location. Any other method returns 405 Method Not Allowed. The check is applied after route matching and redirect resolution, before handler dispatch.

### `redirect`

```nginx
redirect /new-path;
redirect https://example.com/;
```

Issues a 301 Moved Permanently with `Location: <value>`. The redirect is issued before method checking — even disallowed methods get redirected. No body is sent (Content-Length: 0).

### `upload_store`

```nginx
upload_store www/upload;
```

Directory where POST uploads are saved. If absent, uploads go to `root`. The directory is created if it does not exist. POST to a location without `upload_store` and without explicit `upload_store` defined returns 403.

### `cgi_extension`

```nginx
cgi_extension .py /usr/bin/python3;
cgi_extension .sh /bin/bash;
cgi_extension .php /usr/bin/php-cgi;
```

Maps a file extension to an interpreter. When a GET or POST request resolves to a file with the given extension, the server runs `execve(interpreter, [interpreter, scriptpath, NULL], envp)` instead of serving the file directly. Multiple `cgi_extension` directives are allowed in one location.

The interpreter path must be absolute. The script path is `chdir`'d to the script's directory before `execve`, so relative paths inside the script work correctly.

## Complete example: default.conf

```nginx
server {
    listen 8080;
    server_name localhost;
    client_max_body_size 10M;

    error_page 404 /errors/404.html;
    error_page 500 502 503 504 /errors/50x.html;

    location / {
        root www/default;
        index index.html;
        autoindex off;
        allow_methods GET POST;
    }

    location /uploads {
        root www/upload;
        allow_methods POST GET DELETE;
        upload_store www/upload;
        autoindex on;
    }

    location /old {
        redirect /;
        allow_methods GET;
    }

    location /cgi-bin {
        root cgi-bin;
        allow_methods GET POST;
        cgi_extension .py /usr/bin/python3;
    }

    location /login.html {
        root www/default;
        allow_methods GET;
    }
}
```

## Multiple CGI types in separate locations

The `multi_cgi.conf` test config demonstrates routing different extensions to different locations:

```nginx
server {
    listen 8080;
    server_name localhost;
    client_max_body_size 10M;

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

    location /cgi-bin/py {
        root cgi-bin;
        allow_methods GET POST;
        cgi_extension .py /usr/bin/python3;
    }

    location /cgi-bin/php {
        root cgi-bin;
        allow_methods GET POST;
        cgi_extension .php /usr/bin/php-cgi;
    }

    location /cgi-bin/sh {
        root cgi-bin;
        allow_methods GET;
        cgi_extension .sh /bin/bash;
    }
}
```

## Multiple virtual hosts on the same port

```nginx
server {
    listen 8080;
    server_name app.example.com;

    location / {
        root www/app;
        index index.html;
        allow_methods GET;
    }
}

server {
    listen 8080;
    server_name static.example.com;

    location / {
        root www/static;
        autoindex on;
        allow_methods GET;
    }
}
```

Both servers bind port 8080. The Host header selects which config applies. A request without a Host header, or with a Host not matching either `server_name`, is handled by the first block.

## Multiple listen addresses

```nginx
server {
    listen 8080;
    listen 127.0.0.1:8081;
    listen 0.0.0.0:9090;
    server_name localhost;
    ...
}
```

Creates three `ServerSocket` objects. All share the same `ServerConfig`. Connections on any of the three addresses are handled identically.

## Edge cases

**`listen` without host**: `listen 8080` binds `0.0.0.0:8080`. The server accepts connections on all interfaces.

**Relative `root`**: `root www/default` is resolved relative to the working directory when the binary is invoked. If you `cd /tmp && /home/user/webserv/webserv`, the root is `/tmp/www/default`. Run from the project root.

**`root` not specified in a location**: The location block is accepted by the parser. If a request reaches it and tries to serve a file, the root will be empty, causing an empty filesystem path. The security check in `resolvePath` returns empty string, which yields 403.

**Duplicate location paths**: The parser does not reject duplicate location paths in the same server block. The last one defined wins, as routes are stored in a vector and matched by longest prefix. Avoid duplicates.

**`redirect` takes precedence over everything**: A location with `redirect` never reaches method checking or file serving. The 301 is issued immediately after route matching.

**Directory URI without trailing slash**: If a URI resolves to a directory but lacks a trailing slash (e.g., `GET /uploads`), the server issues a 301 to `URI + "/"`. This is automatic behavior in `GetHandler`, not a configuration option.
