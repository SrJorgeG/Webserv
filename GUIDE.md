# Webserv - Guia General del Proyecto

Servidor HTTP/1.1 escrito en C++98. Single-threaded, non-blocking, event-driven mediante `epoll` en Linux. Soporta conexiones concurrentes, CGI via `fork`/`execve`, y I/O de pipes no bloqueante integrado en el mismo event loop.

---

## Diagrama de Componentes

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                                     main                                         │
│                          ConfigParser::parse(argv)                               │
│                                Reactor::init()                                   │
│                                Reactor::run()                                     │
└─────────────────────────────┬───────────────────────────────────────────────────┘
                              │
                              │  vector<ServerConfig>
                              ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              Reactor (core)                                      │
│                                                                                  │
│  epoll_fd unico — todos los fds se registran aqui                               │
│                                                                                  │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐               │
│  │  ServerSocket    │  │   Connection     │  │   Connection     │  ...          │
│  │  (EventHandler)  │  │   (EventHandler) │  │   (EventHandler) │               │
│  │                  │  │                  │  │                  │               │
│  │  handleRead()    │  │  handleRead()     │  │  handleRead()    │               │
│  │   └─ accept()    │  │  handleWrite()    │  │  handleWrite()   │               │
│  │                  │  │  handleError()    │  │  handleError()   │               │
│  └──────────────────┘  └────────┬─────────┘  └──────────────────┘               │
│                                 │                                                │
│         epoll_wait() ◄──────────┴──────────► despacha eventos                   │
│         _cleanupConnections() tras cada ciclo                                   │
│            - CGI timeout                                                         │
│            - conexiones expiradas                                                │
│            - estado CLOSING                                                      │
└─────────────────────────────┬───────────────────────────────────────────────────┘
                              │
           ┌──────────────────┼──────────────────┐
           │                  │                  │
           ▼                  ▼                  ▼
  ┌─────────────────┐ ┌──────────────┐  ┌──────────────┐
  │  HttpParser     │ │   Request     │  │   Response    │
  │                 │ │               │  │               │
  │ parse()         │ │ method, uri,  │  │ status,       │
  │ _parseRequestLine│ │ headers, body │  │ headers, body │
  │ _parseHeaders   │ │ cookies,      │  │ cookies,      │
  │ _parseChunked  │ │ queryString   │  │ toString()    │
  └────────┬────────┘ └──────┬───────┘  └──────────────┘
           │                  │
           │    Connection::_processRequest()
           │                  │
           ▼                  ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │                  HttpMethodHandlers (Strategy Pattern)          │
  │                                                                 │
  │   ┌─────────────────────┐                                       │
  │   │ IHttpMethodHandler  │  <<interface>>                         │
  │   └─────────┬───────────┘                                       │
  │             │                                                   │
  │   ┌─────────┴─────────────────────────────────────┐            │
  │   │        │          │         │        │         │            │
  │   ▼        ▼          ▼         ▼        ▼         ▼            │
  │ GetHandler  PostHandler DeleteHandler HeadHandler PutHandler    │
  │   │            │          │                              │      │
  │   │     upload_store     &&                             │      │
  │   │     multipart         │                              │      │
  │   │                      ▼                              │      │
  │   │               CgiHandler                          │      │
  │   │               fork/execve                        │      │
  │   │               pipe[0] stdin ──► EPOLLOUT          │      │
  │   │               pipe[1] stdout ──► EPOLLIN          │      │
  │   └──────────────────────────────────────────────────┘      │
  │                                                             │
  │  OptionsHandler (405 / CORS)                                │
  └─────────────────────────────────────────────────────────────┘

  ┌──────────────────────────────────────────────────────────────┐
  │                        Config                                │
  │                                                              │
  │  ┌──────────────────┐     ┌──────────────────┐              │
  │  │  ConfigParser    │────►│  ServerConfig    │              │
  │  │  parse(filepath) │     │  listens[]        │              │
  │  │                  │     │  serverName       │              │
  │  │  NGINX-style     │     │  clientMaxBodySize│              │
  │  │  .conf files     │     │  errorPages{}     │              │
  │  └──────────────────┘     │  keepaliveTimeout │              │
  │                           │  routes[] ◄──────┐│              │
  │                           └────────────────────┘│              │
  │                                  │              │              │
  │                           ┌──────┴──────────────┐              │
  │                           │    RouteConfig      │              │
  │                           │  path, root, index   │              │
  │                           │  autoindex, redirect │              │
  │                           │  allowedMethods[]    │              │
  │                           │  uploadStore         │              │
  │                           │  cgiHandlers{}       │              │
  │                           │  tryFiles[]           │              │
  │                           │  authBasic{user,pwd} │              │
  │                           └─────────────────────┘              │
  └──────────────────────────────────────────────────────────────┘

  ┌──────────────────────────────────────────────────────────────┐
  │                        Utils                                 │
  │                                                              │
  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
  │  │    Logger     │  │  StringUtils  │  │  FileUtils   │      │
  │  │  Singleton    │  │  intToString │  │  read, stat, │      │
  │  │  LOG_INFO/    │  │  toUpper,    │  │  exists,     │      │
  │  │  WARN/ERROR   │  │  toLower,    │  │  isDirectory │      │
  │  │               │  │  trim, split │  │              │      │
  │  └──────────────┘  └──────────────┘  └──────────────┘      │
  │                                                              │
  │  ┌──────────────┐  ┌──────────────────────────────────┐    │
  │  │ StatusCodes  │  │       SessionManager (Singleton)   │    │
  │  │ 200→404→...   │  │  create/session data/destroy     │    │
  │  └──────────────┘  │  webserv_session_id cookie        │    │
  │                     └──────────────────────────────────┘    │
  └──────────────────────────────────────────────────────────────┘
```

---

## Flujo Principal

```
1. main()
   ├── signal(SIGPIPE, SIG_IGN)
   ├── ConfigParser::parse(configPath)  ──►  vector<ServerConfig>
   ├── Reactor::init(servers)
   │    ├── epoll_create1()
   │    └── por cada listen en cada ServerConfig:
   │         ├── ServerSocket::bindAndListen()
   │         └── Reactor::registerHandler(fd, serverSocket, EPOLLIN)
   └── Reactor::run()
        └── loop:
             ├── epoll_wait(maxEvents, timeout=1000ms)
             ├── por cada evento: _dispatchEvent()
             │    ├── EPOLLIN  ──► handler->handleRead()
             │    ├── EPOLLOUT ──► handler->handleWrite()
             │    └── EPOLLERR/EPOLLHUP ──► handler->handleError()
             └── _cleanupConnections()
                  ├── Cerrar conexiones con timeout CGI alcanzado
                  ├── Cerrar conexiones inactivas (keepalive timeout)
                  └── Eliminar conexiones en estado CLOSING
```

---

## Estados de una Conexion

```
READING_HEADERS ──► READING_BODY ──► PROCESSING ──► WRITING_RESPONSE ──► CLOSING
                       │                  │
                       │                  ├── Si CGI:
                       │                  │   CGI_WRITING_TO_STDIN
                       │                  │       │
                       │                  │   CGI_READING_FROM_STDOUT
                       │                  │       │
                       │                  └──► WRITING_RESPONSE
                       │
                       └── Si chunked: acumula hasta ver el chunk final
```

---

## Modulos del Proyecto

### core/ — Event Loop y Conexiones

| Componente | Archivo | Responsabilidad |
|------------|---------|-----------------|
| `Reactor` | `core/Reactor` | Event loop central con `epoll`. Registra handlers, despacha eventos y limpia conexiones. |
| `ServerSocket` | `core/ServerSocket` | Socket de escucha. Implementa `EventHandler`. Hace `accept()` en `handleRead()` y crea objetos `Connection`. |
| `Connection` | `core/Connection` | Conexion de cliente. Implementa `EventHandler`. Gestiona el ciclo de vida completo: lectura, parsing, procesamiento, CGI, escritura de respuesta. |
| `EventHandler` | `core/EventHandler` | Interface polimorfica. Metodos virtuales: `handleRead()`, `handleWrite()`, `handleError()`, `getFd()`. |

### http/ — HTTP y Handlers

| Componente | Archivo | Responsibility |
|------------|---------|----------------|
| `HttpParser` | `http/HttpParser` | Parser incremental de HTTP/1.1. Maneja request-line, headers, Content-Length body, y chunked transfer encoding. Soporta pipelining con leftover buffer. |
| `Request` | `http/Request` | Estructura de datos para peticiones HTTP: method, URI, headers, body, queryString, cookies. |
| `Response` | `http/Response` | Estructura de datos para respuestas HTTP: status code, headers, body, cookies. Serializa con `toString()`. |
| `IHttpMethodHandler` | `http/IHttpMethodHandler` | Interface strategy para handlers de metodos HTTP. |
| `GetHandler` | `http/GetHandler` | GET: sirve archivos, directorios, autoindex. Soporta Range requests y ETag/conditional. |
| `PostHandler` | `http/PostHandler` | POST: upload multipart/form-data, delegacion a CGI. |
| `DeleteHandler` | `http/DeleteHandler` | DELETE: elimina archivos del `upload_store`. |
| `PutHandler` | `http/PutHandler` | PUT: creacion/reemplazo de recursos. |
| `HeadHandler` | `http/HeadHandler` | HEAD: igual que GET sin body en la respuesta. |
| `OptionsHandler` | `http/OptionsHandler` | OPTIONS: devuelve Allow header con metodos permitidos. |
| `SessionManager` | `http/SessionManager` | Singleton. Crea y gestiona sesiones via cookie `webserv_session_id`. Timeout de 30 min. |
| `StatusCodes` | `http/StatusCodes` | Mapa de codigos de estado HTTP a sus frases (200 → "OK", 404 → "Not Found"). |

### config/ — Configuracion

| Componente | Archivo | Responsabilidad |
|------------|---------|-----------------|
| `ConfigParser` | `config/ConfigParser` | Parser de archivos de configuracion estilo NGINX. Tokeniza y construye objetos `ServerConfig` y `RouteConfig`. |
| `ServerConfig` | `config/ServerConfig` | Configuracion de un bloque `server`: listens, serverName, clientMaxBodySize, errorPages, routes, keepaliveTimeout. |
| `RouteConfig` | `config/RouteConfig` | Configuracion de un bloque `location`: path, root, index, autoindex, allowedMethods, redirect, uploadStore, cgiHandlers, tryFiles, authBasic. |

### cgi/ — CGI

| Componente | Archivo | Responsabilidad |
|------------|---------|-----------------|
| `CgiHandler` | `cgi/CgiHandler` | Maquina de estados para CGI: setup de pipes, fork/execve del script, I/O no bloqueante con body en stdin pipe y output en stdout pipe. Ambos extremos se registran en el mismo epoll. |

### utils/ — Utilidades

| Componente | Archivo | Responsabilidad |
|------------|---------|-----------------|
| `Logger` | `utils/Logger` | Singleton de logging con niveles DEBUG/INFO/WARN/ERROR. |
| `StringUtils` | `utils/StringUtils` | Utilidades de strings: intToString, toUpper, toLower, trim, split. |
| `FileUtils` | `utils/FileUtils` | Utilidades de sistema de archivos: readFile, exists, isDirectory, getLastModified. |

---

## Patrones de Diseno

| Patron | Donde | Descripcion |
|--------|-------|-------------|
| **Reactor** | `Reactor` | Desacopla deteccion de eventos de su manejo. Un `epoll_fd` detecta; los `EventHandler` manejan. |
| **Strategy** | `IHttpMethodHandler` | Cada metodo HTTP tiene su propio handler inyectable. `Connection` no sabe la logica de GET vs POST. |
| **Singleton** | `Logger`, `SessionManager` | Instancia unica global para logging y sesiones. |
| **State Machine** | `Connection` state, `CgiHandler` state | `ConnectionState` y `CgiState` gobiernan el flujo de procesamiento. |

---

## Decisiones de Diseno Clave

- **epoll sobre select/poll**: O(1) por evento en vez de O(n). Sin limite de FD_SETSIZE. El kernel mantiene el interest set.
- **Level-triggered**: Simplifica la lectura: si `recv` devuelve datos, avanzamos; si bloquea, epoll re-notifica. No hay riesgo de perder eventos.
- **C++98**: Requisito del subject de 42. Las unicas fricciones son la ausencia de `to_string`, `nullptr`, range-for y lambdas.
- **fork/execve para CGI**: El subject prohíbe threads. Aislar cada CGI en su propio proceso previene que un script mal comportado corrompa el estado del servidor.
- **Sin errno despues de I/O**: El subject exige no revisar `errno` despues de operaciones no bloqueantes. Si `recv` devuelve -1, simplemente se retorna; epoll re-dispara cuando hay datos o error.
- **Mismo epoll para todo**: Sockets de escucha, sockets de cliente, pipes de stdin/stdout de CGI — todos comparten el mismo `epoll_fd`. No hay threads, no hay multiplexores separados.

---

## Estructura de Directorios

```
webserv/
├── include/
│   ├── core/          Reactor, Connection, ServerSocket, EventHandler
│   ├── http/           Request, Response, HttpParser, *Handler, SessionManager, StatusCodes
│   ├── config/         ConfigParser, ServerConfig, RouteConfig
│   ├── cgi/            CgiHandler
│   ├── utils/          Logger, StringUtils, FileUtils
│   └── Webserv.hpp     Forward declarations, includes, macros, enums
├── src/                Espeja la estructura de include/
├── conf/
│   ├── default.conf    Configuracion por defecto
│   └── tests/          Configs de prueba: cgi, upload, multi_port, etc.
├── www/
│   ├── default/         Sitio estatico raiz
│   └── upload/         Directorio de uploads
├── cgi-bin/            Scripts CGI: hello.py, echo.py, session.py, env.sh, info.php
├── docs/               Documentacion tecnica detallada
├── Makefile
└── README.md
```

---

## Compilar y Ejecutar

```bash
make                      # Compilar con -Wall -Wextra -Werror -std=c++98
./webserv                 # Usa conf/default.conf
./webserv ruta/a/config   # Configuracion personalizada
```

Requisitos: g++/clang++ con soporte C++98, Linux (epoll).

---

## Probando con curl

```bash
# Servir archivo estatico
curl -v http://localhost:8080/

# Upload multipart
curl -v -X POST -F "file=@/etc/hostname" http://localhost:8080/uploads/

# CGI con query string
curl -v "http://localhost:8080/cgi-bin/hello.py?name=world"

# DELETE en upload
curl -v -X DELETE http://localhost:8080/uploads/hostname

# Virtual host por Host header
curl -v -H "Host: secondary.local" http://localhost:8080/

# Pipelining
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

---

## Referencias

- RFC 7230 — HTTP/1.1 Message Syntax and Routing
- RFC 7231 — HTTP/1.1 Semantics and Content
- RFC 3875 — Common Gateway Interface 1.1
- RFC 6265 — HTTP State Management (cookies)
- RFC 7578 — multipart/form-data
- `epoll(7)`, `fork(2)`, `execve(2)`, `pipe(2)`, `fcntl(2)` — Linux man pages