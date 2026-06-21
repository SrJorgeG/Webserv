# Parte 1 - Core, Event Loop & I/O de red

**Companero A**
**Archivos:** `src/main.cpp`, `src/core/Reactor.cpp`, `src/core/ServerSocket.cpp`, `src/core/Connection.cpp`
**Headers:** `include/core/Reactor.hpp`, `include/core/ServerSocket.hpp`, `include/core/Connection.hpp`, `include/core/EventHandler.hpp`, `include/Webserv.hpp`

Esta es la capa base del servidor: el motor que acepta conexiones, detecta eventos de I/O y despacha a los handlers. Sin esta capa, nada del resto funciona.

---

## 1. Punto de entrada: `main.cpp`

```
main()
  ├── signal(SIGPIPE, SIG_IGN)     // ignorar SIGPIPE: prefiero detectar EPIPE via epoll
  ├── ConfigParser::parse(argv)    // leer config -> vector<ServerConfig>
  ├── Reactor::init(servers)       // crear epoll, bind+listen de cada ServerSocket
  └── Reactor::run()               // event loop infinito
```

Puntos a explicar:
- Por que `signal(SIGPIPE, SIG_IGN)`: si `send()` escribe a un socket que el cliente cerro, el kernel manda SIGPIPE. Sin ignorarlo, el servidor moriria. Con SIG_IGN ignorado, `send` devuelve -1 y epoll se encarga.
- El argumento del config es opcional: si no se pasa, usa `conf/default.conf`.
- Si `ConfigParser::parse` falla (sintaxis incorrecta), el proceso sale con error antes de arrancar el event loop.

---

## 2. Patron Reactor y epoll

### Concepto fundamental

El patron Reactor **desacopla la deteccion de eventos de su manejo**. Un despachador central (`epoll_wait`) espera a que el OS notifique que un fd esta listo. Luego invoca al handler apropiado via un puntero polimorfico.

### epoll vs select vs poll

| Aspecto | `select` | `poll` | `epoll` |
|---------|----------|--------|---------|
| Limite de fds | 1024 (FD_SETSIZE) | Sin limite fijo | Sin limite (solo memoria) |
| Complejidad por evento | O(n) — escanea todos | O(n) — escanea todos | O(1) — kernel devuelve solo los listos |
| Reconstruir fd_set cada llamada? | Si | No | No — `epoll_ctl` modifica el interest set |
| Soporte | POSIX (todas) | POSIX | Solo Linux |

Para un servidor con cientos de conexiones en keep-alive, la diferencia O(n) vs O(1) es critica. El subject permite `select`, `poll`, `kqueue` o `epoll`; elegimos `epoll` porque es Linux y es el mas eficiente.

### Level-triggered vs Edge-triggered

Usamos **level-triggered** (LT), el modo por defecto:
- **LT**: si un fd tiene datos sin leer, epoll notifica repetidamente hasta que se lean.
- **ET**: notifica solo una vez al pasar de "sin datos" a "con datos". Si no se leen todos, no hay mas notificaciones.

LT es mas seguro: si `recv` devuelve menos datos de los disponibles y no vaciamos el buffer, epoll vuelve a notificar. Con ET habria que drenar hasta EAGAIN en un bucle, mas propenso a errores.

### El bucle principal

```cpp
void Reactor::run() {
    _running = true;
    struct epoll_event events[MAX_EVENTS];
    while (_running) {
        int nfds = epoll_wait(_epollFd, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;   // senal recibida, reintentar
            break;
        }
        for (int i = 0; i < nfds; ++i)
            _dispatchEvent(events[i]);
        _cleanupConnections();
    }
}
```

- **timeout=1000ms**: ni 0 (spin-poll) ni -1 (bloqueo infinito). 1 segundo da oportunidad al cleanup de verificar timeouts de CGI y conexiones inactivas.
- **EINTR**: si una senal interrumpe `epoll_wait`, se reintentar. Es el unico lugar donde se revisa `errno` y es legitimo: no es despues de I/O, es despues de `epoll_wait`.

### Registro de descriptores

Tres operaciones a lo largo de la vida de un fd:

1. **`registerHandler(fd, handler, events)`** -> `epoll_ctl(EPOLL_CTL_ADD)`. Se usa al aceptar una conexion o arrancar un CGI.
2. **`modifyHandler(fd, events)`** -> `epoll_ctl(EPOLL_CTL_MOD)`. Cambia los eventos monitoreados. Ej: de `EPOLLIN` a `EPOLLIN | EPOLLOUT` cuando la respuesta esta lista para enviar.
3. **`removeHandler(fd)`** -> `epoll_ctl(EPOLL_CTL_DEL)`. Durante cleanup.

Cada `epoll_event` guarda un `void* data.ptr` al `EventHandler`. No hay mapa fd->handler en dispatch time: el puntero ya esta en el evento.

---

## 3. Interface `EventHandler` (polimorfismo)

```cpp
class EventHandler {
public:
    virtual ~EventHandler() {}
    virtual void handleRead()  = 0;
    virtual void handleWrite() = 0;
    virtual void handleError() = 0;
    virtual int getFd() const  = 0;
};
```

Dos implementaciones concretas:
- **`ServerSocket`**: handleRead hace `accept()` y crea un `Connection`.
- **`Connection`**: handleRead/handleWrite gestionan I/O del cliente Y de los pipes CGI (el mismo objeto se registra para los tres fds).

El Reactor no sabe a quien despacha. Solo llama `handler->handleRead()`. El polimorfismo resuelve el despacho. **Anyadir un tipo nuevo de fd** (ej: un pipe handler separado) solo requiere implementar `EventHandler` y llamar `registerHandler`.

---

## 4. `ServerSocket`: aceptar conexiones

```cpp
void ServerSocket::handleRead() {
    int clientFd = accept(_fd, ...);
    fcntl(clientFd, F_SETFL, O_NONBLOCK);        // no bloqueante
    fcntl(clientFd, F_SETFD, FD_CLOEXEC);        // no heredar en futuros fork
    Connection* conn = new Connection(clientFd, ...);
    _reactor.registerHandler(clientFd, conn, EPOLLIN);
}
```

- `accept` devuelve un fd bloqueante por defecto. Se pone `O_NONBLOCK` inmediatamente.
- `FD_CLOEXEC`: si hay un `fork` para CGI despues, este socket de cliente no se filtra al proceso hijo.
- Si `accept` devuelve -1 (EAGAIN: no hay conexiones pendientes), simplemente se retorna. epoll re-notificara.

---

## 5. `Connection`: maquina de estados

### Diagrama de estados

```
READING_HEADERS --> READING_BODY --> PROCESSING --> WRITING_RESPONSE --> CLOSING
                        |                  |
                        |                  +--> CGI_WRITING_TO_STDIN
                        |                          |
                        |                  CGI_READING_FROM_STDOUT
                        |                          |
                        |                  --------> WRITING_RESPONSE
                        |
                        +--> Si chunked: acumula hasta ver el chunk final
```

### Que hace cada estado

| Estado | `handleRead()` | `handleWrite()` |
|--------|----------------|-----------------|
| `READING_HEADERS` | Acumula buffer, parsea request-line y headers | No hace nada |
| `READING_BODY` | Acumula body (Content-Length o chunked) | No hace nada |
| `PROCESSING` | No deberia ocurrir | No hace nada |
| `CGI_WRITING_TO_STDIN` | `_processCgiRead()` si hay datos en stdout | `_processCgiWrite()` envia body al pipe |
| `CGI_READING_FROM_STDOUT` | `_processCgiRead()` lee output del CGI | No hace nada |
| `WRITING_RESPONSE` | No hace nada | `_processWrite()` envia respuesta al cliente |
| `CLOSING` | Ignorado | Ignorado |

### Transiciones

- **Request sin body, sin CGI**: `READING_HEADERS` -> `PROCESSING` -> `WRITING_RESPONSE`
- **Request con body**: `READING_HEADERS` -> `READING_BODY` -> `PROCESSING` -> ...
- **CGI POST**: `PROCESSING` -> `CGI_WRITING_TO_STDIN` -> `CGI_READING_FROM_STDOUT` -> `WRITING_RESPONSE`
- **CGI GET**: `PROCESSING` -> `CGI_READING_FROM_STDOUT` -> `WRITING_RESPONSE`

### Keep-alive: regreso a READING_HEADERS

Despues de enviar la respuesta, si `Connection: keep-alive` (o HTTP/1.1 sin `Connection: close`):

```cpp
if (_keepAlive) {
    _request.clear();
    _response.clear();
    _parser.reset();
    _state = READING_HEADERS;
    _isKeepAliveIdle = true;
    _reactor.modifyHandler(_clientFd, EPOLLIN);
}
```

Se preserva `_readBuffer` porque puede contener la siguiente peticion (pipelining).

### Enrutado CGI dentro de Connection

El mismo objeto `Connection` se registra para su socket de cliente y para los pipes CGI. Cuando epoll dispara en un pipe, llama al mismo `handleRead()`:

```cpp
void Connection::handleRead() {
    _isKeepAliveIdle = false;
    updateLastActivity();
    if ((_state == CGI_READING_FROM_STDOUT || _state == CGI_WRITING_TO_STDIN) && _cgiHandler) {
        _processCgiRead();    // leer del pipe CGI, no del socket cliente
    } else {
        _processRead();       // leer HTTP del socket cliente
    }
}
```

El estado determina a que metodo interno se enruta.

---

## 6. I/O no bloqueante y la regla del `errno`

### Filosofia del subject

El subject prohibe explicitamente revisar `errno` despues de operaciones de I/O no bloqueante. Es pedagogico: obliga a depender solo de `epoll` para la coordinacion.

### Implementacion

```cpp
ssize_t bytesRead = recv(_clientFd, buffer, BUFFER_SIZE - 1, 0);
if (bytesRead < 0) {
    // Sin verificar errno. Si fue EAGAIN, epoll re-notificara.
    // Si fue un error real, epoll disparara EPOLLERR/EPOLLHUP.
    return;
}
if (bytesRead == 0) {
    _state = CLOSING;   // cliente desconecto
    return;
}
```

Los dos casos se resuelven solos:
- **EAGAIN/EWOULDBLOCK**: no hay datos ahora. epoll notificara con `EPOLLIN` cuando lleguen.
- **Error real**: epoll generara `EPOLLERR` o `EPOLLHUP`, que va a `handleError()` -> estado `CLOSING`.

Lo mismo aplica para `send()`: si devuelve -1, se retorna. Si fue EAGAIN, epoll notificara con `EPOLLOUT`.

### Configuracion de fds no bloqueantes

- Sockets de escucha: `fcntl(_fd, F_SETFL, O_NONBLOCK)` en `ServerSocket::bindAndListen()`
- Sockets de cliente: `fcntl(clientFd, F_SETFL, O_NONBLOCK)` tras `accept()`
- Pipes CGI: `fcntl(_inputPipe[1], F_SETFL, O_NONBLOCK)` y `fcntl(_outputPipe[0], F_SETFL, O_NONBLOCK)` tras `fork()`

Sin `O_NONBLOCK`, cualquier `recv`/`read` sin datos bloquearia todo el proceso. El servidor se congelaria para todas las conexiones.

---

## 7. Orden de dispatch: EPOLLIN antes que EPOLLHUP

```cpp
void Reactor::_dispatchEvent(struct epoll_event& event) {
    EventHandler* handler = static_cast<EventHandler*>(event.data.ptr);
    if (!handler) return;

    if (event.events & EPOLLIN)             handler->handleRead();
    if (event.events & EPOLLOUT)            handler->handleWrite();
    if (event.events & (EPOLLERR | EPOLLHUP)) handler->handleError();
}
```

**Por que este orden importa**: cuando el proceso CGI termina, el kernel cierra el extremo de escritura del pipe de salida. El extremo de lectura (registrado en epoll) recibe `EPOLLHUP | EPOLLIN` si hay datos en el buffer. Si `EPOLLHUP` se procesara primero, `handleError` cerraria la conexion antes de que `_processCgiRead` lea el output del CGI. **Los datos se perderian**.

Al procesar `EPOLLIN` primero, se lee todo el output antes de manejar el cierre. Este fue un bug real que se encontro y se documento en `docs/IMPLEMENTATION_STATUS.md`.

---

## 8. Virtual hosting

`Reactor::matchVirtualHost()` selecciona la configuracion por peticion:

1. **Primera pasada**: busca un `ServerConfig` donde puerto coincida y `server_name` coincida exactamente con el header `Host`.
2. **Segunda pasada**: si no hay match exacto, busca cualquier `ServerConfig` que escuche en el mismo puerto (wildcard).
3. **Fallback**: devuelve el primer `ServerConfig`.

Esto permite que multiples dominios compartan la misma IP:puerto pero sirvan contenido diferente.

```bash
curl -v -H "Host: secondary.local" http://localhost:8080/
```

---

## 9. Timeouts y cleanup loop

### Dos timeouts

| Timeout | Default | Condicion |
|---------|---------|-----------|
| `CONNECTION_TIMEOUT` | 60s | Conexion activa (leyendo/escribiendo) |
| `KEEP_ALIVE_TIMEOUT` | configurable via `keepalive_timeout`, default 10s | Conexion inactiva (esperando nueva peticion) |

```cpp
bool Connection::isTimedOut() const {
    time_t timeout = _isKeepAliveIdle
        ? _serverConfig->getKeepaliveTimeout()
        : CONNECTION_TIMEOUT;
    return (time(NULL) - _lastActivity) > timeout;
}
```

### Cleanup tras cada `epoll_wait`

```cpp
void Reactor::_cleanupConnections() {
    // 1. CGI timeout check (30s -> SIGKILL + 504)
    // 2. isTimedOut() -> cerrar conexiones inactivas
    // 3. estado CLOSING -> delete + removeHandler
}
```

**Por que no se borra durante el dispatch**: las conexiones se marcan `CLOSING` durante el dispatch, pero se borran despues, en `_cleanupConnections`. Esto evita iterator invalidation y use-after-free sobre el puntero `event.data.ptr`.

---

## 10. RAII y gestion de recursos

Aunque C++98 no tiene smart pointers, se usa RAII extensivamente:

- **Destructor de `Connection`**: cierra `close(_clientFd)`, desregistra pipes CGI (`_unregisterCgiPipes()`), elimina el `CgiHandler` si existe.
- **`Reactor::~Reactor()`**: itera sobre `_handlers` y elimina cada `EventHandler*`, luego cierra `_epollFd`.
- **`FD_CLOEXEC`** en sockets de cliente y pipes CGI del padre: no se filtran a futuros procesos hijo si hay otro fork.

Un fd filtrado eventualmente causaria que `epoll_wait` fallara por exceder el limite del sistema.

---

## Comandos de demostracion

```bash
# 1. Arrancar el servidor
make re && ./webserv conf/default.conf

# 2. Concurrencia: 20 requests paralelos con un solo hilo
seq 20 | xargs -P 20 -I{} curl -s -o /dev/null -w "%{http_code}\n" http://localhost:8080/
# Esperado: 20 lineas de "200"

# 3. Keep-alive: dos requests en una conexion
curl -v --keepalive-time 5 http://localhost:8080/ http://localhost:8080/index.html 2>&1 | grep -E "Connected|Re-using"

# 4. Pipelining: dos requests en un solo TCP stream
printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080

# 5. Timeout: abrir conexion y no enviar nada
nc localhost 8080
# (no escribir nada, esperar 60s -> el servidor cierra la conexion)

# 6. Virtual host
curl -v -H "Host: secondary.local" http://localhost:8080/

# 7. Malformed request: no debe crashear
printf 'NOTAMETHOD\r\n\r\n' | nc localhost 8080
# Esperado: 400 Bad Request

# 8. Disconnect abrupto: no debe crashear
curl -v --max-time 1 http://localhost:8080/
```

---

## Preguntas frecuentes del evaluador

**P: Por que epoll y no select?**
R: select tiene limite de 1024 fds y es O(n) por evento. epoll es O(1) y sin limite. Para un servidor con cientos de conexiones en keep-alive, la diferencia es critica.

**P: Por que level-triggered y no edge-triggered?**
R: LT simplifica el codigo: si `recv` devuelve datos, avanzamos; si bloquea, retornamos y epoll re-notifica. Con ET habria que drenar hasta EAGAIN en un bucle, mas propenso a errores.

**P: Como manejas EAGAIN sin revisar errno?**
R: Si `recv` devuelve -1, simplemente retorno. Si fue EAGAIN, epoll re-notificara con EPOLLIN. Si fue error real, epoll disparara EPOLLERR/EPOLLHUP que va a handleError.

**P: Por que solo 1 epoll_fd?**
R: El subject exige un solo `poll()` (o equivalente) para todas las operaciones de I/O. Sockets de escucha, clientes y pipes CGI comparten el mismo epoll.

**P: Por que las conexiones se borran en el cleanup y no durante el dispatch?**
R: Para evitar iterator invalidation y use-after-free. Durante el dispatch, `event.data.ptr` apunta al handler. Si se borra a mitad del bucle, el siguiente evento podria usar un puntero invalido.

**P: Que pasa si el servidor se queda sin memoria?**
R: El subject dice que no debe crashear. Los `new` que fallan lanzan `std::bad_alloc`. En los puntos criticos (aceptar conexion, crear CgiHandler) se podria capturar, pero en la practica el OS hace overcommit y los `new` raramente fallan. El servidor esta disenado para degradar graciosamente: si una conexion falla, se cierra esa conexion, no todo el servidor.
