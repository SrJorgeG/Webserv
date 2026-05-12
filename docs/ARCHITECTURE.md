# Arquitectura de Webserv

## Vision General

Webserv es un servidor HTTP/1.1 implementado en C++98 que utiliza un patron **Reactor** con **epoll** como mecanismo de I/O multiplexing. El servidor es completamente no-bloqueante y maneja multiples conexiones simultaneas a traves de una unica llamada a `epoll_wait()`.

## Patrones de Diseño

### 1. Reactor Pattern
Un unico objeto `Reactor` central monitorea todos los descriptores de archivo (sockets de escucha y conexiones de clientes) mediante `epoll`. Cuando un descriptor esta listo para lectura o escritura, el Reactor despacha el evento al handler correspondiente.

**Ventajas:**
- Desacopla la logica de I/O de la logica de aplicacion HTTP
- Permite escalar a miles de conexiones concurrentes
- Facilita el testing unitario de cada componente

### 2. State Machine (Maquina de Estados)
Cada conexion (`Connection`) mantiene un estado interno que determina que operacion realizar a continuacion:

```
READING_HEADERS  ->  READING_BODY  ->  PROCESSING  ->  WRITING_RESPONSE  ->  CLOSING
       ^                                                              |
       |______________________________________________________________|
```

- **READING_HEADERS**: Leyendo la linea de request y headers
- **READING_BODY**: Leyendo el cuerpo del request (si aplica)
- **PROCESSING**: El request esta completo, se procesa (rutas, CGI, etc.)
- **WRITING_RESPONSE**: Enviando la respuesta al cliente
- **CLOSING**: La conexion se cierra (limpia o error)

### 3. Strategy Pattern para Metodos HTTP
Los metodos HTTP (GET, POST, DELETE) se implementan como clases concretas que heredan de una interfaz comun `IHttpMethodHandler`. Esto permite anadir metodos facilmente y testearlos de forma aislada.

## Diagrama de Componentes

```
+------------------------------------------------------------------+
|                           webserv                                |
|                                                                  |
|  +-----------------+        +------------------------------+     |
|  |  ConfigParser   |------->|     vector<ServerConfig>     |     |
|  +-----------------+        +------------------------------+     |
|                                                                  |
|  +-----------------+        +------------------------------+     |
|  |     Reactor     |<------>|     epoll instance           |     |
|  |  (epoll_wait)   |        +------------------------------+     |
|  +--------+--------+                                             |
|           |                                                      |
|     +-----+-----+                                                |
|     |           |                                                |
|  +--v---+   +---v----+                                           |
|  |Server|   |Client  |                                           |
|  |Socket|   |Connection                                          |
|  +------+   +---+----+                                           |
|                  |                                               |
|          +-------+-------+                                       |
|          |               |                                       |
|      +---v----+     +----v----+                                  |
|      |Request |     |Response |                                  |
|      +---+----+     +----+----+                                  |
|          |               |                                       |
|      +---v------------+--+                                       |
|      | HttpParser     |                                          |
|      +---+------------+                                          |
|          |                                                       |
|  +-------+-------+-------+                                       |
|  |       |       |       |                                       |
|  |  +----v--+ +--v---+ +-v----+                                  |
|  |  |Get    | |Post  | |Delete|                                  |
|  |  |Handler| |Handler|Handler|                                  |
|  |  +----+--+ +--+---+ +--+---+                                  |
|  |       |       |       |                                       |
|  +-------+-------+-------+                                       |
|          |                                                       |
|      +---v----------+                                            |
|      | CgiHandler   | (solo para fork/execve)                    |
|      +--------------+                                            |
+------------------------------------------------------------------+
```

## Estructura de Clases Principal

### Core (Nucleo del Servidor)

#### `Reactor`
- **Responsabilidad**: Gestionar el ciclo de vida del servidor, el bucle principal de eventos y la instancia de epoll.
- **Metodos principales**:
  - `init()`: Crea la instancia epoll y registra los ServerSockets
  - `run()`: Bucle principal `while(running) { epoll_wait(); dispatchEvents(); }`
  - `registerHandler(int fd, EventHandler* handler, uint32_t events)`
  - `modifyHandler(int fd, uint32_t events)`
  - `removeHandler(int fd)`
- **Notas**: Singleton o referencia pasada a los componentes que lo necesiten.

#### `ServerSocket`
- **Responsabilidad**: Escuchar en un puerto especifico y aceptar nuevas conexiones.
- **Metodos principales**:
  - `bindAndListen(port)`: Crea socket, bind, listen, set non-blocking
  - `acceptConnection()`: Acepta nuevo cliente y crea objeto `Connection`
- **Datos**: Puerto, direccion, backlog, socket fd

#### `Connection` : public EventHandler
- **Responsabilidad**: Representar una conexion TCP con un cliente y gestionar su maquina de estados.
- **Miembros**:
  - `int _clientFd`
  - `ConnectionState _state`
  - `Request _request`
  - `Response _response`
  - `std::string _readBuffer`
  - `std::string _writeBuffer`
  - `size_t _bytesWritten`
  - `time_t _lastActivity` (para timeout)
- **Metodos**:
  - `handleRead()`: Lee del socket al buffer, actualiza estado
  - `handleWrite()`: Escribe del buffer al socket
  - `handleError()`: Gestion de errores y cleanup
  - `processRequest()`: Transicion de READING -> PROCESSING -> WRITING
  - `close()`: Cierra fd y se elimina del Reactor

#### `EventHandler` (Interfaz Abstracta)
```cpp
class EventHandler {
public:
    virtual ~EventHandler() {}
    virtual void handleRead() = 0;
    virtual void handleWrite() = 0;
    virtual void handleError() = 0;
    virtual int getFd() const = 0;
};
```

### HTTP (Protocolo y Logica)

#### `Request`
- **Responsabilidad**: Almacenar los datos parseados de un request HTTP.
- **Miembros**:
  - `std::string _method` (GET, POST, DELETE)
  - `std::string _uri`
  - `std::string _version` (HTTP/1.1)
  - `std::map<std::string, std::string> _headers`
  - `std::string _body`
  - `bool _isChunked`
  - `size_t _contentLength`

#### `Response`
- **Responsabilidad**: Construir y almacenar una respuesta HTTP.
- **Miembros**:
  - `int _statusCode`
  - `std::string _statusMessage`
  - `std::map<std::string, std::string> _headers`
  - `std::string _body`
  - `bool _isReady`
- **Metodos**:
  - `setStatus(int code)`
  - `setHeader(key, value)`
  - `setBody(const std::string& body)`
  - `toString() const`: Serializa a formato HTTP raw

#### `HttpParser`
- **Responsabilidad**: Parsear datos raw del buffer en un objeto `Request`.
- **Metodos**:
  - `parse(const std::string& rawData, Request& outRequest)`: Devuelve `PARSE_OK`, `PARSE_INCOMPLETE`, `PARSE_ERROR`
  - `parseRequestLine()`, `parseHeaders()`, `parseBody()`
- **Notas**: Debe manejar requests parciales (el cliente envia datos en varios paquetes).

#### `IHttpMethodHandler` (Interfaz)
```cpp
class IHttpMethodHandler {
public:
    virtual ~IHttpMethodHandler() {}
    virtual void handle(const Request& request, Response& response, 
                        const RouteConfig& route, const ServerConfig& server) = 0;
};
```

#### `GetHandler`, `PostHandler`, `DeleteHandler`
Implementaciones concretas de `IHttpMethodHandler`.

### Configuracion

#### `ServerConfig`
- **Responsabilidad**: Almacenar la configuracion de un bloque `server {}`.
- **Miembros**:
  - `std::vector<std::pair<std::string, int> > _listen` (host:port)
  - `std::string _serverName`
  - `std::map<int, std::string> _errorPages`
  - `size_t _clientMaxBodySize`
  - `std::vector<RouteConfig> _routes`

#### `RouteConfig`
- **Responsabilidad**: Almacenar la configuracion de un bloque `location {}`.
- **Miembros**:
  - `std::string _path`
  - `std::vector<std::string> _allowedMethods`
  - `std::string _root`
  - `bool _autoindex`
  - `std::string _index`
  - `std::string _redirect`
  - `std::string _uploadStore`
  - `std::map<std::string, std::string> _cgiHandlers` (extension -> interpreter)

#### `ConfigParser`
- **Responsabilidad**: Leer y parsear archivos de configuracion estilo NGINX.
- **Metodos**:
  - `parse(const std::string& filepath)`: Devuelve `vector<ServerConfig>`
  - Validacion sintactica y semantica basica

### CGI

#### `CgiHandler`
- **Responsabilidad**: Ejecutar scripts CGI mediante `fork()` + `execve()`.
- **Metodos**:
  - `execute(const Request& request, const RouteConfig& route, Response& response)`
  - `setupEnvironment()`: Prepara variables de entorno (REQUEST_METHOD, QUERY_STRING, etc.)
  - `readOutput()`: Lee la salida del CGI via pipe
- **Notas**: El unico lugar donde se permite `fork()`. Se gestiona con `pipe()` y el proceso padre lee de forma no-bloqueante.

#### `CgiEnvironment`
- **Responsabilidad**: Construir el array de variables de entorno para CGI.

### Utilidades

#### `FileUtils`
- `fileExists()`, `isDirectory()`, `readFile()`, `writeFile()`, `deleteFile()`

#### `StringUtils`
- `trim()`, `split()`, `toLower()`, `toString()`, `decodeUrl()`

#### `Logger`
- Logging basico a stdout/stderr con timestamps. Nivel: DEBUG, INFO, WARN, ERROR.

## Flujo de Datos de una Peticion

```
1. Navegador envia HTTP request -> socket del cliente
2. epoll_wait() detecta EPOLLIN en el fd del cliente
3. Reactor llama a Connection::handleRead()
4. Connection lee datos en _readBuffer
5. HttpParser intenta parsear el buffer
   - Si incompleto: vuelve a epoll_wait (estado READING_HEADERS/READING_BODY)
   - Si completo: Request listo, transicion a PROCESSING
6. Connection::processRequest():
   a. Busca ServerConfig por puerto/host
   b. Busca RouteConfig que haga match con URI
   c. Valida metodo permitido
   d. Si CGI -> CgiHandler::execute()
   e. Si no -> IHttpMethodHandler correspondiente
   f. Construye Response
   g. Transicion a WRITING_RESPONSE
7. epoll_wait() detecta EPOLLOUT (si el socket permite escritura)
8. Connection::handleWrite() envia _writeBuffer
9. Si se envio todo -> transicion a READING_HEADERS (keep-alive) o CLOSING
```

## Gestion de Errores

### Niveles de Error
1. **Errores de parseo HTTP (400 Bad Request)**: Malformed request
2. **Errores de recurso (404 Not Found)**: Archivo/ruta no existe
3. **Errores de permisos (403 Forbidden)**: Metodo no permitido, acceso denegado
4. **Errores de servidor (500 Internal Server Error)**: Excepciones no controladas, fallos de memoria
5. **Errores de payload (413 Payload Too Large)**: Body excede client_max_body_size

### Excepciones
Usaremos excepciones de C++ para flujo de control de errores internos, pero **nunca para controlar el flujo normal**. Cada componente captura excepciones y las convierte en respuestas HTTP apropiadas.

### Memory Safety
- Uso de `std::vector`, `std::string` en lugar de raw arrays/punteros cuando sea posible
- RAII para file descriptors (`close()` en destructores)
- Manejo cuidadoso de `fork()` + `execve()` en CGI para evitar leaks y zombies (waitpid)

## Consideraciones de Concurrencia

- **Single-threaded**: El servidor es single-threaded con I/O no-bloqueante. No hay mutex ni locks.
- **No blocking**: Nunca se llama a `read()`/`write()`/`recv()`/`send()` sin que epoll indique que el fd esta listo (salvo para archivos regulares en disco, que estan exentos segun el subject).
- **Timeouts**: Implementar timeout de conexion inactiva (ej. 60 segundos). El Reactor debe cerrar conexiones inactivas.

## Compatibilidad C++98

- No usar `auto`, ranged-for, lambdas, `nullptr` (usar `NULL`), `std::to_string` (implementar propio)
- Usar `typename` donde sea necesario en templates
- Preferir `<cstring>` sobre `<string.h>`, `<cstdlib>` sobre `<stdlib.h>`, etc.
- Iteradores estilo C++98: `std::vector<T>::iterator`

## Division de Trabajo (Equipo de 2)

| Persona | Modulos | Responsabilidades |
|---------|---------|-------------------|
| **Persona 1** | `core/`, `config/`, `utils/` | Reactor, epoll, ServerSocket, Connection state machine, parser de configuracion, utilidades, Makefile |
| **Persona 2** | `http/`, `cgi/`, `www/`, `tests/` | Request, Response, HttpParser, handlers HTTP, CgiHandler, sitios de prueba, tests Python |

**Interfaz comun**: La clase `Connection` (Persona 1) necesita llamar a `HttpParser` y a los `IHttpMethodHandler` (Persona 2). Se acuerda la interfaz al inicio.

## Proximos Pasos

1. Revisar y aprobar esta arquitectura
2. Escribir los headers (.hpp) de todas las clases (contratos de interfaz)
3. Implementar Makefile con flags C++98
4. Implementar `Logger` y `StringUtils` (utilidades simples primero)
5. Implementar `ConfigParser` y estructuras de config
6. Implementar `Reactor` + `ServerSocket` + `Connection` basico (echo server)
7. Implementar `HttpParser` + `Request` + `Response`
8. Implementar handlers HTTP + CGI
9. Tests y validacion
