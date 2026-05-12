# Estrategia de Testing

## Filosofia de Testing

Dado que Webserv es un servidor HTTP, la mejor forma de testearlo es enviandole requests HTTP reales y verificando las respuestas. Usaremos Python para los tests automatizados porque:
- Tiene excelentes librerias HTTP (`requests`, `http.client`, `urllib`)
- Facil de escribir y mantener
- Permite tests de integracion realistas
- Permite tests de stress y concurrencia

## Tipos de Tests

### 1. Tests Unitarios (C++)
Tests para componentes individuales como `HttpParser`, `ConfigParser`, `StringUtils`. Se implementan con un framework ligero o assertions manual.

**Ejemplo conceptual:**
```cpp
// Test de HttpParser
void testParseSimpleGet() {
    HttpParser parser;
    Request req;
    std::string raw = "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ParseResult res = parser.parse(raw, req);
    assert(res == PARSE_OK);
    assert(req.getMethod() == "GET");
    assert(req.getUri() == "/index.html");
}
```

### 2. Tests de Integracion (Python)
Tests que levantan el servidor y envian requests HTTP reales. Son los mas importantes.

**Framework**: `pytest` o `unittest` de Python.

**Ejemplo:**
```python
import requests
import subprocess
import time
import os

BASE_URL = "http://localhost:8080"

class TestBasicRequests:
    @classmethod
    def setup_class(cls):
        # Levantar servidor
        cls.server = subprocess.Popen(
            ["./webserv", "conf/tests/simple.conf"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(1)  # Esperar a que inicie

    @classmethod
    def teardown_class(cls):
        cls.server.terminate()
        cls.server.wait()

    def test_get_index(self):
        resp = requests.get(f"{BASE_URL}/")
        assert resp.status_code == 200
        assert "<html>" in resp.text

    def test_get_404(self):
        resp = requests.get(f"{BASE_URL}/nonexistent")
        assert resp.status_code == 404

    def test_post_upload(self):
        files = {'file': ('test.txt', 'hello world')}
        resp = requests.post(f"{BASE_URL}/uploads", files=files)
        assert resp.status_code == 200 or resp.status_code == 201

    def test_delete_file(self):
        # Primero crear archivo
        requests.post(f"{BASE_URL}/uploads", files={'file': ('delete_me.txt', 'content')})
        # Luego borrarlo
        resp = requests.delete(f"{BASE_URL}/uploads/delete_me.txt")
        assert resp.status_code == 200 or resp.status_code == 204
```

### 3. Tests de Stress (Python + herramientas externas)
- **Concurrencia**: Lanzar 100 requests simultaneos con `threading` o `asyncio`
- **Conexiones persistentes**: Enviar multiples requests sobre la misma conexion
- **Carga**: Enviar archivos grandes, muchas conexiones rapidas
- **Herramientas**: `ab` (Apache Bench), `wrk`, `siege`, o scripts Python con `aiohttp`

### 4. Tests Manuales con Navegador
- Abrir el servidor en Chrome/Firefox
- Probar navegacion por directorios (autoindex)
- Subir archivos via formulario HTML
- Probar redirecciones
- Inspeccionar headers con DevTools

### 5. Tests con Telnet/Curl
Para validar comportamientos especificos del protocolo:

```bash
# Test basico
curl -v http://localhost:8080/

# Test con headers personalizados
curl -v -H "X-Custom: value" http://localhost:8080/

# Test POST
curl -v -X POST -d "name=test" http://localhost:8080/upload

# Test DELETE
curl -v -X DELETE http://localhost:8080/uploads/file.txt

# Test chunked encoding
curl -v -H "Transfer-Encoding: chunked" -d "test data" http://localhost:8080/upload

# Test con telnet para control total
telnet localhost 8080
GET / HTTP/1.1
Host: localhost

```

## Estructura de Tests

```
tests/
├── conftest.py              # Fixtures compartidos de pytest
├── test_basic.py            # GET, POST, DELETE basicos
├── test_config.py           # Tests de parser de configuracion
├── test_routing.py          # Matching de rutas, redirecciones
├── test_cgi.py              # Ejecucion de scripts CGI
├── test_upload.py           # Subida de archivos
├── test_errors.py           # Paginas de error, codigos de status
├── test_autoindex.py        # Listado de directorios
├── test_stress.py           # Tests de carga y concurrencia
├── test_chunked.py          # Transfer-Encoding: chunked
├── test_keepalive.py        # Conexiones persistentes
├── test_session.py          # Cookies y sesiones (bonus)
└── fixtures/
    ├── test_files/          # Archivos de prueba
    │   ├── small.txt
    │   ├── large.bin
    │   └── image.png
    └── expected/
        └── autoindex.html   # Output esperado de autoindex
```

## Escenarios de Test Importantes

### Obligatorios

1. **Servir archivo estatico**: `GET /index.html` -> 200 con contenido correcto
2. **Archivo no encontrado**: `GET /noexist` -> 404
3. **Metodo no permitido**: `DELETE` en ruta que solo permite `GET` -> 405
4. **Upload de archivo**: `POST` con multipart/form-data -> archivo creado en disco
5. **Delete de archivo**: `DELETE /uploads/file.txt` -> archivo eliminado
6. **Redireccion**: `GET /old` -> 301/302 a `/new`
7. **Autoindex**: `GET /uploads/` -> listado HTML
8. **CGI basico**: `GET /cgi-bin/hello.py` -> output del script
9. **CGI con POST**: Formulario que envia datos al CGI
10. **Body muy grande**: Exceder `client_max_body_size` -> 413
11. **Request malformado**: `GETHTTP/1.1` -> 400

### Stress / Resilience

12. **100 conexiones concurrentes**: El servidor no debe caerse
13. **Request incompleto**: Cliente envia solo parte del request y cierra -> no crash
14. **Cliente lento**: Enviar 1 byte por segundo -> timeout o procesamiento correcto
15. **CGI que no termina**: Script infinito -> 504 Gateway Timeout
16. **Memory stress**: Muchas conexiones simultaneas -> no leak de memoria
17. **Multiple ports**: Servidores en 8080 y 8081 sirven contenido diferente

### Bonus

18. **Cookies**: Servidor envia `Set-Cookie`, cliente lo devuelve
19. **Sesiones**: Datos persistentes entre requests via cookies
20. **Multiple CGI**: Python y PHP funcionan correctamente

## Automatizacion

### Makefile targets para tests

```makefile
test: all
	@echo "Running Python integration tests..."
	cd tests && python3 -m pytest -v

test-stress: all
	@echo "Running stress tests..."
	cd tests && python3 test_stress.py

test-cgi: all
	@echo "Running CGI tests..."
	cd tests && python3 test_cgi.py -v
```

### CI (GitHub Actions)

Opcional pero util para el equipo:

```yaml
# .github/workflows/test.yml
name: Tests
on: [push, pull_request]
jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: make
      - name: Install Python deps
        run: pip install pytest requests
      - name: Run tests
        run: cd tests && pytest -v
```

## Validacion Contra NGINX

Para asegurar que nuestro servidor se comporta correctamente, comparar headers y respuestas con NGINX:

```bash
# Levantar NGINX con la misma config
sudo nginx -c /path/to/nginx.conf

# Comparar respuestas
curl -I http://localhost:80/index.html > nginx_headers.txt
curl -I http://localhost:8080/index.html > webserv_headers.txt
diff nginx_headers.txt webserv_headers.txt
```

**Headers a prestar atencion:**
- `Content-Type`
- `Content-Length`
- `Connection: keep-alive/close`
- `Server`
- `Date`

## Herramientas Utiles

- `curl`: Tests manuales rapidos
- `wget`: Descargas y validacion
- `telnet`: Control completo del protocolo
- `netcat`: Tests de conexion y envio raw
- `strace`: Debugging de syscalls
- `valgrind`: Deteccion de memory leaks
- `lsof`: Ver sockets abiertos
- `ps`: Ver procesos CGI activos

## Lista de Verificacion Pre-Entrega

- [ ] Compila con `-Wall -Wextra -Werror -std=c++98` sin warnings
- [ ] No hay memory leaks (valgrind clean)
- [ ] Todos los tests de integracion pasan
- [ ] Stress test con 100+ conexiones concurrentes pasa
- [ ] CGI funciona con Python
- [ ] Upload y delete funcionan
- [ ] Paginas de error por defecto funcionan
- [ ] Redirecciones funcionan
- [ ] Autoindex funciona
- [ ] Server no crashea con requests malformados
- [ ] Server no crashea con clientes que desconectan abruptamente
- [ ] Multiple ports funcionan
- [ ] Archivo de configuracion por defecto funciona
- [ ] README.md esta completo
- [ ] Bonus: Cookies/sesiones funcionan
- [ ] Bonus: Multiple CGI (PHP) funciona
