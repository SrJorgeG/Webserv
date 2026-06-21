# Parte 3 - CGI, Configuracion & Utils

**Companero C**
**Archivos:** `src/cgi/CgiHandler.cpp`, `src/config/ConfigParser.cpp`, `src/config/ServerConfig.cpp`, `src/config/RouteConfig.cpp`, `src/utils/StringUtils.cpp`, `src/utils/FileUtils.cpp`, `src/utils/Logger.cpp`
**Headers:** `include/cgi/CgiHandler.hpp`, `include/config/*.hpp`, `include/utils/*.hpp`

Esta capa implementa la interaccion con procesos externos (CGI), el parser de configuracion estilo NGINX, y las utilidades transversales (resolucion de paths, seguridad, I/O de archivos, logging).

---

## 1. CGI: fork/execve y pipes

### Por que fork/execve y no threads

El subject **prohíbe threads**. Mas alla de eso, `fork`/`execve` es arquitecturalmente superior para CGI:
- El proceso hijo tiene espacio de direcciones separado. Un script mal comportado no puede corromper la memoria del servidor.
- Hereda solo los fds explicitamente mantenidos abiertos.
- Al salir, el kernel reclama todos sus recursos. La unica limpieza necesaria es `waitpid`.

La alternativa (dlopen + embebir el interprete) requeriria una API C especifica por interprete y no daria aislamiento.

### Arquitectura de pipes

Dos pipes unidireccionales por peticion CGI:

```
Servidor (padre)                    Script CGI (hijo)

_inputPipe[1]  ──── write ──────►  _inputPipe[0]  ───► STDIN
_outputPipe[0] ◄──── read ──────   _outputPipe[1] ◄── STDOUT/STDERR
```

Despues de `fork()`:
- **Padre** cierra `_inputPipe[0]` (lectura) y `_outputPipe[1]` (escritura).
- **Hijo** cierra `_inputPipe[1]` (escritura) y `_outputPipe[0]` (lectura).
- **Hijo** hace `dup2(_inputPipe[0], STDIN)` y `dup2(_outputPipe[1], STDOUT)` y `dup2(_outputPipe[1], STDERR)`.
- **Hijo** hace `chdir(directorio_del_script)` para rutas relativas.
- **Hijo** llama `execve(interprete, [interprete, script, NULL], envp)`.
- Si `execve` falla, el hijo hace `exit(1)`.

### Configuracion no bloqueante de pipes

```cpp
fcntl(_inputPipe[1],  F_SETFL, O_NONBLOCK);   // stdin write end
fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK);   // stdout read end
fcntl(_inputPipe[1],  F_SETFD, FD_CLOEXEC);   // no heredar en futuros fork
fcntl(_outputPipe[0], F_SETFD, FD_CLOEXEC);
```

`FD_CLOEXEC` es critico: si hay otro CGI despues, los pipes de este CGI no se filtran al nuevo proceso hijo.

---

## 2. CGI en el event loop

### Por que registrar los pipes en epoll

El enfoque naíf es un bucle bloqueante de lectura:

```cpp
// NO HACER ESTO
while ((n = read(pipe_out, buf, sizeof(buf))) > 0)
    output += buf;
```

Esto **bloquea todo el event loop** durante la ejecucion del script. Si el script tarda 2 segundos, ninguna otra conexion se sirve en esos 2 segundos. Equivalente a un servidor single-threaded bloqueante.

En lugar de eso, el pipe de salida se registra en epoll con `EPOLLIN`. El event loop sigue sirviendo otras conexiones. Cuando el script escribe, epoll dispara y `_processCgiRead` lee lo disponible sin bloquear. Cuando el script termina, el pipe se cierra, epoll dispara `EPOLLHUP | EPOLLIN` y `read` devuelve 0 (EOF).

### Registro tras fork

```cpp
// Connection.cpp - rama CGI
if (_request.getMethod() == "POST" && !_request.getBody().empty()) {
    _state = CGI_WRITING_TO_STDIN;
    _reactor.registerHandler(_cgiInputFd, this, EPOLLOUT);   // escribir body al CGI
    _reactor.registerHandler(_cgiOutputFd, this, EPOLLIN);   // leer output del CGI
} else {
    _state = CGI_READING_FROM_STDOUT;
    close(_cgiInputFd);    // no hay body que enviar
    _reactor.registerHandler(_cgiOutputFd, this, EPOLLIN);
}
```

El `this` significa que el mismo `Connection` maneja eventos en los pipes. Cuando epoll dispara en `_cgiOutputFd`, `handleRead()` se enruta a `_processCgiRead()` segun el estado.

### Flujo de eventos

```
epoll EPOLLOUT en _cgiInputFd:
  _processCgiWrite()
    write(body chunk a _inputPipe[1])
    si todo el body escrito:
      close(_inputPipe[1])    // senala EOF al stdin del script
      state = CGI_READING_FROM_STDOUT

epoll EPOLLIN en _cgiOutputFd:
  _processCgiRead()
    loop:
      read(_outputPipe[0], buffer)
      si EAGAIN: break (no hay mas datos ahora)
      si EOF (read == 0):
        close(_cgiOutputFd)
        CgiHandler::finishOutputRead()
          waitpid + parseCgiOutput -> Response
        state = WRITING_RESPONSE
```

---

## 3. Maquina de estados del CGI

```
CGI_IDLE --> CGI_RUNNING --> CGI_WRITING --> CGI_READING --> CGI_DONE
                |              |                |
                v              v                v
           (fork+execve)  (escribir body    (leer output
                          al stdin pipe)     del stdout pipe)
```

1. **`CgiHandler::start()`**: crea pipes, construye entorno CGI, fork, execve.
2. **Escritura del body (POST)**: el padre registra `_inputPipe[1]` con `EPOLLOUT`. Cuando epoll notifica, `_processCgiWrite` escribe un fragmento. Al terminar, cierra el pipe (EOF en stdin del CGI).
3. **Lectura del output**: `_outputPipe[0]` registrado con `EPOLLIN`. `_processCgiRead` lee hasta EAGAIN o EOF.
4. **EOF**: el CGI termino. Se hace `waitpid` y se parsea la salida.
5. **Timeout**: `_cleanupConnections()` verifica en cada iteracion si el CGI excedio 30s.

---

## 4. Variables de entorno CGI (RFC 3875)

`_setupEnvironment()` construye las variables requeridas:

| Variable | Origen |
|----------|--------|
| `GATEWAY_INTERFACE` | Constante `CGI/1.1` |
| `SERVER_PROTOCOL` | Constante `HTTP/1.1` |
| `SERVER_SOFTWARE` | Constante `webserv/1.0` |
| `REQUEST_METHOD` | De la request line |
| `SCRIPT_NAME` | URI path sin query string |
| `QUERY_STRING` | URI despues de `?` |
| `CONTENT_TYPE` | Header `Content-Type` |
| `CONTENT_LENGTH` | Header `Content-Length` o tamano del body |
| `SERVER_NAME`, `SERVER_PORT`, `SERVER_ADDR` | Directiva `listen` |
| `REMOTE_ADDR` | `127.0.0.1` (hardcoded, limitacion conocida) |
| `REDIRECT_STATUS` | `200` (requerido por PHP-CGI) |
| `HTTP_*` | Cada header HTTP: `User-Agent` -> `HTTP_USER_AGENT` |

Conversion de headers: guiones -> underscores, mayusculas, prefijo `HTTP_`.

**Limitacion conocida**: `REMOTE_ADDR` esta hardcoded. El IP real del cliente requiere `getpeername(clientFd)`. No afecta la evaluacion de 42.

---

## 5. Parseo de salida CGI

La salida CGI no es una respuesta HTTP completa. RFC 3875 seccion 6: el script outputa headers CGI, linea en blanco, y body:

```
Content-Type: text/html\r\n
Status: 200\r\n
X-Custom: value\r\n
\r\n
<html>...</html>
```

`_parseCgiOutput()` separa headers y body con `\r\n\r\n` (o `\n\n` fallback):
- **`Status:`** -> setea el codigo de respuesta HTTP (default 200 si ausente).
- **`Content-Type:`** -> se reenvia al cliente (default `text/html` si ausente).
- **`Location:`** -> si body vacio y status 2xx, se asume 302 (RFC 3875 6.2.4).
- **Otros headers** -> se reenvian al cliente.

### Deteccion de fallo de execve

Si `execve` falla (interprete no encontrado), el hijo hace `exit(1)`. El padre detecta via `WEXITSTATUS`:

```cpp
if (childFailed && _outputBuffer.find("Content-Type") == std::string::npos
                && _outputBuffer.find("Status") == std::string::npos) {
    response.buildError(500, ...);
}
```

Heuristica: si no hay `Content-Type` ni `Status` en la salida, asumir fallo de execve y devolver 500.

---

## 6. Timeout de CGI y zombie process

### Timeout (30s)

```cpp
void CgiHandler::checkTimeout() {
    if (time(NULL) - _startTime > CGI_TIMEOUT) {
        cleanup();  // kill(_pid, SIGKILL); waitpid(_pid, NULL, 0);
    }
}
```

Se verifica en `_cleanupConnections()` tras cada `epoll_wait`.

**Por que SIGKILL y no SIGTERM**: SIGTERM da oportunidad de limpiar, pero los CGI no se espera que hagan limpieza, no tienen estado en el servidor, y un script descontrolado puede ignorar SIGTERM. SIGKILL es inmediato e incapturable. El costo es que el hijo no puede limpiar archivos temporales; aceptable para 42.

### Zombie process

Si el hijo termina y el padre no llama `waitpid`, el hijo queda como zombie. Se maneja en `finishOutputRead()`:

```cpp
pid_t result = waitpid(_pid, &status, WNOHANG);
if (result == 0) {
    // hijo aun corriendo pero su stdout cerro
    kill(_pid, SIGKILL);
    waitpid(_pid, &status, 0);   // blocking wait, seguro tras SIGKILL
}
```

`WNOHANG` (no bloqueante) primero. Si el hijo aun no termino pero su pipe de salida cerro, se le envia SIGKILL. Esto previene que un CGI descontrolado consuma recursos indefinidamente.

### Limite de tamano de output

```cpp
if (_outputBuffer.size() > CGI_MAX_OUTPUT_SIZE) {
    kill(_pid, SIGTERM);
    return -1;
}
```

Previene que un script runaway llene la memoria del servidor.

---

## 7. ConfigParser: DSL estilo NGINX

### Diseno del parser

Parser descendente recursivo sobre sintaxis NGINX:

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
}
```

Fases de tokenizacion:
1. `_skipWhitespace()` y `_skipComments()`: saltan espacios y lineas `#`.
2. `_parseToken()`: extrae palabraclave hasta espacio, `;`, `{`, o `}`.
3. `_parseValue()`: extrae valor hasta `;`, `{`, o `}`, soporta comillas.

La estructura resultante: `vector<ServerConfig>` donde cada `ServerConfig` contiene `vector<RouteConfig>`. Refleja la anidacion del archivo.

### Directivas soportadas

**Nivel `server`:**

| Directiva | Ejemplo | Descripcion |
|-----------|---------|-------------|
| `listen` | `listen 127.0.0.1:8080;` | Direccion y puerto. Sin IP: `0.0.0.0` |
| `server_name` | `server_name localhost;` | Nombre para virtual hosting |
| `client_max_body_size` | `client_max_body_size 10M;` | Limite de body. Sufijos K/M/G |
| `error_page` | `error_page 404 502 /errors/;` | Pagina de error personalizada |
| `keepalive_timeout` | `keepalive_timeout 10;` | Timeout keep-alive en segundos |

**Nivel `location`:**

| Directiva | Ejemplo | Descripcion |
|-----------|---------|-------------|
| `root` | `root www/default;` | Directorio raiz |
| `index` | `index index.html;` | Archivo por defecto en directorio |
| `autoindex` | `autoindex on;` | Listado HTML de directorios |
| `allow_methods` | `allow_methods GET POST;` | Metodos permitidos (405 si no) |
| `redirect` | `redirect /;` | Redireccion 301 |
| `upload_store` | `upload_store www/upload;` | Directorio de uploads |
| `cgi_extension` | `cgi_extension .py /usr/bin/python3;` | Extension -> interprete |
| `try_files` | `try_files $uri $uri/ /404.html;` | Lista de archivos a intentar |
| `auth_basic` | `auth_basic "Area";` | Realm para Basic Auth |
| `auth_basic_user` | `auth_basic_user admin;` | Usuario Basic Auth |
| `auth_basic_password` | `auth_basic_password secret;` | Contrasena Basic Auth |

### Longest-prefix matching de rutas

`ServerConfig::findRoute()` hace longest-prefix match sobre el URI. Si `/cgi-bin/py` y `/cgi-bin` estan definidos, una peticion a `/cgi-bin/py/script.py` matchea `/cgi-bin/py`. Si ninguna location matchea, 404. Se requiere `location /` para handler todos los no matcheados.

### Multi-listen y virtual hosts

```nginx
server {
    listen 8080;
    listen 127.0.0.1:8081;   # segunda bind address, mismo server config
    server_name localhost;
    ...
}
```

Cada `listen` crea un `ServerSocket` separado. Si dos servers escuchan el mismo puerto, el primero es el default. El `Host` header selecciona cual aplica (ver Parte 1, virtual hosting).

---

## 8. ServerConfig y RouteConfig

### ServerConfig

Configuracion de un bloque `server`:
- `listens`: vector de (host, puerto).
- `serverName`: para virtual hosting.
- `clientMaxBodySize`: limite de body (default 1MB).
- `errorPages`: mapa codigo -> path.
- `routes`: vector de `RouteConfig`.
- `keepaliveTimeout`: timeout keep-alive (default 10s).

### RouteConfig

Configuracion de un bloque `location`:
- `path`: prefijo del URI.
- `root`: directorio base.
- `index`: archivo por defecto.
- `autoindex`: on/off.
- `allowedMethods`: vector de metodos.
- `redirect`: path o URL destino.
- `uploadStore`: directorio de uploads.
- `cgiHandlers`: mapa extension -> interprete.
- `tryFiles`: vector de candidatos.
- `authBasic`, `authBasicUser`, `authBasicPassword`: Basic Auth.

---

## 9. StringUtils: seguridad y utilidades

### resolvePath: proteccion contra path traversal

Esta es la funcion mas critica de seguridad del servidor. Mapea un URI a un path del filesystem:

```cpp
std::string StringUtils::resolvePath(const std::string& uri,
                                     const std::string& routePath,
                                     const std::string& root) {
    std::string path = stripQueryString(uri);    // remove ?query

    if (routePath != "/" && startsWith(path, routePath)) {
        if (path.size() > routePath.size())
            path = path.substr(routePath.size()); // strip location prefix
    }

    if (path.empty() || path[0] != '/')
        path = "/" + path;

    std::string fullPath = FileUtils::joinPath(root, path);
    std::string normalizedPath = FileUtils::normalizePath(fullPath);

    if (!startsWith(normalizedPath, root))
        return "";          // path traversal -> 403

    return normalizedPath;
}
```

**Invariante clave**: la normalizacion (`normalizePath`) debe ocurrir ANTES del check `startsWith(normalizedPath, root)`. Si se aplicara al path no-normalizado, un URI como `/uploads/../etc/passwd` pasaria el check (`startsWith("/uploads/../etc/passwd", "/uploads")` es true) pero escaparia del root despues de normalizar.

La query string se strippea antes de cualquier operacion de path. Si no, el `?query` se convierte en parte del filename y los lookups fallan.

Retorna string vacio para path traversal. Todos los callers verifican empty y devuelven 403.

```bash
curl -v "http://localhost:8080/../etc/passwd"
# Esperado: 403 Forbidden
```

### Otras utilidades de StringUtils

- **`decodeUrl`**: decodifica `%20` -> espacio, `%3D` -> `=`, etc.
- **`base64Decode`**: decodificacion Base64 sin librerias. Para Basic Auth.
- **`intToString`**: usa `std::ostringstream` (C++98 no tiene `std::to_string`).
- **`toUpper`, `toLower`, `trim`, `split`**: utilidades de strings.
- **`toHex`**: conversion a hexadecimal para ETags.

---

## 10. FileUtils

Operaciones de filesystem:
- **`readFile(path)`**: lee archivo completo a string.
- **`readFileRange(path, start, length)`**: lee un rango de bytes (para Range requests).
- **`fileExists(path)`**: verifica existencia.
- **`isDirectory(path)`**: verifica si es directorio.
- **`joinPath(a, b)`**: une paths con `/`.
- **`normalizePath(path)`**: resuelve `.` y `..`, colapsa `//`. Preserva naturaleza relativa/absoluta.
- **`getLastModified(path)`**: mtime para `Last-Modified` header.
- **`getParentDirectory(path)`**: directorio padre (para PUT).
- **`writeFile(path, content)`**: escribe contenido a archivo.

---

## 11. Logger

Singleton con niveles DEBUG / INFO / WARN / ERROR.

```cpp
Logger::getInstance().log(LOG_INFO, "Mensaje");
// macro: LOG_INFO("Mensaje")
```

Los niveles se pueden filtrar. En produccion se usan INFO/WARN/ERROR; DEBUG se reserva para desarrollo.

---

## Comandos de demostracion

```bash
# 1. CGI GET con query string
curl -v "http://localhost:8080/cgi-bin/hello.py?name=world"
# Esperado: 200, output muestra QUERY_STRING=name=world

# 2. CGI POST
curl -v -X POST -d "key=value&other=123" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  http://localhost:8080/cgi-bin/echo.py
# Esperado: 200, output muestra el body

# 3. CGI script no encontrado
curl -v http://localhost:8080/cgi-bin/nonexistent.py
# Esperado: 404

# 4. Interprete no instalado
curl -v http://localhost:8080/cgi-bin/info.php
# Esperado: 500 (execve falla)

# 5. Multi-CGI (cambiar de config)
./webserv conf/tests/multi_cgi.conf
curl http://localhost:8080/cgi-bin/py/hello.py     # Python
curl http://localhost:8080/cgi-bin/sh/env.sh        # Shell
curl http://localhost:8080/cgi-bin/php/info.php     # PHP (500 si no hay php-cgi)

# 6. CGI concurrente
seq 5 | xargs -P 5 -I{} curl -s http://localhost:8080/cgi-bin/hello.py | grep -c "CGI"
# Esperado: 5

# 7. Path traversal bloqueado
curl -v "http://localhost:8080/../etc/passwd"
# Esperado: 403

# 8. Configuracion: cambiar config y reiniciar
# Editar conf/default.conf: anyadir un location, cambiar allow_methods, etc.
./webserv conf/default.conf

# 9. CGI con status personalizado
# (crear cgi-bin/teapot.py con "Status: 418" header)
curl -v http://localhost:8080/cgi-bin/teapot.py
# Esperado: 418

# 10. CGI redirect
# (crear cgi-bin/redirect.py con "Location: http://example.com/" header)
curl -v http://localhost:8080/cgi-bin/redirect.py
# Esperado: 302 Location: http://example.com/

# 11. Timeout CGI (crear script infinite.py con while True: sleep(1))
curl -v --max-time 35 http://localhost:8080/cgi-bin/infinite.py
# Esperado: 504 Gateway Timeout despues de ~30s
```

---

## Preguntas frecuentes del evaluador

**P: Por que fork/execve y no threads?**
R: El subject prohíbe threads. Ademas, fork da aislamiento completo: el CGI tiene su propio espacio de direcciones. Un script mal comportado no puede corromper el servidor. La unica comunicacion son los pipes.

**P: Como evitas que el CGI bloquee el event loop?**
R: Los pipes del CGI se registran en el mismo epoll_fd. El event loop sigue sirviendo otras conexiones. Cuando el CGI escribe o termina, epoll notifica y se lee lo disponible sin bloquear. Nunca se hace un `read` bloqueante en el pipe.

**P: Que variables de entorno recibe el CGI?**
R: Todas las requeridas por RFC 3875: `GATEWAY_INTERFACE`, `SERVER_PROTOCOL`, `REQUEST_METHOD`, `SCRIPT_NAME`, `QUERY_STRING`, `CONTENT_TYPE`, `CONTENT_LENGTH`, `SERVER_NAME`, `SERVER_PORT`, `REDIRECT_STATUS`, y cada header HTTP como `HTTP_*`. La conversion es: guiones -> underscores, mayusculas, prefijo `HTTP_`.

**P: Como manejas el timeout del CGI?**
R: 30 segundos, verificado en `_cleanupConnections()` tras cada `epoll_wait`. Si excede, SIGKILL directo (no SIGTERM, porque un script descontrolado puede ignorarlo) + `waitpid` bloqueante (seguro tras SIGKILL). Se devuelve 504 Gateway Timeout.

**P: Como evitas zombies?**
R: Tras leer EOF del pipe de salida, se llama `waitpid(_pid, &status, WNOHANG)`. Si el hijo aun corre pero su stdout cerro, se le envia SIGKILL y se hace `waitpid` bloqueante. Esto reclama el proceso y evita el zombie.

**P: Como parseas el archivo de configuracion?**
R: Parser descendente recursivo. Tokenizo en tres fases (skip whitespace/comments, parse token, parse value). Construyo `vector<ServerConfig>` donde cada uno contiene `vector<RouteConfig>`, reflejando la anidacion NGINX. El parser es estricto: falta de `;` o llaves sin cerrar abortan el arranque.

**P: Como funciona longest-prefix matching de rutas?**
R: `findRoute()` busca la location cuyo `path` es el prefijo mas largo del URI. Si `/cgi-bin/py` y `/cgi-bin` existen, `/cgi-bin/py/script.py` matchea `/cgi-bin/py`. Es consistente con NGINX.

**P: Como proteges contra path traversal?**
R: `resolvePath` normaliza el path (resuelve `..`) ANTES de verificar `startsWith(root)`. Si la normalizacion escapara del root, retorno string vacio y el caller devuelve 403. La query string se strippea antes de cualquier operacion de path. El bug clasico es verificar antes de normalizar: `/uploads/../etc/passwd` pasaria el check pero escaparia del root.

**P: Por que `REDIRECT_STATUS=200`?**
R: PHP-CGI lo requiere por seguridad: sin el, php-cgi se niega a ejecutar. Es una proteccion contra invocacion directa. Lo seteamos siempre para compatibilidad.

**P: Se puede usar fork para otra cosa que no sea CGI?**
R: No. El subject lo dice explicitamente: "You can't use fork for anything other than CGI". Fork solo se usa en `CgiHandler::_forkAndExecute()`.
