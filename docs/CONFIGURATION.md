# Configuracion del Servidor

## Formato del Archivo de Configuracion

El archivo de configuracion sigue una sintaxis inspirada en NGINX. Es un formato declarativo basado en bloques y directivas.

### Reglas Lexicas

- Los bloques se abren con `{` y cierran con `}`
- Las directivas terminan con `;`
- Los comentarios empiezan con `#` y van hasta el final de la linea
- Las cadenas pueden estar entre comillas dobles `"` o simples `'` (opcional para tokens simples, obligatorio para valores con espacios)
- Las directivas dentro de un bloque `server` aplican a ese servidor virtual
- Las directivas dentro de un bloque `location` aplican a esa ruta especifica

### Ejemplo Completo

```nginx
# Servidor por defecto
server {
    listen 8080;
    server_name localhost;
    client_max_body_size 10M;

    error_page 404 /errors/404.html;
    error_page 500 502 503 504 /errors/50x.html;

    # Ruta raiz
    location / {
        root /var/www/html;
        index index.html;
        autoindex off;
        allow_methods GET POST;
    }

    # Directorio de uploads
    location /uploads {
        root /var/www/uploads;
        allow_methods POST GET DELETE;
        upload_store /var/www/uploads;
        autoindex on;
    }

    # Redireccion
    location /old-path {
        redirect /new-path;
        allow_methods GET;
    }

    # CGI con Python
    location /cgi-bin {
        root /var/www/cgi-bin;
        allow_methods GET POST;
        cgi_extension .py /usr/bin/python3;
    }

    # CGI con PHP (bonus - multiples CGIs)
    location ~ \.php$ {
        root /var/www/php;
        allow_methods GET POST;
        cgi_extension .php /usr/bin/php-cgi;
    }
}

# Segundo servidor (multiples puertos)
server {
    listen 8081;
    server_name secondary.local;

    location / {
        root /var/www/secondary;
        index index.html;
        allow_methods GET;
    }
}
```

## Directivas Globales (fuera de server)

No hay directivas globales en nuestra implementacion. Todo esta contenido en bloques `server`.

## Directivas del Bloque `server`

| Directiva | Tipo | Requerida | Descripcion |
|-----------|------|-----------|-------------|
| `listen` | `host:port` o `port` | Si | Puerto (y opcionalmente host) de escucha. Ej: `listen 8080`, `listen 127.0.0.1:8080` |
| `server_name` | string | No | Nombre del servidor (virtual host). Default: `""` |
| `client_max_body_size` | size (ej: `10M`, `1K`) | No | Tamano maximo del body del request. Default: `1M` |
| `error_page` | `code [...] uri` | No | Pagina de error personalizada para codigos especificos |
| `location` | bloque | No | Bloque de configuracion de ruta |

## Directivas del Bloque `location`

| Directiva | Tipo | Requerida | Descripcion |
|-----------|------|-----------|-------------|
| `path` | string (implicito) | Si | La ruta del location (ej: `/`, `/uploads`, `/api`) |
| `root` | path | No (default heredado) | Directorio raiz para servir archivos en esta ruta |
| `index` | filename | No | Archivo por defecto cuando se solicita un directorio |
| `autoindex` | `on` \| `off` | No | Mostrar listado de directorios. Default: `off` |
| `allow_methods` | lista de metodos | No | Metodos HTTP permitidos. Default: `GET` |
| `redirect` | uri | No | Redireccion HTTP (301 Moved Permanently) |
| `upload_store` | path | No | Directorio donde guardar archivos subidos |
| `cgi_extension` | `ext interpreter` | No | Extension de archivo CGI y su interprete |

## Reglas de Resolucion de Rutas

### Matching de Location

Cuando llega un request con URI `/kapouet/pouic/toto/pouet`:

1. Se busca el bloque `server` correspondiente (por `listen` y `server_name`)
2. Dentro del servidor, se busca el `location` con el **prefijo mas largo** que haga match
   - Ejemplo: `/kapouet` hace match con `/kapouet/pouic/toto/pouet`
   - Si existe `/kapouet/pouic`, tiene prioridad sobre `/kapouet`
3. Si no hay match exacto ni prefijo, se usa el location `/` (si existe)

### Resolucion de Archivos

Con `location /kapouet { root /tmp/www; }`:

```
Request URI: /kapouet/pouic/toto/pouet
Ruta en disco: /tmp/www/pouic/toto/pouet
```

Nota: La parte del path del location se elimina y se anade el resto al root.

### Directory Listing (Autoindex)

Si `autoindex on` y el request apunta a un directorio:
1. Si existe el archivo `index` (ej: `index.html`), se sirve ese archivo
2. Si no, se genera un HTML con el listado de archivos del directorio

Si `autoindex off` y no hay index -> `403 Forbidden`

## Paginas de Error por Defecto

Si no se especifica `error_page`, el servidor usa paginas de error embebidas en codigo:

```html
<!DOCTYPE html>
<html>
<head><title>404 Not Found</title></head>
<body>
<center><h1>404 Not Found</h1></center>
<hr><center>webserv/1.0</center>
</body>
</html>
```

Las paginas por defecto deben existir para al menos:
- 400 Bad Request
- 403 Forbidden
- 404 Not Found
- 405 Method Not Allowed
- 413 Payload Too Large
- 500 Internal Server Error
- 502 Bad Gateway (CGI error)
- 504 Gateway Timeout (CGI timeout)

## Validaciones del Parser

El parser debe detectar y reportar errores como:

- Bloque sin cerrar
- Directiva sin `;`
- `listen` sin puerto o con puerto invalido
- `allow_methods` con metodos no soportados
- `client_max_body_size` con formato invalido
- `location` duplicado exacto en el mismo server
- Valores vacios donde no se permiten

**Comportamiento ante errores de config**: El servidor imprime el error y termina con exit code != 0.

## Archivo de Configuracion por Defecto

Si el usuario no proporciona archivo de configuracion:

```bash
./webserv                    # Usa conf/default.conf
./webserv mi_config.conf     # Usa mi_config.conf
```

El archivo `conf/default.conf` debe estar incluido en la entrega y contener un servidor funcional en el puerto 8080.

## Notas de Implementacion

### Tamano de Body

`client_max_body_size` acepta sufijos:
- `K` o `k`: Kilobytes
- `M` o `m`: Megabytes
- `G` o `g`: Gigabytes
- Sin sufijo: Bytes

Ejemplos: `1K`, `10M`, `1G`, `1024`

### Herencia

Las directivas de `server` no se heredan automaticamente en `location`. Cada location debe ser autocontenido. Sin embargo, para simplificar:
- Si `root` no se especifica en location, se puede usar un valor por defecto del sistema o requerirlo explicitamente
- Recomendacion: Cada location debe tener su `root` explicito para evitar confusion

### Multiples Listen

Un server puede escuchar en multiples puertos:

```nginx
server {
    listen 8080;
    listen 8081;
    listen 127.0.0.1:9090;
    ...
}
```

Esto crea multiples `ServerSocket` dentro del mismo `ServerConfig`.

## Estructuras de Datos Propuestas

```cpp
struct RouteConfig {
    std::string path;
    std::string root;
    std::string index;
    bool autoindex;
    std::vector<std::string> allowedMethods;
    std::string redirect;
    std::string uploadStore;
    std::map<std::string, std::string> cgiHandlers; // ".py" -> "/usr/bin/python3"
};

struct ServerConfig {
    std::vector<std::pair<std::string, int> > listens; // (host, port)
    std::string serverName;
    size_t clientMaxBodySize;
    std::map<int, std::string> errorPages; // 404 -> "/errors/404.html"
    std::vector<RouteConfig> routes;
};
```
