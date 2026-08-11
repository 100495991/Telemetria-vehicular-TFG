# Publicación del stack — de broker público a broker privado accesible desde internet

Este documento recoge, paso a paso, todo lo hecho para dejar de depender del
broker público `test.mosquitto.org` y publicar el broker MQTT privado
(`mosquitto`, levantado en este mismo `docker-compose.yml`) de forma que el
Arduino (conectado por GPRS/red móvil, no por el WiFi de casa) pueda
alcanzarlo desde cualquier sitio. Sirve como referencia para la memoria del
TFG (capítulo de despliegue/infraestructura).

## Índice

1. [Motivación](#1-motivación)
2. [Cambio de broker: público → privado](#2-cambio-de-broker-público--privado)
3. [Autenticación del broker](#3-autenticación-del-broker)
4. [Firewall de Windows](#4-firewall-de-windows)
5. [IP local fija del PC](#5-ip-local-fija-del-pc)
6. [Port-forwarding en el router (Movistar)](#6-port-forwarding-en-el-router-movistar)
7. [DNS dinámico (DuckDNS)](#7-dns-dinámico-duckdns)
8. [Actualizar el firmware](#8-actualizar-el-firmware)
9. [Verificación end-to-end](#9-verificación-end-to-end)
10. [Alternativa: alojar el servidor en Google Cloud](#10-alternativa-alojar-el-servidor-en-google-cloud)

---

## 1. Motivación

El stack original publicaba y se suscribía en `test.mosquitto.org`, un broker
público de terceros: cualquiera podía leer la telemetría o inyectar tramas
falsas en el topic, y no hay garantía de disponibilidad ni de latencia. El
objetivo es usar el Mosquitto que ya se levanta en `platform/mosquitto/`
(`docker-compose.yml`) como único broker, con autenticación, y exponerlo a
internet solo para el propio dispositivo.

## 2. Cambio de broker: público → privado

Antes, `ingestor` se suscribía al broker público porque así lo fijaba `.env`.
El `docker-compose.yml` ya soportaba apuntar al mosquitto local (variable
`MQTT_BROKER_HOST`), solo había que cambiar el valor:

**`CloudServices/.env`**
```diff
- MQTT_BROKER_HOST=test.mosquitto.org
+ MQTT_BROKER_HOST=mosquitto
  MQTT_BROKER_PORT=1883
```

`mosquitto` es el nombre del servicio en la red interna de Docker Compose, así
que el `ingestor` lo resuelve directamente sin necesidad de IP.

## 3. Autenticación del broker

Como el broker va a quedar expuesto a internet (ver §6), no puede seguir
aceptando conexiones anónimas: cualquiera que escanee el puerto podría leer la
telemetría o publicar tramas falsas. Se creó un usuario `arduino`:

```bash
# Genera el hash de la contraseña dentro del propio contenedor de mosquitto
docker run --rm --entrypoint sh eclipse-mosquitto:2 \
  -c "mosquitto_passwd -c -b /tmp/passwd arduino TU_CLAVE && cat /tmp/passwd"
# copia la línea "arduino:$7$...=" resultante a platform/mosquitto/passwd
```

**`platform/mosquitto/mosquitto.conf`**
```diff
- allow_anonymous true
- # password_file /mosquitto/config/passwd
+ allow_anonymous false
+ password_file /mosquitto/config/passwd
```

**`CloudServices/.env`** (usado por el `ingestor`):
```diff
- MQTT_USER=
- MQTT_PASSWORD=
+ MQTT_USER=arduino
+ MQTT_PASSWORD=TU_CLAVE
```

**Firmware** — `DeviceServices/gprs.h` y `gprs.cpp`:
```diff
+ #define MQTT_USER      "arduino"
+ #define MQTT_PASSWORD  "TU_CLAVE"
```
```diff
- if (mqtt.connect(MQTT_CLIENT_ID)) {
+ if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
```

Aplicar y comprobar:
```bash
docker compose restart mosquitto ingestor

# Sin credenciales debe rechazar la conexión:
docker run --rm --network container:tfg_mosquitto eclipse-mosquitto:2 \
  mosquitto_pub -h localhost -t test -m hola
# → Connection Refused: not authorised

# Con credenciales debe funcionar:
docker run --rm --network container:tfg_mosquitto eclipse-mosquitto:2 \
  mosquitto_pub -h localhost -u arduino -P TU_CLAVE -t test -m hola
```

> Nota: al montar `passwd` como bind mount desde Windows, Mosquitto avisa de
> que el fichero no pertenece al usuario `mosquitto` dentro del contenedor
> (`owner is not mosquitto`). Es una limitación de traducción de permisos
> Windows↔Linux en los bind mounts de Docker Desktop; hoy es solo un aviso,
> no impide arrancar. Si una versión futura de Mosquitto lo convierte en
> error, la solución es mover `passwd` a un volumen Docker con nombre en vez
> de un bind mount.

## 4. Firewall de Windows

Docker Desktop publica los puertos del `docker-compose.yml` en `0.0.0.0` del
host, pero Windows Firewall bloquea por defecto el tráfico entrante. Reglas
necesarias (PowerShell como administrador):

```powershell
New-NetFirewallRule -DisplayName "TFG MQTT" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow
New-NetFirewallRule -DisplayName "TFG Web"  -Direction Inbound -Protocol TCP -LocalPort 8080 -Action Allow
```

Comprobar que el perfil de red activo es "Privado" (no "Público"), donde
Windows es menos restrictivo:

```powershell
Get-NetConnectionProfile
```

## 5. IP local fija del PC

Antes de reenviar puertos, el PC necesita una IP local que no cambie (si no,
la regla de port-forwarding se queda apuntando a una IP que ya no es la del
PC). Dos opciones:

- **IP estática en Windows:** Panel de control → Centro de redes → adaptador
  → Propiedades → IPv4 → fijar IP/máscara/puerta de enlace manualmente.
- **Reserva DHCP en el router (recomendado):** el router siempre asigna la
  misma IP a la MAC del PC; se configura en el propio router (ver §6).

## 6. Port-forwarding en el router (Movistar)

Con router Movistar (HGU / Smart WiFi 6, el que trae la fibra de Movistar) el
panel de administración está en `192.168.1.1`:

1. **Entrar al router:** navegador → `http://192.168.1.1`. Usuario y
   contraseña de administración están impresos en la etiqueta del propio
   router (no es la clave del WiFi). Si nunca se cambió, suele ser el usuario
   `1234` con la clave de la etiqueta.
2. **Reservar IP local del PC:** menú *Configuración avanzada* → *LAN* →
   *Reserva de IP* (a veces bajo *DHCP*). Añade la MAC del PC (`ipconfig /all`
   → "Dirección física" del adaptador activo) y fija la IP que ya tiene, p.
   ej. `192.168.1.33`.
3. **Redirección de puertos:** menú *Configuración avanzada* → *NAT* →
   *Redirección de puertos* (a veces aparece como *Port Forwarding* o
   *Virtual Server*). Crear una regla nueva:
   - Nombre: `TFG MQTT`
   - Protocolo: `TCP`
   - Puerto externo: `1883`
   - Puerto interno: `1883`
   - IP interna: la IP reservada del PC (p. ej. `192.168.1.33`)
   - Repetir para el puerto `8080` (`TFG Web`) solo si quieres el dashboard
     accesible desde fuera de casa.
4. **Guardar y reiniciar el router** si lo pide.
5. **Comprobar CG-NAT:** algunos contratos de fibra/móvil no dan una IP
   pública real, sino una compartida (CG-NAT), lo que **impide** que el
   port-forwarding funcione pese a estar bien configurado. Para comprobarlo:
   compara la IP pública que ves en `https://www.whatismyip.com` desde un
   dispositivo de tu red con la que muestra el propio router en su panel de
   estado (*Estado* → *WAN* / *Internet*). Si coinciden, tienes IP pública
   real y el port-forwarding funcionará. Si no coinciden, tu operador usa
   CG-NAT y hay que pedir una IP pública fija (algunos operadores la dan bajo
   petición o con una tarifa concreta) o usar un servicio de túnel (p. ej.
   Cloudflare Tunnel, ngrok) como alternativa.

## 7. DNS dinámico (DuckDNS)

La IP pública de una conexión doméstica normalmente cambia con el tiempo
(IP dinámica), así que en vez de escribir la IP directamente en el firmware
se usa un dominio DDNS que siempre apunta a la IP actual:

1. Entra en [duckdns.org](https://www.duckdns.org) y accede con una cuenta
   (Google/GitHub/Reddit).
2. Crea un subdominio, p. ej. `tutfg` → queda como `tutfg.duckdns.org`.
   DuckDNS te da un **token** propio de tu cuenta.
3. Instala un actualizador que avise a DuckDNS de tu IP actual cada pocos
   minutos. En Windows, con el Programador de tareas:

   `duckdns_update.ps1`:
   ```powershell
   Invoke-WebRequest -Uri "https://www.duckdns.org/update?domains=tutfg&token=TU_TOKEN&ip=" | Out-Null
   ```

   Crear una tarea programada que lo ejecute cada 5 minutos:
   ```powershell
   $action  = New-ScheduledTaskAction -Execute "powershell.exe" -Argument "-NoProfile -WindowStyle Hidden -File C:\ruta\duckdns_update.ps1"
   $trigger = New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes 5) -RepetitionDuration ([TimeSpan]::MaxValue)
   Register-ScheduledTask -TaskName "DuckDNS Update" -Action $action -Trigger $trigger -RunLevel Highest
   ```
4. Verifica que resuelve a tu IP pública actual:
   ```powershell
   Resolve-DnsName tutfg.duckdns.org
   ```

## 8. Actualizar el firmware

En `DeviceServices/gprs.h`, sustituir el placeholder por el dominio DDNS:

```c
#define MQTT_BROKER  "tutfg.duckdns.org"
#define MQTT_PORT    1883
```

Recompilar y subir el sketch al Arduino.

## 9. Verificación end-to-end

Con el Arduino aún en el banco de pruebas conectado por USB (para ver el log
serie), comprobar en orden:

1. `docker compose ps` en `CloudServices/` → todos los servicios `healthy`/`running`.
2. Desde fuera de la red de casa (p. ej. con datos móviles en el teléfono, sin
   WiFi):
   ```bash
   nc -vz tutfg.duckdns.org 1883      # o Test-NetConnection en PowerShell
   ```
   Debe conectar. Si falla: revisar CG-NAT (§6.5), regla de forwarding, y
   firewall de Windows (§4).
3. Encender el Arduino y mirar el log serie: debe llegar a
   `MQTT: Conectado correctamente`.
4. Comprobar en el dashboard (`http://localhost:8080`, o
   `http://tutfg.duckdns.org:8080` si también reenviaste el 8080) que llegan
   tramas nuevas.

## 10. Alternativa: alojar el servidor en Google Cloud

Todo lo de las secciones 4–7 (firewall casero, IP fija por reserva DHCP,
port-forwarding, CG-NAT, DDNS) existe porque el servidor vive en un PC
doméstico detrás de un router residencial. Mover el mismo `docker-compose.yml`
a una VM de Google Cloud (Compute Engine) elimina buena parte de esa fricción:

- **IP pública real y estable de fábrica** (o una IP estática reservada
  aparte): no hay CG-NAT ni depende de que el operador de casa dé IP pública,
  y no hace falta DDNS — el firmware apunta directo a esa IP o a un registro
  DNS normal (o Cloud DNS si quieres un dominio propio).
- **Reglas de firewall centralizadas en la consola de GCP** (`gcloud compute
  firewall-rules create` o el propio panel web) en vez de depender de la
  interfaz —a veces limitada— del router del operador.
- **Disponibilidad**: la VM no depende de que el PC de casa esté encendido,
  ni de cortes del router doméstico o reinicios del ISP.
- **Coste**: una VM pequeña (`e2-micro`, que entra en el nivel gratuito de
  GCP en regiones concretas de EE.UU., o unos pocos euros/mes en Europa)
  es suficiente para este stack (Mosquitto + MySQL + API + nginx son
  servicios ligeros). Hay que sumarlo como coste de infraestructura si no
  cabe en el free tier.
- **Lo que NO cambia**: el `docker-compose.yml` es el mismo, la
  autenticación del broker (§3) se mantiene igual, y el paso de "generar el
  passwd de mosquitto" es idéntico.

En resumen: para el propósito del TFG (demostrar la arquitectura
end-to-end), exponer el PC de casa es válido y es lo que se ha hecho aquí;
Google Cloud simplifica el despliegue si se busca una demo estable a largo
plazo sin depender de la red doméstica, a cambio de un coste recurrente y de
gestionar credenciales de un proyecto GCP.
