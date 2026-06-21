# Parte 2 - Protocolo HTTP & Handlers

**Companero B**
**Archivos:** `src/http/HttpParser.cpp`, `src/http/Request.cpp`, `src/http/Response.cpp`, `src/http/GetHandler.cpp`, `src/http/PostHandler.cpp`, `src/http/DeleteHandler.cpp`, `src/http/PutHandler.cpp`, `src/http/HeadHandler.cpp`, `src/http/OptionsHandler.cpp`, `src/http/SessionManager.cpp`, `src/http/StatusCodes.cpp`
**Headers:** `include/http/*.hpp`

Esta capa implementa el protocolo HTTP/1.1: parsing incremental, todos los metodos, upload de archivos, chunked encoding, sesiones, y features extra (Range, ETag, Basic Auth, try_files).

---

## 1. HttpParser: parsing incremental

### El problema del framing TCP

TCP es un stream protocol. No garantiza que una peticion HTTP completa llegue en un solo `recv()`. Los datos pueden llegar:
- **Fraccionados**: una peticion en multiples `recv()` (TCP la fragmento).
- **Pegados**: multiples peticiones en un solo `recv()` (Nagle las junto).
- **Parciales**: headers incompletos, body a medias.

### Diseno incremental

`HttpParser` se llama repetidamente con datos parciales. Mantiene un buffer interno y flags de estado:

```cpp
ParseResult HttpParser::parse(const std::string& rawData, Request& outRequest) {
    _buffer.append(rawData);  // acumula todo

    if (!_requestLineComplete)
        result = _parseRequestLine(outRequest);  // busca \r\n
    if (!_headersComplete)
        result = _parseHeaders(outRequest);      // busca \r\n\r\n
    // si tiene Content-Length, espera tener suficientes bytes
    // si es chunked, parsea los chunks uno a uno
}
```

Tres valores de retorno:
- **`PARSE_OK`**: peticion completa. El handler puede procesarla.
- **`PARSE_INCOMPLETE`**: faltan datos. Seguir leyendo del socket.
- **`PARSE_ERROR`**: peticion mal formada. Responder 400.

Ejemplo de como llega una peticion en trozos:

```
"GET /index.ht"   -> PARSE_INCOMPLETE (request line incompleta)
"ml HTTP/1.1\r\n" -> PARSE_INCOMPLETE (headers incompletos)
"Host: localh"    -> PARSE_INCOMPLETE
"ost\r\n\r\n"     -> PARSE_OK
```

---

## 2. Pipelining HTTP y leftover data

HTTP/1.1 permite pipelining: enviar multiples peticiones sin esperar la respuesta de cada una. El buffer puede contener:

```
[HTTPRequest1][HTTPRequest2][...fragmento de HttpRequest3...]
```

Cuando `parse()` devuelve `PARSE_OK`, el buffer puede tener datos de la siguiente peticion:

```cpp
if (result == PARSE_OK) {
    _readBuffer = _parser.getLeftoverData();  // preserva datos de la siguiente
    _parser.reset();
    // ... procesa la peticion actual ...
}
```

Despues de enviar la respuesta en keep-alive, el estado regresa a `READING_HEADERS` y el leftover ya esta en `_readBuffer` esperando a ser parseado sin necesidad de otro `recv`.

```bash
# Demo pipelining
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
# Esperado: dos respuestas HTTP completas en el stream TCP
```

---

## 3. Chunked transfer encoding

Para peticiones con `Transfer-Encoding: chunked`, el parser reensambla el body:

```
5\r\n
Hello\r\n
6\r\n
 World\r\n
0\r\n
\r\n
```

Cada chunk: tamano en hexadecimal, `\r\n`, datos, `\r\n`. El chunk final tiene tamano 0. `_parseChunkedBody()` procesa los chunks incrementalmente: si el buffer no tiene un chunk completo, retorna `PARSE_INCOMPLETE`.

**Importante**: el body des-ensamblado (no el formato chunked en crudo) es lo que reciben los handlers y el CGI. El subject dice: "for chunked requests, your server needs to un-chunk them, the CGI will expect EOF as the end of the body."

```bash
curl -v -X POST -H "Transfer-Encoding: chunked" --data-binary "hello chunked" http://localhost:8080/uploads/
# Esperado: 201
```

---

## 4. Request y Response: estructuras de datos

### Request

Campos: `method`, `uri`, `httpVersion`, `headers` (map), `body`, `queryString`, `cookies`.

Metodos clave:
- `getHeader(name)`: busca header case-insensitive.
- `getCookie(name)`: busca cookie por nombre.
- `getQueryString()`: parte despues de `?` en el URI.

### Response

Campos: `status`, `headers` (map), `body`, `cookies`.

Metodos clave:
- `setStatus(code)`: setea el codigo de estado.
- `setHeader(name, value)`: anyade/modifica un header.
- `setCookie(name, value)`: anyade cookie a la respuesta.
- `toString()`: serializa la respuesta completa a wire format HTTP.
- `buildError(code, errorPages, root)`: construye una respuesta de error, usando pagina personalizada si existe, o HTML por defecto.
- `clearBody()`: borra el body sin tocar los headers (usado por HEAD).

### StatusCodes

Mapa de codigo -> frase: 200 -> "OK", 404 -> "Not Found", etc.

---

## 5. Patron Strategy: IHttpMethodHandler

```cpp
class IHttpMethodHandler {
public:
    virtual ~IHttpMethodHandler() {}
    virtual void handle(const Request& request, Response& response,
                        const RouteConfig& route, const ServerConfig& server) = 0;
};
```

Cada metodo HTTP tiene su propio handler:
- `GetHandler`, `PostHandler`, `DeleteHandler`, `PutHandler`, `HeadHandler`, `OptionsHandler`

`Connection` no sabe la logica de GET vs POST. Segun el metodo, instancia el handler apropiado y llama `handle()`. Esto desacopla el dispatch de la implementacion de cada metodo.

---

## 6. GetHandler: mas que servir archivos

`GetHandler` implementa varias features:

### 6.1 Resolucion de path

Combina el URI de la peticion con `path` (prefijo del location) y `root` (directorio base). Decodifica la URL (`%20` -> espacio). Llama a `StringUtils::resolvePath()` que normaliza y verifica path traversal.

### 6.2 try_files

Si la directiva `try_files` esta configurada, prueba los paths en orden. El token `$uri` se reemplaza por el path de la peticion. El ultimo entry es un fallback:

```nginx
location /app {
    root www/default;
    try_files $uri $uri/ /index.html;
}
```

Esto hace que las Single Page Applications (React/Vue/Angular) funcionen: toda URL profunda sirve `index.html` para que JavaScript enrute.

### 6.3 Redireccion de directorios sin `/`

Si el path resuelve a un directorio y el URI no tiene `/` al final, se envia 301 redirect a `URI + "/"`. Esto evita problemas con rutas relativas en el HTML.

```bash
curl -v http://localhost:8080/uploads
# Esperado: 301 Location: /uploads/
```

### 6.4 Autoindex

Si `autoindex on` y no hay archivo `index`, se genera un listado HTML con links a cada archivo del directorio.

### 6.5 Range requests (206 Partial Content) - EXTRA

Permite descargar un rango de bytes de un archivo. Usado por video players (seek), download managers (resume), CDNs.

Formatos soportados:
- `bytes=0-1023`: primeros 1024 bytes
- `bytes=1024-`: desde byte 1024 hasta el final
- `bytes=-512`: ultimos 512 bytes

Si el rango esta fuera de limite: 416 Range Not Satisfiable.

```bash
curl -v -H "Range: bytes=0-1023" http://localhost:8080/uploads/Makefile
# Esperado: 206, Content-Range: bytes 0-1023/<size>
```

### 6.6 ETag y conditional requests (304) - EXTRA

ETag = `"<mtime_hex>-<size_hex>"` (mismo formato que Apache httpd). Soporta:
- `If-None-Match`: 304 si el ETag coincide (o `*` siempre coincide).
- `If-Modified-Since`: 304 si el archivo no ha cambiado desde la fecha del cliente.

```bash
ETAG=$(curl -sI http://localhost:8080/index.html | grep -i etag | awk '{print $2}' | tr -d '\r')
curl -v -H "If-None-Match: $ETAG" http://localhost:8080/index.html
# Esperado: 304 Not Modified, sin body
```

### 6.7 Path traversal protection

`StringUtils::resolvePath` normaliza el path y rechaza secuencias como `..` que escaparian del root. Ver Parte 3 para detalles.

---

## 7. PostHandler: upload multipart/form-data

`PostHandler` maneja `multipart/form-data`:

1. Extrae el `boundary` del header `Content-Type`.
2. Parsea las partes del body buscando el campo `filename`.
3. Escribe el contenido al directorio `upload_store`.

El boundary puede estar entre comillas o sin comillas. El parser maneja CRLF y LF.

### POST raw (sin multipart)

Si el `Content-Type` no es multipart, se guarda el body raw con un nombre autogenerado `upload_<timestamp>.txt`.

```bash
# Upload multipart
curl -v -X POST -F "file=@Makefile" http://localhost:8080/uploads/
# Esperado: 201

# Upload raw
curl -v -X POST -H "Content-Type: text/plain" --data-binary "hello" http://localhost:8080/uploads/
# Esperado: 201, archivo upload_<timestamp>.txt
```

### 413 Payload Too Large

Se verifica en dos puntos:
- Cuando `Content-Length` excede `client_max_body_size`: 413 inmediato sin leer el body.
- Cuando el buffer acumulado excede el limite (ej: chunked): se limpia el buffer y 413.

---

## 8. DeleteHandler

Elimina el archivo del `upload_store`. Verifica:
1. Que el archivo exista (sino 404).
2. Que el metodo este permitido en el location (sino 405).
3. Resolucion de path segura (path traversal protection).

```bash
curl -v -X DELETE http://localhost:8080/uploads/Makefile
# Esperado: 204
```

---

## 9. PutHandler - EXTRA

PUT crea o reemplaza un recurso. Es idempotente: el mismo PUT dos veces deja el mismo estado.

| Condicion | Codigo |
|-----------|--------|
| Archivo no existia, creado | 201 + `Location` header |
| Archivo existia, reemplazado | 200 |
| Target es directorio | 409 Conflict |
| Directorio padre no existe | 409 Conflict |
| Path traversal | 403 |

```bash
curl -v -X PUT http://localhost:8080/uploads/test.txt -H "Content-Type: text/plain" --data-binary "hello"
# Esperado: 201, Location: /uploads/test.txt
curl http://localhost:8080/uploads/test.txt
# Esperado: "hello"
```

---

## 10. HeadHandler - EXTRA

HEAD es identico a GET pero sin body. RFC 9110: todos los headers (`Content-Length`, `Content-Type`, `ETag`, `Last-Modified`) deben ser identicos a los de GET.

```cpp
void HeadHandler::handle(...) {
    GetHandler get;
    get.handle(request, response, route, server);  // ejecuta GET completo
    response.clearBody();                           // borra solo el body
}
```

Por que ejecutar GET completo y despues borrar el body? Porque si GET devolveria 403 o 404, el body es una pagina de error de tamano no trivial. Calcular ese `Content-Length` sin ejecutar GET requeriria duplicar toda la logica. Ejecutar GET y descartar el body es la unica forma correcta para todos los codigos de respuesta.

```bash
curl -v -X HEAD http://localhost:8080/index.html
# Esperado: 200, Content-Length presente, 0 bytes de body
```

---

## 11. OptionsHandler - EXTRA

OPTIONS devuelve el header `Allow` con los metodos permitidos. Usado por browsers para CORS preflight.

```cpp
std::string allow = "OPTIONS";
for (size_t i = 0; i < allowed.size(); ++i) {
    allow += ", " + allowed[i];
    if (allowed[i] == "GET") hasGet = true;
}
if (hasGet) allow += ", HEAD";  // HEAD siempre disponible si GET lo esta
```

`Content-Length: 0` es requerido por HTTP/1.1 para que los clientes keep-alive no esperen un body.

```bash
curl -v -X OPTIONS http://localhost:8080/
# Esperado: 200, Allow: OPTIONS, GET, POST, HEAD
```

---

## 12. SessionManager (bonus)

Gestion de sesiones via cookie `webserv_session_id`. Singleton con mapa ID -> `Session`.

```cpp
struct Session {
    std::string id;
    std::map<std::string, std::string> data;
    time_t createdAt;
    time_t lastAccessed;
};
```

Flujo por peticion:
1. Extraer cookie `webserv_session_id` del header `Cookie`.
2. Si no existe o no es valida, crear nueva sesion.
3. Setear cookie en la respuesta.
4. Cada 300 iteraciones del event loop, `cleanExpired()` elimina sesiones con mas de 30 min de inactividad.

El ID se genera con `rand()` + timestamp -> hexadecimal. No es criptograficamente seguro, pero adecuado para un proyecto educativo.

```bash
curl -v http://localhost:8080/ 2>&1 | grep -i set-cookie
# Esperado: Set-Cookie: webserv_session_id=<id>; Path=/; HttpOnly
```

---

## 13. Codigos de estado implementados

| Codigo | Condicion |
|--------|-----------|
| 200 | Respuesta exitosa |
| 201 | Archivo creado (POST upload, PUT nuevo) |
| 204 | No Content (DELETE exitoso) |
| 206 | Partial Content (Range request) |
| 301 | Redirect permanente (directiva `redirect` o directorio sin `/`) |
| 302 | CGI redirect (header `Location` sin status explicito) |
| 304 | Not Modified (ETag/If-None-Match o If-Modified-Since) |
| 400 | Bad Request (peticion mal formada) |
| 401 | Unauthorized (Basic Auth) |
| 403 | Forbidden (path traversal, directorio sin autoindex) |
| 404 | Not Found |
| 405 | Method Not Allowed |
| 409 | Conflict (PUT a directorio) |
| 413 | Payload Too Large |
| 416 | Range Not Satisfiable |
| 500 | Internal Server Error |
| 504 | Gateway Timeout (CGI) |

### Paginas de error personalizadas

```nginx
error_page 404 /errors/404.html;
error_page 500 502 503 504 /errors/50x.html;
```

`Response::buildError()` verifica si hay pagina personalizada. Si existe, la sirve; si no, genera HTML por defecto. La pagina por defecto **siempre** esta disponible, no depende del filesystem.

---

## 14. Basic Auth (extra)

Implementado en `Connection::_checkBasicAuth()`:

1. Extraer header `Authorization`.
2. Verificar que empieza con `Basic `.
3. Decodificar Base64 (sin librerias, implementacion propia en `StringUtils`).
4. Separar en `usuario:contrasena`.
5. Comparar con `auth_basic_user` y `auth_basic_password` de la config.

Si falla: 401 + header `WWW-Authenticate: Basic realm="..."`.

```nginx
location /private {
    auth_basic "Restricted Area";
    auth_basic_user admin;
    auth_basic_password secret123;
}
```

```bash
curl -v http://localhost:8080/private/
# Esperado: 401, WWW-Authenticate: Basic realm="Restricted Area"
curl -v -u admin:secret123 http://localhost:8080/private/
# Esperado: 200
```

---

## Comandos de demostracion

```bash
# 1. GET estatico
curl -v http://localhost:8080/
# Esperado: 200

# 2. 404
curl -v http://localhost:8080/nonexistent.html
# Esperado: 404

# 3. 405 metodo no permitido
curl -v -X DELETE http://localhost:8080/
# Esperado: 405 con header Allow

# 4. Upload multipart
curl -v -X POST -F "file=@Makefile" http://localhost:8080/uploads/
# Esperado: 201

# 5. Ver upload
curl http://localhost:8080/uploads/
# Esperado: autoindex con "Makefile"

# 6. DELETE
curl -v -X DELETE http://localhost:8080/uploads/Makefile
# Esperado: 204

# 7. Chunked
curl -v -X POST -H "Transfer-Encoding: chunked" --data-binary "hello chunked" http://localhost:8080/uploads/
# Esperado: 201

# 8. Range request
dd if=/dev/urandom bs=1K count=10 > /tmp/10k.bin
curl -X PUT http://localhost:8080/uploads/10k.bin --data-binary @/tmp/10k.bin
curl -v -H "Range: bytes=0-1023" http://localhost:8080/uploads/10k.bin
# Esperado: 206

# 9. ETag / 304
ETAG=$(curl -sI http://localhost:8080/index.html | grep -i etag | awk '{print $2}' | tr -d '\r')
curl -v -H "If-None-Match: $ETAG" http://localhost:8080/index.html
# Esperado: 304

# 10. Sesion
curl -v http://localhost:8080/ 2>&1 | grep -i set-cookie
# Esperado: Set-Cookie: webserv_session_id=...

# 11. 413 body too large
dd if=/dev/zero bs=1M count=15 2>/dev/null | curl -v -X POST -H "Content-Length: 15728640" --data-binary @- http://localhost:8080/uploads/
# Esperado: 413

# 12. HEAD
curl -v -X HEAD http://localhost:8080/index.html
# Esperado: 200, Content-Length presente, sin body

# 13. OPTIONS
curl -v -X OPTIONS http://localhost:8080/
# Esperado: 200, Allow header
```

---

## Preguntas frecuentes del evaluador

**P: Como manejas peticiones TCP fraccionadas?**
R: HttpParser es incremental. Acumula en un buffer interno y parsea con flags de estado (`_requestLineComplete`, `_headersComplete`). Retorna `PARSE_INCOMPLETE` si faltan datos y epoll re-notificara.

**P: Como funciona el pipelining?**
R: Cuando `parse()` devuelve `PARSE_OK`, el buffer puede tener datos de la siguiente peticion. `getLeftoverData()` los extrae y se guardan en `_readBuffer`. Tras responder en keep-alive, el estado vuelve a `READING_HEADERS` y el leftover se procesa sin nuevo `recv`.

**P: Por que HEAD ejecuta GET completo?**
R: Porque si GET devolveria 403/404, el body es una pagina de error. Calcular ese `Content-Length` sin ejecutar GET requeriria duplicar toda la logica. Ejecutar GET y borrar el body con `clearBody()` es correcto para todos los codigos.

**P: Como calculas el ETag sin libcrypto?**
R: ETag = `"<mtime_hex>-<size_hex>"`. Usa mtime y size del `stat()` del archivo. Es lo que usa Apache httpd por defecto. MD5 seria mas resistente a colisiones pero requeriria libcrypto o implementar MD5 desde cero.

**P: Como proteges contra path traversal?**
R: `StringUtils::resolvePath` normaliza el path (resuelve `..`) ANTES de verificar `startsWith(root)`. Si la normalizacion escapara del root, la verificacion falla y se retorna 403. Ver Parte 3 para detalles.

**P: Como se separa el body del CGI en headers y contenido?**
R: Se busca `\r\n\r\n` (o `\n\n` como fallback). El header `Status:` setea el codigo HTTP. `Location` sin body dispara 302. Ver Parte 3 para detalles del CGI.
