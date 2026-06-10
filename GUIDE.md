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

## Desarrollo Teorico de Conceptos

### 1. El Patron Reactor y epoll

#### Concepto fundamental

El patron Reactor desacopla la **deteccion** de eventos de su **manejo**. En lugar de que cada componente sondee activamente si hay datos disponibles, un despachador central espera a que el sistema operativo notifique que un descriptor de archivo esta listo para I/O, y luego invoca al handler apropiado.

En este servidor, el Reactor es el punto central de toda la concurrencia:

```
Reactor::run() {
    while (_running) {
        nfds = epoll_wait(_epollFd, events, MAX_EVENTS, 1000);
        for (i = 0; i < nfds; i++)
            _dispatchEvent(events[i]);
        _cleanupConnections();
    }
}
```

`epoll_wait` bloquea hasta que uno o mas descriptores estan listos. Los descriptores se registran con `epoll_ctl` usando `event.data.ptr`, que almacena un puntero al `EventHandler` que sabe como manejar ese fd. Esto elimina la necesidad de mapear fd a handler manualmente — el puntero ya esta en el evento.

#### epoll vs select vs poll

| Aspecto | `select` | `poll` | `epoll` |
|---------|----------|--------|---------|
| Limite de fds | 1024 (FD_SETSIZE) | Sin limite fijo | Sin limite (solo memoria) |
| Complejidad por evento | O(n) — escanea todos los fds | O(n) — escanea todos los fds | O(1) — el kernel devuelve solo los listos |
| Requiere reconstruir fd_set? | Si, en cada llamada | No | No — se modifica con `epoll_ctl` |
| Soporte | POSIX (todas las plataformas) | POSIX | Solo Linux |

Para un servidor que mantiene cientos de conexiones abiertas (muchas en keep-alive, esperando datos), la diferencia entre O(n) y O(1) es critica. Con `select`, cada iteracion del loop necesita reconstruir `fd_set` y escanear todos los fds. Con `epoll`, el kernel mantiene el interest set y devuelve solo los que estan listos.

#### Level-triggered vs Edge-triggered

Este servidor usa level-triggered (LT), el modo por defecto de epoll:

- **LT**: Si un fd tiene datos sin leer, epoll notifica repetidamente hasta que se lean todos.
- **ET**: Notifica solo una vez cuando el fd pasa de "sin datos" a "con datos". Si no se leen todos los datos, no hay mas notificaciones hasta que lleguen nuevos datos.

LT es mas seguro para servidores simples: si `recv()` devuelve menos datos de los disponibles y la aplicacion no vacia el buffer, epoll volvera a notificar. Con ET, la aplicacion debe usar un bucle de lectura hasta `EAGAIN`, lo que es mas propenso a errores.

La eleccion de LT simplifica considerablemente el codigo. Cuando `recv` devuelve datos, se procesan; si no hay mas datos, la funcion retorna y epoll re-notifica en la siguiente iteracion. No se necesita un bucle de drenado.

#### Como se registran los descriptores

Cada descriptor pasa por tres operaciones a lo largo de su vida:

1. **`registerHandler(fd, handler, events)`**: Llama a `epoll_ctl(EPOLL_CTL_ADD)` para agregar el fd al interest set. Se usa cuando se acepta una nueva conexion (`ServerSocket::handleRead` crea un `Connection` y lo registra con `EPOLLIN`) o cuando se arranca un CGI y se registran los pipes.

2. **`modifyHandler(fd, events)`**: Llama a `epoll_ctl(EPOLL_CTL_MOD)` para cambiar los eventos monitoreados. El caso mas comun es cuando la respuesta esta lista para enviarse: se cambia de `EPOLLIN` a `EPOLLIN | EPOLLOUT` para que epoll notifique cuando el socket este listo para escribir.

3. **`removeHandler(fd)`**: Llama a `epoll_ctl(EPOLL_CTL_DEL)` y elimina del mapa. Se usa durante cleanup cuando una conexion se cierra.

#### El polimorfismo de EventHandler

La clave del patron es la interface `EventHandler`:

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

El Reactor no sabe si esta despachando a un `ServerSocket` o a un `Connection`. Simplemente llama `handler->handleRead()`. El polimorfismo resuelve el despacho.

`Connection` implementa `EventHandler` y se registra a si mismo tanto para su socket de cliente como para los pipes de CGI. Cuando epoll notifica actividad en un pipe de CGI, llama al mismo `Connection::handleRead()` o `Connection::handleWrite()`, que internamente enruta a `_processCgiRead()` o `_processCgiWrite()` segun el estado actual.

---

### 2. Maquinas de Estado: Connection y CgiHandler

#### ConnectionStateMachine

El ciclo de vida de una conexion se rige por `ConnectionState`:

```
READING_HEADERS --> READING_BODY --> PROCESSING --> WRITING_RESPONSE --> CLOSING
                                        |
                                        +--> CGI_WRITING_TO_STDIN --> CGI_READING_FROM_STDOUT --> WRITING_RESPONSE
```

Cada estado determina que hace `handleRead()` y `handleWrite()`:

| Estado | `handleRead()` | `handleWrite()` |
|--------|----------------|-----------------|
| `READING_HEADERS` | Acumula buffer, parsea request-line y headers | No hace nada |
| `READING_BODY` | Acumula body (Content-Length o chunked) | No hace nada |
| `PROCESSING` | No deberia ocurrir | No hace nada |
| `CGI_WRITING_TO_STDIN` | `_processCgiRead()` si hay datos en stdout | `_processCgiWrite()` envia body al pipe |
| `CGI_READING_FROM_STDOUT` | `_processCgiRead()` lee output del CGI | No hace nada |
| `WRITING_RESPONSE` | No hace nada | `_processWrite()` envia respuesta al cliente |
| `CLOSING` | Ignorado | Ignorado |

La transicion entre estados depende exclusivamente del resultado del parsing y del tipo de request:

- **Request completa sin CGI**: `READING_HEADERS` -> `PROCESSING` -> `WRITING_RESPONSE`
- **Request con body**: `READING_HEADERS` -> `READING_BODY` -> `PROCESSING` -> ...
- **CGI POST**: `PROCESSING` -> `CGI_WRITING_TO_STDIN` -> `CGI_READING_FROM_STDOUT` -> `WRITING_RESPONSE`
- **CGI GET**: `PROCESSING` -> `CGI_READING_FROM_STDOUT` -> `WRITING_RESPONSE`

Despues de enviar la respuesta completa, si `Connection: keep-alive`, se reinicia a `READING_HEADERS` sin cerrar el socket, preservando los datos sobrantes del buffer para soportar pipelining HTTP.

#### CgiHandler: Maquina de estados del proceso CGI

`CgiState` controla la vida del proceso CGI:

```
CGI_IDLE --> CGI_RUNNING --> CGI_WRITING --> CGI_READING --> CGI_DONE
                   |              |                |
                   v              v                v
              (fork+execve)  (escribir body    (leer output
                             al stdin pipe)     del stdout pipe)
```

El flujo detallado:

1. **`CgiHandler::start()`**: Crea dos pipes (`pipe(_inputPipe)`, `pipe(_outputPipe)`), construye las variables de entorno CGI (RFC 3875), y llama a `fork()`. El hijo hace `dup2()` para conectar stdin/stdout/stderr a los pipes, luego `chdir()` al directorio del script, y llama a `execve()`. El padre cierra los extremos opuestos de los pipes y los configura como no bloqueantes con `fcntl(O_NONBLOCK)`.

2. **Escritura del body (POST)**: Si la request tiene body, el padre registrara el pipe de escritura (`_inputPipe[1]`) con `EPOLLOUT`. Cuando epoll notifique que el pipe esta listo, `Connection::_processCgiWrite()` escribira un fragmento del body. Una vez escrito todo, se cierra el pipe de escritura (`finishBodyWrite()`), lo que senala al CGI que el input ha terminado (EOF en stdin).

3. **Lectura del output**: El pipe de lectura (`_outputPipe[0]`) se registra con `EPOLLIN`. En `_processCgiRead()`, se lee en un bucle hasta que `read()` devuelva 0 (EOF) o -1 (EAGAIN). Si es EOF, se parsea la salida CGI con `_parseCgiOutput()`.

4. **Parseo de salida CGI**: La salida se separa en headers y body usando la secuencia `\r\n\r\n` (o `\n\n` como fallback). Se busca un header `Status:` para determinar el codigo de respuesta. Si hay un header `Location` sin body, se asume un redirect 302 (RFC 3875 seccion 6.2.4).

5. **Timeout**: `_cleanupConnections()` verifica en cada iteracion si algun CGI ha excedido los 30 segundos. Si es asi, se envia SIGKILL al proceso hijo y se limpia.

6. **Error handling**: Si el proceso hijo termina con status no-cero y la salida no contiene headers CGI validos, se devuelve un error 500.

#### Importancia de `EPOLLIN antes que EPOLLHUP`

En `_dispatchEvent()`, los eventos se procesan en este orden:

```cpp
if (event.events & EPOLLIN) handler->handleRead();
if (event.events & EPOLLOUT) handler->handleWrite();
if (event.events & (EPOLLERR | EPOLLHUP)) handler->handleError();
```

Esto es intencional: cuando el proceso CGI cierra su extremo del pipe, el padre recibe `EPOLLHUP | EPOLLIN`. Los datos finales CGI estan en el buffer del pipe. Si se procesara `EPOLLHUP` primero, los datos se perderian. Al procesar `EPOLLIN` primero, se lee todo el output antes de manejar el cierre.

---

### 3. I/O No Bloqueante y el Problema del `errno`

#### Filosofia del subject

El subject de 42 exige explícitamente que `errno` no se consulte despues de operaciones de I/O no bloqueante. La razon es pedagogica: obliga a depender unicamente de `epoll` para la coordinacion, no de la inspeccion de `errno`.

#### Implementacion practica

Cuando `recv()` devuelve -1 en un socket no bloqueante:

```cpp
ssize_t bytesRead = recv(_clientFd, buffer, BUFFER_SIZE - 1, 0);
if (bytesRead < 0) {
    // Sin verificar errno: si fue EAGAIN, epoll re-notificara.
    // Si fue un error real, epoll disparara EPOLLERR/EPOLLHUP.
    return;
}
```

Los dos casos se resuelven solos:
- **EAGAIN/EWOULDBLOCK**: No hay datos disponibles ahora. epoll volvera a notificar con `EPOLLIN` cuando lleguen datos.
- **Error real**: epoll generara un evento `EPOLLERR` o `EPOLLHUP`, que despacha a `handleError()`, que pone la conexion en estado `CLOSING`.

Lo mismo aplica para `send()`: si devuelve -1, simplemente retornamos. Si fue EAGAIN, epoll notificara con `EPOLLOUT` cuando el socket este listo para escribir. Si fue un error, `EPOLLERR` lo manejara.

#### Configuracion de sockets como no bloqueantes

Todos los sockets se configuran con `O_NONBLOCK` en `ServerSocket::bindAndListen()`:

```cpp
fcntl(_fd, F_SETFL, O_NONBLOCK);
```

Los pipes de CGI tambien se configuran como no bloqueantes despues del fork:

```cpp
fcntl(_inputPipe[1], F_SETFL, O_NONBLOCK);
fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK);
```

Sin `O_NONBLOCK`, cualquier llamada a `recv()` o `read()` que no tenga datos disponibles bloquearia todo el proceso, congelando el servidor para todas las conexiones.

---

### 4. Parsing HTTP Incremental y Pipelining

#### El problema del framing TCP

TCP es un stream protocol — no hay garantía de que una peticion HTTP completa llegue en una sola llamada a `recv()`. Ni de que una sola llamada a `recv()` contenga exactamente una peticion. Los datos pueden llegar:
- Fraccionados: una peticion en multiples `recv()` (TCP la fragmento).
- Pegados: multiples peticiones en un solo `recv()` (TCP las junto, aka Nagle).
- Parciales: headers incompletos, body a medias.

#### HttpParser: diseno incremental

`HttpParser` esta disenado para ser llamado repetidamente con datos parciales:

```cpp
ParseResult HttpParser::parse(const std::string& rawData, Request& outRequest) {
    _buffer.append(rawData);  // Acumula todo en buffer interno

    if (!_requestLineComplete)
        result = _parseRequestLine(outRequest);  // Busca \r\n

    if (!_headersComplete)
        result = _parseHeaders(outRequest);  // Busca \r\n\r\n

    // Si el metodo no tiene body (GET, HEAD, DELETE), retorna PARSE_OK inmediatamente
    // Si tiene Content-Length, espera tener suficientes bytes
    // Si es chunked, parsea los chunks uno a uno
}
```

Los tres valores de retorno son:
- **`PARSE_OK`**: Peticion completa. El handler puede procesarla.
- **`PARSE_INCOMPLETE`**: Faltan datos. Se debe seguir leyendo del socket.
- **`PARSE_ERROR`**: Peticion mal formada. Se responde con 400.

#### Pipelining HTTP y leftover data

HTTP/1.1 permite pipelining: enviar multiples peticiones sin esperar la respuesta de cada una. El buffer puede contener datos de la peticion siguiente:

```
[HTTPRequest1][HTTPRequest2][...fragmento de HttpRequest3...]
```

Cuando `parse()` devuelve `PARSE_OK`, el buffer interno puede contener datos de la siguiente peticion. El flujo en `Connection` es:

```cpp
if (result == PARSE_OK) {
    _readBuffer = _parser.getLeftoverData();  // Preserva datos de la siguiente peticion
    _parser.reset();
    // ... procesa la peticion actual ...
}
```

Despues de enviar la respuesta en una conexion keep-alive, el estado regresa a `READING_HEADERS`, y el leftover ya esta en `_readBuffer` esperando a ser parseado en la proxima iteracion de `epoll_wait`.

#### Chunked Transfer Encoding

Para peticiones con `Transfer-Encoding: chunked`, el parser reensambla el body:

```
5\r\n
Hello\r\n
6\r\n
 World\r\n
0\r\n
\r\n
```

Cada chunk tiene el tamano en hexadecimal,seguido de `\r\n`, los datos, y `\r\n`. El chunk final tiene tamano 0. `_parseChunkedBody()` procesa los chunks incrementalmente — si el buffer no contiene un chunk completo, retorna `PARSE_INCOMPLETE` y espera mas datos.

---

### 5. CGI: Comunicacion Inter-Proceso con Pipes

#### Arquitectura de pipes

CGI usa dos pipes unidireccionales para comunicar entre el servidor (padre) y el script (hijo):

```
Servidor (padre)                     Script CGI (hijo)
================                     =================

_inputPipe[1] ──── write ──────────► _inputPipe[0] ───► STDIN
_outputPipe[0] ◄──── read ───────── _outputPipe[1] ◄── STDOUT/STDERR
```

Despues de `fork()`:
- El **padre** cierra `_inputPipe[0]` (extremo lectura) y `_outputPipe[1]` (extremo escritura).
- El **hijo** cierra `_inputPipe[1]` (extremo escritura) y `_outputPipe[0]` (extremo lectura).
- El hijo hace `dup2(_inputPipe[0], STDIN_FILENO)` y `dup2(_outputPipe[1], STDOUT_FILENO)`.

#### Variables de entorno CGI (RFC 3875)

La funcion `_setupEnvironment()` construye las variables requeridas por la especificacion CGI 1.1:

| Variable | Origen |
|----------|--------|
| `GATEWAY_INTERFACE` | Constante `CGI/1.1` |
| `SERVER_PROTOCOL` | Constante `HTTP/1.1` |
| `REQUEST_METHOD` | De la request line |
| `SCRIPT_NAME` | Del URI path (antes de `?`) |
| `QUERY_STRING` | Del URI path (despues de `?`) |
| `CONTENT_TYPE` | Del header `Content-Type` |
| `CONTENT_LENGTH` | Del header `Content-Length` o del tamano del body |
| `SERVER_NAME`, `SERVER_PORT` | De la directiva `listen` del server |
| `HTTP_*` | Cada header HTTP se convierte: `User-Agent` -> `HTTP_USER_AGENT` |
| `REDIRECT_STATUS` | Constante `200` (requerido por PHP-CGI) |

#### El problema del zombie process

Despues de que el script CGI termina, el proceso hijo se convierte en zombie si el padre no llama a `waitpid()`. El servidor maneja esto en `finishOutputRead()`:

```cpp
pid_t result = waitpid(_pid, &status, WNOHANG);
if (result == 0) {
    // Hijo aun corriendo — pero su stdout ya cerro.
    // Un CGI bien comportado deberia haber terminado.
    kill(_pid, SIGKILL);
    waitpid(_pid, &status, 0);
}
```

Se usa `WNOHANG` (no bloqueante) primero. Si el hijo aun no termino pero su pipe de salida ya cerro, se le envia SIGKILL. Esto previene que un CGI descontrolado consuma recursos indefinidamente — el timeout de 30 segundos actua como segundo mecanismo de seguridad.

---

### 6. Virtual Hosting y Routing

#### Seleccion de ServerConfig por Host header

La funcion `Reactor::matchVirtualHost()` implementa la seleccion de servidor virtual:

1. **Primera pasada**: Busca un `ServerConfig` donde el puerto coincida y el `server_name` coincida exactamente con el header `Host`.
2. **Segunda pasada**: Si no hay match exacto, busca cualquier `ServerConfig` que escuche en el mismo puerto (wildcard match).
3. **Fallback**: Devuelve el primer `ServerConfig`.

Esto permite que multiples dominios compartan la misma direccion IP y puerto, pero sirvan contenido diferente:

```nginx
server { listen 8080; server_name site1.local; ... }
server { listen 8080; server_name site2.local; ... }
```

Cuando llega una peticion con `Host: site2.local:8080`, se selecciona el segundo bloque. Con `Host: localhost` o sin header Host, se recurre al primer bloque.

#### Matching de rutas

`ServerConfig::findRoute()` hace longest-prefix match sobre el URI de la peticion. Cada bloque `location` tiene un path, y se selecciona la ruta cuyo path es el prefijo mas largo del URI. Esto es consistente con el comportamiento de NGINX.

---

### 7. Configuracion: NGINX-Style DSL

#### Diseno del parser

`ConfigParser` implementa un parser descendente recursivo sobre un DSL que imita la sintaxis de NGINX:

```nginx
server {
    listen 8080;                    # directiva simple
    server_name localhost;          # directiva simple
    client_max_body_size 10M;       # directiva con sufijo (K/M/G)
    error_page 404 50x /errors/;    # directiva multi-valor

    location / {                    # bloque anidado
        root www/default;
        allow_methods GET POST;     # lista de valores
    }
}
```

El parser tokeniza el archivo en tres fases:
1. `_skipWhitespace()` y `_skipComments()`: Saltan espacios y lineas que empiezan con `#`.
2. `_parseToken()`: Extrae una palabraclave hasta el proximo espacio, `;`, `{`, o `}`.
3. `_parseValue()`: Extrae un valor hasta `;`, `{`, o `}`, soportando comillas.

La estructura de datos resultante es un `vector<ServerConfig>` donde cada `ServerConfig` contiene un `vector<RouteConfig>`. Esta jerarquia refleja directamente la anidacion del archivo de configuracion.

#### Directivas soportadas

**A nivel `server`:**

| Directiva | Ejemplo | Descripcion |
|-----------|---------|-------------|
| `listen` | `listen 127.0.0.1:8080;` | Direccion y puerto. Sin IP: `0.0.0.0` |
| `server_name` | `server_name localhost;` | Nombre para virtual hosting |
| `client_max_body_size` | `client_max_body_size 10M;` | Limite de tamano de body. Sufijos K/M/G |
| `error_page` | `error_page 404 502 /errors/;` | Pagina de error personalizada por codigo |
| `keepalive_timeout` | `keepalive_timeout 10;` | Timeout en segundos para conexiones inactivas |

**A nivel `location`:**

| Directiva | Ejemplo | Descripcion |
|-----------|---------|-------------|
| `root` | `root www/default;` | Directorio raiz para servir archivos |
| `index` | `index index.html;` | Archivo por defecto al servir un directorio |
| `autoindex` | `autoindex on;` | Generar listado HTML de directorios |
| `allow_methods` | `allow_methods GET POST;` | Metodos HTTP permitidos (405 si no esta en la lista) |
| `redirect` | `redirect /;` | Redireccion 301 a otro path |
| `upload_store` | `upload_store www/upload;` | Directorio donde guardar uploads |
| `cgi_extension` | `cgi_extension .py /usr/bin/python3;` | Extension e interprete para CGI |
| `try_files` | `try_files $uri $uri/ /404.html;` | Lista de archivos a intentar en orden |
| `auth_basic` | `auth_basic "Area restringida";` | Realm para autenticacion Basic |
| `auth_basic_user` | `auth_basic_user admin;` | Usuario para Basic Auth |
| `auth_basic_password` | `auth_basic_password secret;` | Contrasena para Basic Auth |

---

### 8. Keep-Alive, Pipelining y Timeouts

#### Keep-Alive

Despues de enviar una respuesta, la conexion no se cierra si:
- El cliente envio `Connection: keep-alive` (o no envio `Connection: close`).
- La version es HTTP/1.1 (keep-alive por defecto).

Al completar el envio:

```cpp
if (_keepAlive) {
    _request.clear();
    _response.clear();
    _writeBuffer.clear();
    _bytesWritten = 0;
    _parser.reset();
    _state = READING_HEADERS;
    _isKeepAliveIdle = true;
    updateLastActivity();
    _reactor.modifyHandler(_clientFd, EPOLLIN);
}
```

Se preserva `_readBuffer` porque puede contener datos de una peticion siguiente (pipelining).

#### Pipelining

HTTP pipelining permite enviar multiples peticiones seguidas sin esperar la respuesta de cada una. El servidor debe procesarlas en orden y responder en orden. En esta implementacion, solo se procesa una peticion a la vez por conexion — se guarda el leftover del buffer y se reinicia el parser despues de cada respuesta.

#### Timeouts

Hay dos timeouts configurables:

| Timeout | Default | Condicion |
|---------|---------|-----------|
| `CONNECTION_TIMEOUT` | 60s | Conexion activa (leyendo/escribiendo) |
| `KEEP_ALIVE_TIMEOUT` | configurable via `keepalive_timeout`, default 10s | Conexion inactiva (esperando nueva peticion) |

La distincion se maneja con el flag `_isKeepAliveIdle`. En `_cleanupConnections()`:

```cpp
bool Connection::isTimedOut() const {
    time_t timeout = _isKeepAliveIdle
        ? _serverConfig->getKeepaliveTimeout()
        : CONNECTION_TIMEOUT;
    return (time(NULL) - _lastActivity) > timeout;
}
```

---

### 9. HTTP Semantico: Metodos, Redirecciones y Uploads

#### GET Handler: Mas que servir archivos

`GetHandler` no solo lee archivos del disco. Implementa:

1. **Resolucion de path**: `_resolvePath()` combina el URI de la peticion con `path` (prefijo del location) y `root` (directorio base) para obtener la ruta fisica. Se decodifica la URL (`%20` -> espacio, etc.).

2. **try_files**: Si la directiva `try_files` esta configurada, se prueban los paths en orden. El token `$uri` se reemplaza por el path de la peticion. El ultimo entry es un fallback.

3. **Redireccion de directorios**: Si el path resuelve a un directorio y el URI no tiene `/` al final, se envia un 301 redirect. Esto evita problemas con rutas relativas en el HTML.

4. **Autoindex**: Si `autoindex on` y no hay archivo `index`, se genera un listado HTML con links a cada archivo.

5. **Range requests**: Soporta `Range: bytes=start-end` para descargas parciales (HTTP 206). Tambien soporta suffix ranges (`bytes=-N`: ultimos N bytes) y open-ended (`bytes=N-`: desde N hasta el final).

6. **ETag y conditional requests**: Calcula un ETag basado en mtime+size (`"mtime_hex-size_hex"`). Soporta `If-None-Match` (304 si el ETag coincide) e `If-Modified-Since` (304 si no ha cambiado).

7. **Path traversal protection**: `_resolvePath` en `StringUtils` normaliza el path y rechaza secuencias como `..` que escaparian del directorio raiz.

#### POST Handler: Multipart parsing

`PostHandler` maneja `multipart/form-data` extrayendo el `boundary` del header `Content-Type`, parseando las partes del body para encontrar el campo `filename`, y escribiendo el contenido al directorio `upload_store`.

El boundary puede estar entre comillas o sin comillas. El parser maneja ambos formatos de fin de linea (CRLF y LF).

#### DELETE Handler

`DeleteHandler` elimina el archivo especificado en el URI del directorio `upload_store`. Primero verifica que el archivo exista y que el metodo este permitido en la configuracion del location.

#### Basic Auth

La autenticacion se implementa en `Connection::_checkBasicAuth()`:

```
1. Extraer header Authorization
2. Verificar que empieza con "Basic "
3. Decodificar Base64 la parte despues de "Basic "
4. Separar en usuario:contrasena
5. Comparar con auth_basic_user y auth_basic_password de la configuracion
```

Si la autenticacion falla, se responde con 401 y header `WWW-Authenticate: Basic realm="..."`.

---

### 10. Sesion y Cookies

#### SessionManager

La gestion de sesiones usa un patron Singleton con un mapa de ID a objeto `Session`:

```cpp
struct Session {
    std::string id;
    std::map<std::string, std::string> data;
    time_t createdAt;
    time_t lastAccessed;
};
```

El flujo por cada peticion:

1. Se extrae la cookie `webserv_session_id` del header `Cookie`.
2. Si no existe o no es valida, se crea una nueva sesion con `SessionManager::createSession()`.
3. Se establece la cookie en la respuesta con `response.setCookie("webserv_session_id", sessionId)`.
4. Cada 300 iteraciones del event loop, `SessionManager::cleanExpired()` elimina sesiones con mas de 30 minutos de inactividad.

El ID de sesion se genera con `rand()` + timestamp, convertido a hexadecimal. Esto es deterministicamente predecible y no es criptograficamente seguro — es adecuado para un proyecto educativo pero no para produccion.

---

### 11. Manejo de Errores y Respuestas de Error

#### Codigos de estado implementados

| Codigo | Condicion |
|--------|-----------|
| 200 | Respuesta exitosa |
| 201 | Archivo creado (POST upload) |
| 206 | Partial Content (Range request) |
| 301 | Redirect permanente (directiva `redirect` o directorio sin `/`) |
| 302 | CGI redirect (header `Location` sin status explicito) |
| 304 | Not Modified (ETag/If-None-Match o If-Modified-Since) |
| 400 | Peticion mal formada |
| 401 | No autenticado (Basic Auth) |
| 403 | Acceso denegado (path traversal, directorio sin autoindex) |
| 404 | No encontrado |
| 405 | Metodo no permitido |
| 413 | Body excede `client_max_body_size` |
| 416 | Range Not Satisfiable |
| 500 | Error interno del servidor |
| 504 | Timeout de CGI |

#### Paginas de error personalizadas

La directiva `error_page` permite configurar archivos HTML personalizados por codigo de error:

```nginx
error_page 404 /errors/404.html;
error_page 500 502 503 504 /errors/50x.html;
```

Cuando se genera un error, `Response::buildError()` verifica si hay una pagina personalizada para ese codigo. Si existe, la sirve; si no, genera una pagina HTML por defecto.

---

### 12. RAII y Gestion de Recursos

Aunque C++98 no tiene smart pointers, el servidor usa RAII extensivamente:

- **Destructor de Connection**: Cierra el socket del cliente (`close(_clientFd)`), desregistra los pipes CGI (`_unregisterCgiPipes()`), y elimina el `CgiHandler` si existe.
- **Reactor::~Reactor()**: Itera sobre `_handlers` y elimina cada `EventHandler*`, luego cierra `_epollFd`.
- **CgiHandler::cleanup()**: Envia SIGKILL al proceso hijo si aun corre, llama a `waitpid()` para prevenir zombies, y cierra todos los pipes.
- **FD_CLOEXEC**: Los pipes CGI del padre se configuran con `FD_CLOEXEC` para que no se filtren a futuros procesos hijo si hay otro fork.

Esta disciplina de "quien crea, destruye" es critica en un servidor que maneja cientos de descriptores. Un fd filtrado eventualmente causaria que `epoll_wait` fallen por exceder el limite del sistema.

---

## Referencias

- RFC 7230 — HTTP/1.1 Message Syntax and Routing
- RFC 7231 — HTTP/1.1 Semantics and Content
- RFC 3875 — Common Gateway Interface 1.1
- RFC 6265 — HTTP State Management (cookies)
- RFC 7578 — multipart/form-data
- RFC 7233 — Range Requests
- RFC 7232 — Conditional Requests (ETag, If-Modified-Since)
- `epoll(7)`, `fork(2)`, `execve(2)`, `pipe(2)`, `fcntl(2)` — Linux man pages