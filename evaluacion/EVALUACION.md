# Webserv - Guion de Correccion (3 personas)

Servidor HTTP/1.1 en C++98. Single-threaded, non-blocking, event-driven con `epoll` en Linux. CGI via `fork`/`execve`, I/O de pipes no bloqueante integrado en el mismo event loop.

**Autores:** jgomez-d, dcid-san (+ 1 companero)

---

## Division de tareas para la correccion

El proyecto se divide en **3 bloques arquitectonicos**, uno por companero. Cada bloque corresponde a una capa del servidor y a un conjunto de archivos coherente. El orden de presentacion sigue el flujo de una peticion: primero la infraestructura de red (Parte 1), despues el procesamiento HTTP (Parte 2), y finalmente CGI + configuracion + utilidades (Parte 3).

| Parte | Companero | Capa | Archivos principales | Lineas aprox. |
|-------|-----------|------|----------------------|---------------|
| **1** | Companero A | Core / Event Loop / I/O de red | `Reactor`, `ServerSocket`, `Connection`, `main` | ~990 |
| **2** | Companero B | Protocolo HTTP & Handlers | `HttpParser`, `Request`, `Response`, `*Handler`, `SessionManager` | ~1674 |
| **3** | Companero C | CGI, Configuracion & Utils | `CgiHandler`, `ConfigParser`, `ServerConfig`, `RouteConfig`, `StringUtils`, `FileUtils`, `Logger` | ~1902 |

Cada companero tiene su propio `.md` detallado en esta misma carpeta:
- [`PARTE1_CORE_EVENT_LOOP.md`](./PARTE1_CORE_EVENT_LOOP.md)
- [`PARTE2_HTTP_HANDLERS.md`](./PARTE2_HTTP_HANDLERS.md)
- [`PARTE3_CGI_CONFIG_UTILS.md`](./PARTE3_CGI_CONFIG_UTILS.md)

---

## Parte 1 - Core, Event Loop & I/O de red

**Archivos:** `src/main.cpp`, `src/core/Reactor.cpp`, `src/core/ServerSocket.cpp`, `src/core/Connection.cpp`
**Headers:** `include/core/Reactor.hpp`, `include/core/ServerSocket.hpp`, `include/core/Connection.hpp`, `include/core/EventHandler.hpp`, `include/Webserv.hpp`

### Que hay que explicar

1. **Patron Reactor y epoll**: por que epoll y no select/poll, level-triggered vs edge-triggered, el unico `epoll_fd` para todos los fds.
2. **Interface `EventHandler`**: polimorfismo. `ServerSocket` y `Connection` implementan la misma interface. El Reactor no sabe a quien despacha.
3. **Ciclo de vida de una `Connection`**: maquina de estados (`READING_HEADERS` -> `READING_BODY` -> `PROCESSING` -> `WRITING_RESPONSE` -> `CLOSING`), estados CGI, transiciones.
4. **I/O no bloqueante**: `O_NONBLOCK` en todos los fds. Por que NO se revisa `errno` despues de `recv`/`send` (requisito del subject). Como se resuelven EAGAIN y errores reales sin errno.
5. **Orden de dispatch EPOLLIN antes que EPOLLHUP**: el bug real que se encontro y como se soluciono.
6. **Keep-alive y pipelining**: como se preserva el leftover del buffer, timeout de keep-alive vs timeout de conexion activa.
7. **Virtual hosting**: matching por `Host` header, fallback al primer server del puerto.
8. **Cleanup loop**: `_cleanupConnections()` tras cada `epoll_wait`. Por que las conexiones se marcan CLOSING y se borran despues, no durante el dispatch.
9. **RAII y gestion de fds**: destructores cierran fds, `FD_CLOEXEC` en sockets de cliente y pipes CGI.

### Demo sugerido

```bash
make && ./webserv conf/default.conf
# En otra terminal:
seq 20 | xargs -P 20 -I{} curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/
# Esperado: 20 lineas de "200" — concurrencia con un solo hilo
```

---

## Parte 2 - Protocolo HTTP & Handlers

**Archivos:** `src/http/HttpParser.cpp`, `src/http/Request.cpp`, `src/http/Response.cpp`, `src/http/GetHandler.cpp`, `src/http/PostHandler.cpp`, `src/http/DeleteHandler.cpp`, `src/http/PutHandler.cpp`, `src/http/HeadHandler.cpp`, `src/http/OptionsHandler.cpp`, `src/http/SessionManager.cpp`, `src/http/StatusCodes.cpp`
**Headers:** `include/http/*.hpp`

### Que hay que explicar

1. **HttpParser incremental**: TCP es un stream. Los datos llegan fraccionados, pegados o parciales. Estados `_requestLineComplete`, `_headersComplete`. Valores de retorno `PARSE_OK` / `PARSE_INCOMPLETE` / `PARSE_ERROR`.
2. **Pipelining HTTP**: leftover buffer preservado entre requests en la misma conexion. `getLeftoverData()`.
3. **Chunked transfer encoding**: des-ensamblado de chunks antes de procesar. El CGI espera EOF, no chunks.
4. **Patron Strategy**: `IHttpMethodHandler`. `Connection` no sabe la logica de GET vs POST. Inyeccion del handler segun el metodo.
5. **GET Handler**: resolucion de path, `try_files`, autoindex, redirect de directorio sin `/`, Range requests (206), ETag/conditional requests (304), path traversal protection.
6. **POST Handler**: parsing de `multipart/form-data`, extraccion del boundary, guardado en `upload_store`. POST raw (sin multipart) con nombre autogenerado.
7. **DELETE, PUT, HEAD, OPTIONS**: idempotencia de PUT, HEAD reutiliza GET y borra body, OPTIONS devuelve `Allow` header.
8. **SessionManager (bonus)**: cookie `webserv_session_id`, Singleton, expiracion de 30 min, limpieza periodica.
9. **Codigos de estado**: tabla completa de codigos implementados (200, 201, 204, 206, 301, 302, 304, 400, 401, 403, 404, 405, 409, 413, 416, 500, 504).
10. **Basic Auth (extra)**: decodificacion Base64 sin librerias, header `WWW-Authenticate`.

### Demo sugerido

```bash
# GET estatico
curl -v http://localhost:8080/

# Upload multipart
curl -v -X POST -F "file=@Makefile" http://localhost:8080/uploads/

# Chunked
curl -v -X POST -H "Transfer-Encoding: chunked" --data-binary "hello chunked" http://localhost:8080/uploads/

# Range request
curl -v -H "Range: bytes=0-1023" http://localhost:8080/uploads/Makefile

# ETag / 304
ETAG=$(curl -sI http://localhost:8080/index.html | grep -i etag | awk '{print $2}' | tr -d '\r')
curl -v -H "If-None-Match: $ETAG" http://localhost:8080/index.html

# Sesion
curl -v http://localhost:8080/ 2>&1 | grep -i set-cookie
```

---

## Parte 3 - CGI, Configuracion & Utils

**Archivos:** `src/cgi/CgiHandler.cpp`, `src/config/ConfigParser.cpp`, `src/config/ServerConfig.cpp`, `src/config/RouteConfig.cpp`, `src/utils/StringUtils.cpp`, `src/utils/FileUtils.cpp`, `src/utils/Logger.cpp`
**Headers:** `include/cgi/CgiHandler.hpp`, `include/config/*.hpp`, `include/utils/*.hpp`

### Que hay que explicar

1. **CGI con fork/execve**: por que fork y no threads (subject lo prohíbe + aislamiento de memoria). Dos pipes unidireccionales. `dup2` en el hijo. `chdir` al directorio del script.
2. **CGI en el event loop**: los pipes se registran en el mismo `epoll_fd`. `_processCgiWrite` escribe el body al stdin del CGI. `_processCgiRead` lee el stdout hasta EOF. No se bloquea el servidor esperando al CGI.
3. **Maquina de estados del CGI**: `CGI_IDLE` -> `CGI_RUNNING` -> `CGI_WRITING` -> `CGI_READING` -> `CGI_DONE`.
4. **Variables de entorno CGI (RFC 3875)**: `GATEWAY_INTERFACE`, `REQUEST_METHOD`, `SCRIPT_NAME`, `QUERY_STRING`, `CONTENT_LENGTH`, `HTTP_*`, `REDIRECT_STATUS`. Conversion de headers HTTP a `HTTP_*`.
5. **Parseo de salida CGI**: separacion headers/body con `\r\n\r\n`. Header `Status:` -> codigo de respuesta. `Location` sin body -> 302. Deteccion de fallo de `execve` -> 500.
6. **Timeout de CGI (30s)**: SIGKILL directo (no SIGTERM). `waitpid` con `WNOHANG`. Respuesta 504. Problema del zombie process.
7. **ConfigParser**: parser descendente recursivo estilo NGINX. Tokenizacion, bloques anidados `server`/`location`. Directivas soportadas (tabla completa).
8. **ServerConfig y RouteConfig**: estructura de datos jerarquica. Longest-prefix matching de rutas. Multi-listen, virtual hosts.
9. **StringUtils::resolvePath**: seguridad contra path traversal. Normalizacion antes del check `startsWith`. Stripping de query string. Decodificacion URL.
10. **FileUtils y Logger**: readFile, readFileRange, isDirectory, joinPath, normalizePath. Logger Singleton con niveles.

### Demo sugerido

```bash
# CGI GET con query string
curl -v "http://localhost:8080/cgi-bin/hello.py?name=world"

# CGI POST
curl -v -X POST -d "key=value" -H "Content-Type: application/x-www-form-urlencoded" http://localhost:8080/cgi-bin/echo.py

# Multi-CGI
./webserv conf/tests/multi_cgi.conf
curl http://localhost:8080/cgi-bin/py/hello.py
curl http://localhost:8080/cgi-bin/sh/env.sh

# Path traversal bloqueado
curl -v "http://localhost:8080/../etc/passwd"
# Esperado: 403

# Configuracion: cambiar conf/default.conf y reiniciar
```

---

## Checklist comun (todos deben saber responder)

Estas preguntas pueden dirigirse a cualquier companero durante la correccion:

- [ ] `make re` compila con 0 warnings bajo `-Wall -Wextra -Werror -std=c++98`
- [ ] El servidor no crashea bajo ninguna circunstancia (malformed requests, disconnects, OOM)
- [ ] Solo 1 `poll()`/`epoll_wait()` para todas las operaciones de I/O
- [ ] `poll()`/`epoll` monitorea lectura y escritura simultaneamente
- [ ] Nunca se hace `read`/`write` sin pasar por `epoll` primero
- [ ] No se revisa `errno` despues de I/O no bloqueante
- [ ] `fork` solo se usa para CGI
- [ ] GET, POST y DELETE funcionan
- [ ] Se puede servir un sitio web estatico completo
- [ ] Los clientes pueden subir archivos
- [ ] Hay paginas de error por defecto
- [ ] El servidor escucha multiples puertos
- [ ] Stress test: el servidor sigue disponible
- [ ] Respuesta a una peticion nunca se cuelga indefinidamente

---

## Orden recomendado de la correccion

1. **Compilar y arrancar** (cualquiera): `make re && ./webserv conf/default.conf`
2. **Parte 1** explica la arquitectura base y demuestra concurrencia
3. **Parte 2** explica HTTP y demuestra GET/POST/DELETE/upload/chunked/sesiones
4. **Parte 3** explica CGI y configuracion, demuestra CGI y cambia de config
5. **Modificacion en vivo**: el evaluador puede pedir un cambio pequeno (anyadir un location, cambiar un metodo permitido, etc.). Todos deben poder hacerlo.
