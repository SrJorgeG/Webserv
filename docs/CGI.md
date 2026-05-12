# Common Gateway Interface (CGI)

## Que es CGI

CGI (Common Gateway Interface) es un protocolo estandar que permite a un servidor web ejecutar programas externos (scripts) y devolver su salida como respuesta HTTP. Es la forma en que servidores como NGINX y Apache ejecutan PHP, Python, Perl, etc.

En nuestro proyecto, CGI se usa para:
- Ejecutar scripts Python (`.py`)
- Ejecutar scripts PHP (`.php`) - bonus
- Procesar formularios dinamicos
- Gestionar sesiones (bonus)

## Flujo de Ejecucion CGI

```
1. Request llega a un location configurado con cgi_extension
2. Servidor identifica el interprete (ej: /usr/bin/python3)
3. Servidor crea un pipe() para comunicacion
4. Servidor hace fork()
5. Proceso hijo:
   a. Redirige stdin al pipe de entrada (si hay body)
   b. Redirige stdout al pipe de salida
   c. Prepara variables de entorno
   d. Cambia al directorio correcto (chdir)
   e. Ejecuta execve(interprete, [script_path, args], env)
6. Proceso padre:
   a. Escribe el body del request al pipe de entrada (si aplica)
   b. Lee la salida del CGI del pipe de salida (no-bloqueante)
   c. Espera al hijo con waitpid() (no-bloqueante, WNOHANG)
   d. Parsea la salida del CGI en una respuesta HTTP
```

## Variables de Entorno CGI

El servidor debe proporcionar estas variables de entorno al script CGI:

### Variables Obligatorias (RFC 3875)

| Variable | Descripcion | Ejemplo |
|----------|-------------|---------|
| `REQUEST_METHOD` | Metodo HTTP | `GET`, `POST`, `DELETE` |
| `QUERY_STRING` | Parametros URL (todo despues de `?`) | `name=value&foo=bar` |
| `CONTENT_LENGTH` | Tamano del body en bytes | `1234` |
| `CONTENT_TYPE` | Tipo MIME del body | `application/x-www-form-urlencoded` |
| `SCRIPT_NAME` | Ruta del script en el URL | `/cgi-bin/hello.py` |
| `PATH_INFO` | Informacion de ruta extra despues del script | `/extra/path` |
| `PATH_TRANSLATED` | PATH_INFO traducido a ruta fisica | `/var/www/extra/path` |
| `SERVER_NAME` | Nombre del servidor | `localhost` |
| `SERVER_PORT` | Puerto del servidor | `8080` |
| `SERVER_PROTOCOL` | Version HTTP | `HTTP/1.1` |
| `GATEWAY_INTERFACE` | Version CGI | `CGI/1.1` |
| `REMOTE_ADDR` | IP del cliente | `127.0.0.1` |

### Variables Adicionales Utiles

| Variable | Descripcion |
|----------|-------------|
| `HTTP_*` | Todas las cabeceras HTTP del request convertidas a variables de entorno. Ej: `HTTP_USER_AGENT`, `HTTP_COOKIE` |
| `REDIRECT_STATUS` | Requerido por PHP-CGI | `200` |

## Salida del CGI

El script CGI puede devolver la respuesta de dos formas:

### 1. Con Content-Length (respuesta parseada)

```
Content-Type: text/html\r\n
Content-Length: 42\r\n
\r\n
<html><body>Hello from CGI</body></html>
```

El servidor parsea los headers y el body, y construye un Response HTTP completo.

### 2. Sin Content-Length (EOF marca el final)

```
Content-Type: text/html\r\n
\r\n
<html><body>Hello from CGI</body></html>
```

Si no hay `Content-Length`, el servidor lee hasta EOF (el pipe se cierra cuando el proceso CGI termina).

**Nota importante**: El CGI **no** debe generar la linea de status HTTP (`HTTP/1.1 200 OK`). Eso lo hace el servidor. Sin embargo, algunos CGIs devuelven un header `Status:` que el servidor debe interpretar.

## Chunked Requests

Si el request del cliente usa `Transfer-Encoding: chunked`, el servidor debe:
1. Des-chunkear el body completo antes de enviarlo al CGI
2. El CGI espera EOF en stdin como senal de fin de body
3. Establecer `CONTENT_LENGTH` con el tamano total des-chunkeado

## Implementacion Tecnica

### Clase CgiHandler

```cpp
class CgiHandler {
public:
    CgiHandler();
    ~CgiHandler();

    // Ejecuta el CGI y llena la response
    void execute(const Request& request, 
                 const RouteConfig& route,
                 Response& response);

private:
    void setupPipes();
    void setupEnvironment(const Request& request, const RouteConfig& route);
    void forkAndExecute(const std::string& interpreter, 
                        const std::string& scriptPath);
    void writeBodyToCgi(const std::string& body);
    void readOutputFromCgi(Response& response);
    void waitForChild();
    void cleanup();

    int _inputPipe[2];   // Padre -> Hijo (body del request)
    int _outputPipe[2];  // Hijo -> Padre (respuesta del CGI)
    pid_t _pid;
    std::vector<char*> _envp; // Variables de entorno para execve
};
```

### Gestion de Procesos

**Timeout de CGI**: Si el script CGI tarda mas de X segundos (ej: 30s), el servidor debe:
1. Enviar `SIGKILL` al proceso hijo
2. Devolver `504 Gateway Timeout`
3. Limpiar pipes y estado

**Zombies**: Usar `signal(SIGCHLD, SIG_IGN)` o `waitpid()` con `WNOHANG` periodicamente para evitar procesos zombie.

**No-bloqueante**: La lectura del pipe de salida del CGI debe hacerse de forma no-bloqueante. El proceso es:
1. `fork()` y `execve()`
2. Registrar el pipe de salida en epoll (EPOLLIN)
3. Cuando epoll indica datos disponibles, leer con `read()`
4. Cuando el pipe cierra (EOF) o hay error, `waitpid()` y finalizar

### Ejemplo de Script CGI en Python

```python
#!/usr/bin/env python3
import os
import sys

# Leer variables de entorno
method = os.environ.get('REQUEST_METHOD', 'GET')
query = os.environ.get('QUERY_STRING', '')

# Generar respuesta
print("Content-Type: text/html")
print("")
print("<html><body>")
print(f"<h1>Hello from CGI!</h1>")
print(f"<p>Method: {method}</p>")
print(f"<p>Query: {query}</p>")
print("</body></html>")
```

### Script de Upload (ejemplo)

```python
#!/usr/bin/env python3
import os
import sys

content_length = int(os.environ.get('CONTENT_LENGTH', 0))
upload_dir = os.environ.get('UPLOAD_DIR', '/tmp/uploads')

if content_length > 0:
    data = sys.stdin.read(content_length)
    # Parsear multipart/form-data...
    # Guardar archivo en upload_dir
    print("Content-Type: text/plain")
    print("")
    print("Upload successful")
else:
    print("Content-Type: text/plain")
    print("Status: 400")
    print("")
    print("No data received")
```

## Notas de Seguridad

1. **Validar rutas**: Asegurarse de que el script a ejecutar esta dentro del `root` permitido
2. **Sanitizar PATH_INFO**: No permitir path traversal (`../`)
3. **Limitar recursos**: Considerar limitar el tiempo de ejecucion y memoria del CGI
4. **Variables de entorno**: No pasar variables sensibles del servidor al CGI

## Limitaciones del Subject

- `fork()` solo se permite para CGI
- El servidor debe seguir siendo no-bloqueante incluso durante la ejecucion de CGI
- No se permite `execve` de otro servidor web

## Scripts de Prueba Recomendados

Crear en `cgi-bin/`:
1. `hello.py` - CGI basico que muestra variables de entorno
2. `upload.py` - Procesa uploads de archivos
3. `session_test.py` - Demuestra manejo de cookies/sesiones (bonus)
4. `infinite.py` - Script que nunca termina (para testear timeout)
5. `error.py` - Script que devuelve errores intencionales
