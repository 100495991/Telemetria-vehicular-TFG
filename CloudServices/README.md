# CloudServices — Plataforma de telemetría (TFG UC3M)

Stack completo, autocontenido en Docker, que recibe la telemetría del vehículo
por MQTT, la almacena en MySQL y la muestra en un dashboard web.

```
  Arduino (SIM800L)
        │  MQTT (1883)
        ▼
   ┌─────────────┐     ┌────────────┐     ┌──────────┐
   │  mosquitto  │ ──► │  ingestor  │ ──► │  MySQL   │
   │  (broker)   │     │ MQTT→MySQL │     │   (db)   │
   └─────────────┘     └────────────┘     └────┬─────┘
                                               │
                        ┌──────────┐     ┌─────▼──────┐
        navegador  ◄──► │   web    │ ──► │    api     │
        (8080)          │ (nginx)  │     │ (FastAPI)  │
                        └──────────┘     └────────────┘
```

Los servicios se reparten por capas:

| Servicio   | Carpeta                       | Qué hace                                          |
|------------|-------------------------------|---------------------------------------------------|
| mosquitto  | `platform/mosquitto`          | Broker MQTT privado (con autenticación)           |
| db         | `microservices/database/init` | MySQL 8; esquema base en `init/01_schema.sql`     |
| ingestor   | `microservices/database`      | Suscribe a MQTT y escribe cada trama en MySQL     |
| api        | `app/backend`                 | API de solo lectura (FastAPI) sobre MySQL         |
| web        | `app/frontend`                | nginx: sirve el dashboard y hace de proxy a la API|

```
CloudServices/
├── platform/          broker MQTT + message_router (suscriptor a CSV original)
│   ├── mosquitto/
│   └── message_router/
├── microservices/     gestión de la BBDD
│   └── database/      esquema (init/) + ingestor MQTT→MySQL
├── app/               aplicación web
│   ├── backend/       API FastAPI
│   └── frontend/      dashboard (nginx + html/)
└── docker-compose.yml
```

---

## Puesta en marcha

Requiere **Docker Desktop**. Desde esta carpeta (`CloudServices/`):

```bash
cp .env.example .env      # revisa/ajusta contraseñas (ya hay un .env con valores por defecto)
docker compose up -d --build
```

La primera vez, MySQL tarda ~20-30 s en inicializar la base de datos; el
ingestor y la API esperan a que esté sana antes de arrancar.

- **Dashboard:** http://localhost:8080 (pide usuario/contraseña, ver abajo)
- **API + documentación interactiva:** http://localhost:8080/api/docs
- **MySQL** (para Workbench, DBeaver…): `localhost:3306`, usuario/clave del `.env`

Comprobar que está todo arriba:

```bash
docker compose ps
curl -u dashboard:TU_CLAVE http://localhost:8080/api/health      # {"status":"ok","db":"up"}
```

### Autenticación del dashboard (obligatoria si expones el 8080 a internet)

`app/frontend/nginx.conf` exige autenticación básica HTTP para todo el sitio
(dashboard y `/api`), usando `app/frontend/.htpasswd` (gitignored, no forma
parte de la imagen versionada). Sin login, cualquiera con la URL vería la
ubicación en vivo del vehículo. Regenerar credenciales:

```bash
docker run --rm httpd:alpine htpasswd -nbm dashboard TU_CLAVE_NUEVA
# copia la linea "dashboard:$apr1$..." resultante a app/frontend/.htpasswd
docker compose up -d --build web
```

Si solo vas a usar el dashboard en tu red local (sin reenviar el 8080 en el
router), esta autenticación es una capa extra pero no crítica; si lo expones
a internet, es imprescindible.

Parar / borrar:

```bash
docker compose down          # para los contenedores (conserva los datos)
docker compose down -v       # además borra la base de datos y los volúmenes
```

---

## Configurar el Arduino

El Arduino se conecta por GPRS (red móvil de la operadora), no por el WiFi de
casa, así que `MQTT_BROKER` en `src/DeviceServices/gprs.h` **no** puede ser
una IP local (`192.168.1.x`) ni `localhost`: tiene que ser una dirección
alcanzable desde internet. Además el broker exige usuario/contraseña (ver
siguiente sección), así que el firmware también necesita `MQTT_USER` /
`MQTT_PASSWORD`.

```c
#define MQTT_BROKER    "tutfg.duckdns.org"   // dominio DDNS que apunta a tu IP publica
#define MQTT_PORT      1883
#define MQTT_USER      "arduino"
#define MQTT_PASSWORD  "TU_CLAVE"
```

Todo el proceso para llegar hasta ahí (firewall, IP fija, port-forwarding en
el router, DNS dinámico) está documentado paso a paso en
[`DEPLOYMENT.md`](DEPLOYMENT.md).

Los topics (`MQTT_TOPIC`, `MQTT_TOPIC_CABECERA`) deben coincidir con los del
`.env`. El firmware publica primero la **cabecera con retain** y luego las
tramas de datos; el ingestor usa esa cabecera para mapear cada campo a su
columna.

**Esquema dinámico:** la tabla `telemetria` arranca solo con `id` +
`received_at`; el resto de columnas las crea el ingestor a partir de la
cabecera. Las columnas conocidas (mapa `COLUMNAS` en
`microservices/database/ingestor.py`) se crean con su tipo correcto; cualquier
columna nueva que aparezca en la cabecera se crea sola como `VARCHAR`. Por eso
**añadir un sensor al firmware no obliga a tocar la plataforma**: la columna
aparece automáticamente. Si quieres que tenga un tipo concreto (p. ej. `DOUBLE`
en vez de `VARCHAR`), la añades a ese mapa.

---

## Prueba sin Arduino

Puedes simular al vehículo publicando a mano (con el stack levantado):

```bash
# Cabecera (retained). Ajusta las columnas si cambia el firmware.
docker exec tfg_mosquitto mosquitto_pub -t "uc3m/tfg/telemetria/cabecera" -r \
  -m "timestamp_local,status,latitud,latitud_hemisferio,longitud,longitud_hemisferio,velocidad_kmh,rumbo_grados,fix_quality,fix_quality_desc,n_satelites_en_uso,altitud_m,hdop,hdop_calidad,accel_ax_g,accel_ay_g,accel_az_g,accel_roll_deg,accel_pitch_deg,accel_temperature,temperatura_ambiente,temperatura_neumatico,gprs_conectado,gprs_rssi_pct,gprs_paquetes_enviados"

# Una trama de datos
docker exec tfg_mosquitto mosquitto_pub -t "uc3m/tfg/telemetria" \
  -m "2026-07-23T18:55:01,A,40.3331,N,-3.7672,W,52.3,181.5,1,GPS fix,8,660.5,1.20,Buena,0.0121,0.0264,1.0377,-7.9,3.65,38.87,31.17,45.9,1,93,1"
```

Refresca el dashboard y verás la fila reflejada en las tarjetas, el mapa y las
gráficas.

---

## Autenticación del broker

El broker **exige** usuario y contraseña (`allow_anonymous false` en
`platform/mosquitto/mosquitto.conf`), necesario porque queda expuesto a
internet para que el Arduino lo alcance por GPRS. El usuario `arduino` y su
hash ya están en `platform/mosquitto/passwd`; el `ingestor` lee las
credenciales de `MQTT_USER` / `MQTT_PASSWORD` en `.env`, y el firmware las
manda en `mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)`.

Para regenerar la contraseña o dar de alta otro dispositivo:

```bash
docker run --rm --entrypoint sh eclipse-mosquitto:2 \
  -c "mosquitto_passwd -c -b /tmp/passwd arduino TU_CLAVE_NUEVA && cat /tmp/passwd"
# copia la linea resultante a platform/mosquitto/passwd, actualiza .env y el firmware
docker compose restart mosquitto ingestor
```

Ver [`DEPLOYMENT.md`](DEPLOYMENT.md) para el proceso completo de publicación
(firewall, port-forwarding, DDNS).

---

## Notas

- **Datos ausentes:** cuando un módulo no tiene lectura válida, el firmware
  envía el campo vacío; el ingestor lo guarda como `NULL` (no 0), y las gráficas
  dejan un hueco en vez de dibujar un valor falso.
- **El dashboard usa Leaflet y Chart.js desde CDN**, así que el navegador
  necesita internet para el mapa y las librerías. El resto del stack es local.
- **`message_router.py`** (en `platform/message_router/`) es el suscriptor
  original que volcaba a CSV. Se conserva como herramienta independiente; el
  ingestor de este stack lo sustituye para el flujo a base de datos.
