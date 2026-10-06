# TFG - Telemetría vehicular

Sistema que toma datos de un vehículo mientras circula (posición GPS, velocidad,
aceleraciones, temperaturas y datos del OBD2), los guarda en una tarjeta SD, los envía
por red móvil a un servidor y los muestra en una página web con mapa y gráficas.

El proyecto tiene dos partes:

- **DeviceServices**: el programa del Arduino que va en el vehículo.
- **CloudServices**: el servidor que recibe los datos y los muestra.

## Cómo funciona

```
   VEHÍCULO (DeviceServices)                     SERVIDOR (CloudServices)

 GPS ────────┐
 Acelerómetro┤                                 ┌───────────┐   ┌──────────┐
 Temperatura ┼──► Arduino Mega ──► SIM800L ───►│ Mosquitto │──►│ Ingestor │
 OBD2 ───────┘        │            (GPRS/MQTT)  │  (MQTT)   │   └────┬─────┘
                      ├──► Tarjeta SD           └───────────┘        ▼
                      └──► Pantalla TFT                          ┌───────┐
                                                                 │ MySQL │
                                       Navegador ◄── Web ◄── API └───────┘
```

1. El Arduino lee los sensores y guarda cada lectura como una línea de texto (CSV) en la SD.
2. Esa misma línea se envía por GPRS al servidor usando MQTT.
3. El ingestor recibe cada línea y la guarda en la base de datos MySQL.
4. La API lee la base de datos y la página web dibuja los datos en tarjetas, mapa y gráficas.

## Estructura

```
DeviceServices/    Código del Arduino (un archivo por sensor/módulo)
CloudServices/     Servidor, todo en Docker
  ├── platform/        Broker MQTT (Mosquitto)
  ├── microservices/   Base de datos e ingestor
  └── app/             API (backend) y página web (frontend)
```

Más detalle en [`DeviceServices/PROYECTO_TELEMETRIA.md`](DeviceServices/PROYECTO_TELEMETRIA.md),
[`CloudServices/README.md`](CloudServices/README.md) y
[`CloudServices/DEPLOYMENT.md`](CloudServices/DEPLOYMENT.md).

## Cómo hacer el deploy

### 1. Servidor

Necesitas Docker Desktop. Desde la carpeta `CloudServices/`:

```bash
cp .env.example .env
```

Edita `.env` con tus contraseñas y tu API key de CARTO (para el mapa,
https://carto.com/basemaps/apikey).

Después hay que crear dos ficheros con las claves de acceso (no están en git):

```bash
# Usuario del Arduino para el broker MQTT
docker run --rm --entrypoint sh eclipse-mosquitto:2 \
  -c "mosquitto_passwd -c -b /tmp/passwd arduino TU_CLAVE_MQTT && cat /tmp/passwd" \
  > platform/mosquitto/passwd

# Usuario para entrar a la página web
docker run --rm httpd:alpine htpasswd -nbm dashboard TU_CLAVE_WEB \
  > app/frontend/.htpasswd
```

La clave MQTT debe ser la misma que `MQTT_PASSWORD` del `.env`.

Y se arranca todo:

```bash
docker compose up -d --build
```

La web queda en http://localhost:8080. Para pararlo: `docker compose down`.

### 2. Acceso desde internet

El Arduino se conecta por red móvil, así que el servidor tiene que ser alcanzable desde
fuera de casa. Hay que abrir el puerto 1883 en el firewall y en el router (apuntando al PC
del servidor) y usar un dominio DuckDNS para no depender de la IP pública. Los pasos están
explicados en [`CloudServices/DEPLOYMENT.md`](CloudServices/DEPLOYMENT.md).

### 3. Arduino

1. Copia `DeviceServices/secrets.example.h` como `secrets.h` y pon el usuario y la clave MQTT
   (los mismos que en el servidor).
2. En `gprs.h` revisa el PIN de la SIM, el APN de la operadora y `MQTT_BROKER` (el dominio
   DuckDNS).
3. Abre `DeviceServices.ino` en el Arduino IDE, selecciona **Arduino Mega 2560**, compila y sube.
4. Pon la tarjeta SD y la SIM, y alimenta el Arduino.

### 4. Comprobar que funciona

- `docker compose ps` muestra todos los servicios en marcha.
- El monitor serie del Arduino muestra `MQTT: Conectado correctamente`.
- La web empieza a mostrar datos nuevos.
