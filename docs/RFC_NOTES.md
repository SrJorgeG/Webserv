# Notas sobre RFCs de HTTP

## RFCs Relevantes

### RFC 7230 - HTTP/1.1: Message Syntax and Routing
**Importancia: ALTA**

Define la sintaxis basica de mensajes HTTP/1.1:
- Formato de request line: `METHOD SP REQUEST-URI SP HTTP-VERSION CRLF`
- Formato de headers: `field-name: field-value CRLF`
- Separacion entre headers y body: `CRLF CRLF`
- Codificacion de transferencia (chunked)
- Conexiones persistentes (keep-alive)

**Puntos clave para implementar:**
```
Request-Line = Method SP Request-URI SP HTTP-Version CRLF
Method = "GET" / "HEAD" / "POST" / "PUT" / "DELETE" / ...
HTTP-Version = "HTTP" "/" 1*DIGIT "." 1*DIGIT
```

### RFC 7231 - HTTP/1.1: Semantics and Content
**Importancia: ALTA**

Define la semantica de metodos y codigos de estado:
- Significado de GET, POST, DELETE
- Codigos de estado y cuando usarlos
- Headers de content negotiation

**Metodos que implementamos:**
- **GET**: Obtener un recurso. Debe ser seguro (no modificar estado) e idempotente.
- **POST**: Enviar datos al servidor. No es idempotente.
- **DELETE**: Eliminar un recurso. Idempotente.

**Codigos de estado comunes:**

| Codigo | Nombre | Cuando usar |
|--------|--------|-------------|
| 200 | OK | Request exitoso |
| 201 | Created | Recurso creado (POST exitoso) |
| 204 | No Content | Request exitoso sin body (DELETE) |
| 301 | Moved Permanently | Redireccion permanente |
| 302 | Found | Redireccion temporal |
| 400 | Bad Request | Request malformado |
| 403 | Forbidden | Acceso denegado |
| 404 | Not Found | Recurso no existe |
| 405 | Method Not Allowed | Metodo no permitido en ruta |
| 413 | Payload Too Large | Body excede limite |
| 500 | Internal Server Error | Error del servidor |
| 502 | Bad Gateway | Error en CGI |
| 504 | Gateway Timeout | CGI tarda demasiado |

### RFC 3875 - CGI Version 1.1
**Importancia: ALTA (para CGI)**

Especifica el protocolo CGI:
- Variables de entorno requeridas
- Formato de entrada y salida
- Manejo de PATH_INFO, QUERY_STRING

### RFC 6265 - HTTP State Management Mechanism (Cookies)
**Importancia: MEDIA (para bonus)**

Define como funcionan las cookies:
- Header `Set-Cookie` en respuesta
- Header `Cookie` en request
- Atributos: `Path`, `Domain`, `Max-Age`, `HttpOnly`

### RFC 7578 - Returning Values from Forms: multipart/form-data
**Importancia: MEDIA (para uploads)**

Define el formato `multipart/form-data` usado en formularios con archivos.

## Decisiones de Diseno basadas en RFCs

### Keep-Alive / Conexiones Persistentes

HTTP/1.1 usa conexiones persistentes por defecto a menos que se especifique `Connection: close`.

**Comportamiento de nuestro servidor:**
- Por defecto, mantener conexion abierta despues de enviar respuesta
- Si request tiene `Connection: close`, cerrar despues de responder
- Si response tiene `Connection: close`, cerrar despues de enviar
- Timeout de keep-alive: 60 segundos de inactividad

### Chunked Transfer-Encoding

Cuando no conocemos el tamano del body de antemano (ej: salida de CGI), usamos:

```
Transfer-Encoding: chunked

5\r\n
hello\r\n
0\r\n
\r\n
```

**Nuestro servidor debe:**
- Aceptar requests con `Transfer-Encoding: chunked`
- Des-chunkear el body antes de enviarlo al CGI
- Poder enviar responses chunked (opcional pero util)

### Headers Importantes a Manejar

**Request headers:**
- `Host`: Obligatorio en HTTP/1.1. Seleccionar virtual host.
- `Content-Length`: Tamano del body.
- `Content-Type`: Tipo MIME del body (importante para POST).
- `Transfer-Encoding`: `chunked`.
- `Connection`: `keep-alive` o `close`.
- `Cookie`: Para sesiones (bonus).

**Response headers:**
- `Content-Type`: Tipo MIME de la respuesta.
- `Content-Length`: Tamano del body (si se conoce).
- `Connection`: `keep-alive` o `close`.
- `Location`: Para redirecciones (301, 302).
- `Set-Cookie`: Para sesiones (bonus).
- `Date`: Fecha y hora actual (formato RFC 7231).
- `Server`: Identificacion del servidor (`webserv/1.0`).

### Formato de Fecha HTTP

Las fechas en HTTP usan el formato:
```
Date: <day-name>, <day> <month> <year> <hour>:<minute>:<second> GMT

Ejemplo: Date: Mon, 11 May 2026 14:30:00 GMT
```

Funcion util en C++:
```cpp
std::string getHttpDate() {
    time_t now = time(NULL);
    struct tm* tm = gmtime(&now);
    char buf[100];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tm);
    return std::string(buf);
}
```

### URL Encoding

Los caracteres especiales en URLs se codifican como `%XX`:
- Espacio: `%20` o `+`
- `&`: `%26`
- `=`: `%3D`
- etc.

El servidor debe decodificar URIs antes de buscar archivos en disco.

## Simplificaciones Permitidas

El subject indica que no es necesario implementar todo el RFC. Simplificaciones validas:

- **HTTP/1.0 como referencia**: Podemos asumir comportamiento HTTP/1.0 si simplifica
- **Sin virtual hosts complejos**: `server_name` no es obligatorio para el matching basico
- **Sin pipeline**: No es necesario soportar multiple requests pendientes en una conexion
- **Sin range requests**: No soportar `Range: bytes=0-1024`
- **Sin autenticacion**: No implementar Basic/Digest auth
- **Sin compression**: No implementar gzip/deflate

## Referencias Rapidas

### Linea de Request
```
GET /path?query=value HTTP/1.1\r\n
```

### Headers tipicos de Request
```
Host: localhost:8080\r\n
User-Agent: Mozilla/5.0\r\n
Accept: text/html\r\n
Connection: keep-alive\r\n
Content-Length: 123\r\n
Content-Type: application/x-www-form-urlencoded\r\n
\r\n
```

### Response de ejemplo
```
HTTP/1.1 200 OK\r\n
Date: Mon, 11 May 2026 14:30:00 GMT\r\n
Server: webserv/1.0\r\n
Content-Type: text/html\r\n
Content-Length: 123\r\n
Connection: keep-alive\r\n
\r\n
<html>...</html>
```

## Recursos Adicionales

- **MDN HTTP**: https://developer.mozilla.org/en-US/docs/Web/HTTP
- **HTTP Status Codes**: https://httpstatuses.com/
- **Real Python HTTP Guide**: https://realpython.com/python-http-server/
- **Beej's Guide to Network Programming**: https://beej.us/guide/bgnet/

## Tests con NGINX

Antes de implementar una feature, testear como se comporta NGINX:

```bash
# Instalar nginx
sudo apt-get install nginx

# Configuracion basica para comparar
# /etc/nginx/sites-available/default
server {
    listen 80;
    root /var/www/html;
    index index.html;
    location / {
        try_files $uri $uri/ =404;
    }
}

# Lanzar nginx
sudo nginx

# Comparar comportamientos
curl -v http://localhost/
curl -v -X POST -d "test" http://localhost/
```
