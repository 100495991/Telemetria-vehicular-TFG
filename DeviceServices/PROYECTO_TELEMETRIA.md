# TFG — Dinámica Vehicular: Sistema de Telemetría Vehicular con Arduino
## Entregas 1 a 8 — Guía Técnica Completa

> **Tutor:** Carlos Rodríguez Sánchez  
> **Institución:** Universidad Carlos III de Madrid (UC3M)  
> **Hardware base:** Arduino Mega 2560 + Módulo GPS NEO-6M + Módulo MicroSD + Acelerómetro 3 ejes + Sensor IR MLX90614 + Pantalla TFT ILI9488 + ELM327 OBD2 + Módulo GPRS SIM800L

---

## Índice

1. [Entrega 1 — Puesta en marcha: Arduino, GPS y protocolo NMEA](#entrega-1)
2. [Entrega 2 — Almacenamiento en tarjeta SD (bus SPI)](#entrega-2)
3. [Entrega 2B — Optimización GPS y ficheros SD estructurados](#entrega-2b)
4. [Entrega 3 — Acelerómetro 3 ejes](#entrega-3)
5. [Entrega 4 — Sensor de temperatura infrarroja MLX90614](#entrega-4)
6. [Entrega 5 — Pantalla TFT táctil ILI9488](#entrega-5)
7. [Entrega 6 — OBD2 con ELM327](#entrega-6)
8. [Entrega 7 — Comunicaciones GPRS/MQTT con SIM800L](#entrega-7)
9. [Entrega 8 — Integración final, encapsulado y TFG escrito](#entrega-8)

---

## Entrega 1 — Puesta en Marcha: Arduino, GPS y Protocolo NMEA {#entrega-1}

### Objetivo

Instalar el entorno de desarrollo Arduino, familiarizarse con la placa Mega 2560, establecer comunicación con el módulo GPS NEO-6M mediante un convertidor USB-TTL, interpretar el protocolo NMEA 0183, y generar un primer programa que presente los datos de posicionamiento por el terminal serie.

---

### 1. Hardware necesario

| Componente | Descripción |
|-----------|-------------|
| **Arduino Mega 2560 R3** | Placa principal. Elegir cualquier MEGA 2560R3 (ej: AZDelivery) |
| **Módulo GPS NEO-6M (GY-NEO6MV2)** | Con antena cerámica, cable conector y pila de botón CR1220 |
| **Convertidor USB a TTL (CP2102 o CH340)** | Para comunicar directamente el GPS con el portátil antes de conectarlo al Arduino |
| **Kit de cables Dupont** | Macho-macho, macho-hembra para conexionado |

> ⚠️ Verificar al comprar el GPS:
> - Que incluya la **antena cerámica** (el cuadradito blanco) y su cable conector.
> - Que lleve **pila de botón** (CR1220) para retener los datos de la última sesión.
> - Si no trae pines soldados, habrá que soldarlos antes de poder conectar cables.

---

### 2. Instalación del entorno

1. Descargar e instalar **Arduino IDE** desde [arduino.cc](https://www.arduino.cc/).
2. Instalar los drivers del convertidor USB-TTL:
   - **CP2102:** Driver de Silicon Labs.
   - **CH340:** Driver CH340/CH341.
3. Verificar que el puerto COM aparece en el Administrador de Dispositivos (Windows) o en `/dev/ttyUSB*` (Linux/Mac).
4. Instalar la librería **TinyGPS++** desde el Gestor de Librerías del IDE:  
   `Herramientas → Administrar Bibliotecas → buscar "TinyGPSPlus"`

---

### 3. Protocolo NMEA 0183

El módulo NEO-6M envía tramas de texto en formato **NMEA 0183** (estándar de la National Marine Electronics Association). Cada trama comienza con `$` y termina con `*XX` (checksum).

#### Tramas principales

| Trama | Contenido |
|-------|-----------|
| `$GPGGA` | Posición fija: lat, lon, altitud, HDOP, número de satélites |
| `$GPRMC` | Datos mínimos: timestamp, lat, lon, velocidad, rumbo |
| `$GPGLL` | Posición geográfica (lat/lon) |
| `$GPGSA` | Satélites activos y DOP |
| `$GPGSV` | Satélites en vista (elevación, azimut, SNR) |
| `$GPVTG` | Velocidad y rumbo respecto al suelo |

#### Ejemplo de trama GGA

```
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
         │       │         │           │  │   │    │
         │       │         │           │  │   │    └── Altitud (m)
         │       │         │           │  │   └─────── HDOP
         │       │         │           │  └─────────── Núm. satélites
         │       │         │           └────────────── Calidad fix (1=GPS)
         │       │         └────────────────────────── Longitud
         │       └──────────────────────────────────── Latitud
         └──────────────────────────────────────────── Hora UTC (HHMMSS)
```

#### ¿Qué es el HDOP?

El **HDOP (Horizontal Dilution of Precision)** es un indicador de la calidad geométrica de los satélites disponibles. Cuanto más bajo, mejor la precisión horizontal.

| HDOP | Calidad |
|------|---------|
| < 1 | Excelente |
| 1 – 2 | Bueno |
| 2 – 5 | Moderado |
| 5 – 10 | Regular |
| > 10 | Muy malo |

> Para maximizar la precisión: colocar la antena en **exterior con cielo despejado** y esperar a que el número de satélites sea ≥ 6 y el HDOP < 2.

---

### 4. Comunicación GPS ↔ USB-TTL ↔ Portátil

Antes de conectar el GPS al Arduino, se debe verificar que funciona correctamente comunicándolo directamente con el portátil.

#### Conexionado USB-TTL → GPS NEO-6M

| Pin GPS | Pin USB-TTL |
|---------|------------|
| TX | RX del TTL |
| RX | TX del TTL |
| VCC | 3.3V o 5V |
| GND | GND |

> ⚠️ **Cruzar siempre TX↔RX.** El TX de un dispositivo va al RX del otro.

#### Configuración del terminal serie

- Abrir Arduino IDE → Monitor Serie, o cualquier terminal (PuTTY, CoolTerm).
- Velocidad: **9600 baudios**, 8N1.
- Deberían aparecer tramas NMEA cada segundo.

#### Herramienta u-center

Descargar **u-center v22.05** (software oficial de u-blox para configurar el módulo):

- Permite ver las constelaciones GPS (americanos, GLONASS rusos) en tiempo real.
- Muestra el número de satélites, SNR, HDOP, posición en mapa.
- Permite configurar la frecuencia de refresco y los mensajes NMEA activos.

> El módulo NEO-6M puede recibir tanto satélites **GPS (americanos)** como **GLONASS (rusos)**, lo que aumenta el número de satélites disponibles y mejora el HDOP.

---

### 5. Conexionado GPS → Arduino Mega

Una vez verificado el funcionamiento con el PC, conectar al Arduino:

| Pin GPS | Pin Arduino Mega |
|---------|-----------------|
| TX | RX1 (Pin 19) |
| RX | TX1 (Pin 18) |
| VCC | 5V |
| GND | GND |

> Se usa **Serial1** (hardware) del Mega. Nunca usar SoftwareSerial para GPS a alta velocidad.

---

### 6. Código base — GPS con Arduino

```cpp
// gps_setup.ino

#include <TinyGPSPlus.h>

#define SERIAL_GPS  Serial1
#define BAUD_GPS    9600

TinyGPSPlus gps;

struct DatosGPS {
  double   latitud;
  double   longitud;
  double   altitud;    // metros
  double   velocidad;  // km/h
  double   hdop;
  int      satellites;
  // Timestamp
  int      anyo, mes, dia;
  int      hora, minuto, segundo, centesima;
  bool     valido;
};

DatosGPS datoGPS;

// -----------------------------------------------
void setupGPS() {
  SERIAL_GPS.begin(BAUD_GPS);
  Serial.println("[GPS] Inicializando a 9600 baud...");
}

// -----------------------------------------------
void leerGPS() {
  while (SERIAL_GPS.available() > 0) {
    gps.encode(SERIAL_GPS.read());
  }

  if (gps.location.isUpdated() && gps.location.isValid()) {
    datoGPS.latitud    = gps.location.lat();
    datoGPS.longitud   = gps.location.lng();
    datoGPS.altitud    = gps.altitude.meters();
    datoGPS.velocidad  = gps.speed.kmph();
    datoGPS.hdop       = gps.hdop.hdop();
    datoGPS.satellites = gps.satellites.value();

    datoGPS.anyo      = gps.date.year();
    datoGPS.mes       = gps.date.month();
    datoGPS.dia       = gps.date.day();
    datoGPS.hora      = gps.time.hour();
    datoGPS.minuto    = gps.time.minute();
    datoGPS.segundo   = gps.time.second();
    datoGPS.centesima = gps.time.centisecond();
    datoGPS.valido    = true;
  }
}

// -----------------------------------------------
// Generar string de timestamp: YYMMDDHHMMSS
String getTimestamp() {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02d%02d%02d%02d%02d%02d",
           datoGPS.anyo % 100, datoGPS.mes,    datoGPS.dia,
           datoGPS.hora,       datoGPS.minuto, datoGPS.segundo);
  return String(buf);
}

// -----------------------------------------------
void imprimirGPS() {
  if (!datoGPS.valido) {
    Serial.println("[GPS] Sin fix válido...");
    return;
  }
  Serial.print("Timestamp: "); Serial.println(getTimestamp());
  Serial.print("Lat: ");       Serial.println(datoGPS.latitud,  6);
  Serial.print("Lon: ");       Serial.println(datoGPS.longitud, 6);
  Serial.print("Alt (m): ");   Serial.println(datoGPS.altitud,  1);
  Serial.print("Vel (km/h): ");Serial.println(datoGPS.velocidad,1);
  Serial.print("HDOP: ");      Serial.println(datoGPS.hdop,     2);
  Serial.print("Satélites: "); Serial.println(datoGPS.satellites);
  Serial.println("---");
}
```

```cpp
// main.ino  (programa principal)

void setup() {
  Serial.begin(115200);   // Monitor serie del portátil
  setupGPS();
}

void loop() {
  leerGPS();
  imprimirGPS();
  delay(1000);  // 1 Hz por defecto
}
```

---

### 7. Notas sobre alta frecuencia de muestreo (opcional)

El NEO-6M admite hasta **5 Hz** de refresco y velocidades de hasta **115200 baudios**. Aumentar la frecuencia requiere enviar comandos UBX propietarios al módulo (detallado en la Entrega 2B). No intentarlo hasta tener el sistema estable a 9600 baud / 1 Hz.

---

## Entrega 2 — Almacenamiento en Tarjeta SD (Bus SPI) {#entrega-2}

### Objetivo

Guardar los datos del GPS en una tarjeta SD en formato CSV, con un nombre de fichero único basado en el timestamp, gestionando correctamente los errores posibles (SD no insertada, fichero inexistente, etc.), y organizar el código en múltiples ficheros siguiendo buenas prácticas.

---

### 1. Hardware

| Componente | Descripción |
|-----------|-------------|
| **Módulo MicroSD** | Módulo SPI para Arduino (con regulador 3.3V integrado) |
| **Tarjeta MicroSD** | Cualquier tarjeta de hasta 32 GB formateada en **FAT32** |

> ⚠️ Formatear siempre la tarjeta en **FAT32** (no exFAT ni NTFS). La librería SD de Arduino solo soporta FAT16/FAT32.

---

### 2. Bus SPI — Conexionado

El módulo SD se comunica por **SPI (Serial Peripheral Interface)**, un bus síncrono de 4 hilos.

| Pin Módulo SD | Pin Arduino Mega | Función |
|--------------|-----------------|---------|
| CS (SS) | Pin 53 | Chip Select (selecciona el dispositivo) |
| MOSI | Pin 51 | Master Out Slave In (datos hacia SD) |
| MISO | Pin 50 | Master In Slave Out (datos desde SD) |
| SCK | Pin 52 | Reloj SPI |
| VCC | 5V | Alimentación |
| GND | GND | Masa |

> En el Arduino Mega los pines SPI hardware son fijos: 50 (MISO), 51 (MOSI), 52 (SCK). El CS (pin 53) puede cambiarse por software.

---

### 3. Librería

```
SD.h  (incluida en Arduino IDE por defecto)
SPI.h (incluida en Arduino IDE por defecto)
```

---

### 4. Estructura de ficheros en la SD

```
SD:/
├── 250129093045.csv    ← Fichero de datos (nombre único por sesión)
├── 250130114500.csv    ← Sesión anterior
└── LOG.TXT             ← Fichero de log del sistema (único, persistente)
```

#### Formato CSV del fichero de datos

```
TIMESTAMP,LAT,LON,ALT,VEL,HDOP,SATS
20250129:093045,40.416775,-3.703790,650.5,0.00,1.2,9
20250129:093046,40.416776,-3.703791,650.6,0.00,1.1,10
```

---

### 5. Código modular

#### Fichero `sd_manager.ino`

```cpp
// sd_manager.ino
// Gestión completa de la tarjeta SD: inicialización, escritura de datos y log.

#include <SD.h>
#include <SPI.h>

#define PIN_SD_CS  53

File dataFile;
File logFile;
char dataFileName[20];   // Nombre único del fichero CSV de esta sesión
bool sdOK        = false;
bool dataFileOpen = false;

// -----------------------------------------------
bool setupSD() {
  Serial.print("[SD] Inicializando...");
  if (!SD.begin(PIN_SD_CS)) {
    Serial.println(" ERROR - SD no detectada o fallo de formato");
    sdOK = false;
    return false;
  }
  sdOK = true;
  Serial.println(" OK");

  // Abrir (o crear) fichero de log persistente
  logFile = SD.open("LOG.TXT", FILE_WRITE);
  if (!logFile) {
    Serial.println("[SD] ADVERTENCIA - No se pudo abrir LOG.TXT");
  }
  logEvent("=== SISTEMA INICIADO ===");
  logEvent("SD: Inicializacion correcta");
  return true;
}

// -----------------------------------------------
// Escribir evento en el fichero de log
void logEvent(const char* msg) {
  Serial.print("[LOG] "); Serial.println(msg);
  if (!sdOK || !logFile) return;
  if (datoGPS.valido) {
    logFile.print("[");
    logFile.print(getTimestamp());
    logFile.print("] ");
  }
  logFile.println(msg);
  logFile.flush();
}

// -----------------------------------------------
// Crear el fichero CSV de datos con nombre único (llamar tras primer fix GPS)
bool createDataFile() {
  if (!sdOK) return false;
  if (dataFileOpen) return true;   // Ya creado

  // Nombre: YYMMDDHHMMSS.csv
  snprintf(dataFileName, sizeof(dataFileName), "%s.csv", getTimestamp().c_str());

  // Verificar si ya existe (no debería, el timestamp es único)
  if (SD.exists(dataFileName)) {
    logEvent("SD: AVISO - Fichero ya existe, sobreescribiendo");
    SD.remove(dataFileName);
  }

  dataFile = SD.open(dataFileName, FILE_WRITE);
  if (!dataFile) {
    logEvent("SD: ERROR - No se pudo crear fichero de datos");
    return false;
  }

  // Cabecera CSV
  dataFile.println("TIMESTAMP,LAT,LON,ALT,VEL,HDOP,SATS");
  dataFile.flush();
  dataFileOpen = true;

  char msg[50];
  snprintf(msg, sizeof(msg), "SD: Fichero creado: %s", dataFileName);
  logEvent(msg);
  return true;
}

// -----------------------------------------------
// Escribir una línea de datos en el CSV
void writeDataLine(const String& linea) {
  if (!sdOK || !dataFileOpen || !dataFile) {
    logEvent("SD: ERROR - Intento de escritura sin fichero abierto");
    return;
  }
  dataFile.println(linea);
  dataFile.flush();
}

// -----------------------------------------------
// Cerrar ambos ficheros de forma segura (llamar antes de cortar alimentación)
void closeSD() {
  if (dataFile)  { dataFile.close();  }
  if (logFile)   { logFile.close();   }
  dataFileOpen = false;
  Serial.println("[SD] Ficheros cerrados correctamente");
}
```

#### Fichero `main.ino` actualizado

```cpp
// main.ino

bool gpsFijado = false;   // Controla si ya se creó el fichero de datos

void setup() {
  Serial.begin(115200);
  setupGPS();
  setupSD();
}

void loop() {
  leerGPS();

  // En cuanto haya un fix válido, crear el fichero CSV (solo la primera vez)
  if (datoGPS.valido && !gpsFijado) {
    logEvent("GPS: Fix válido obtenido");
    createDataFile();
    gpsFijado = true;
  }

  // Construir la cadena de datos y guardarla
  if (datoGPS.valido && dataFileOpen) {
    String linea = buildDataString();
    writeDataLine(linea);
    Serial.println(linea);
  }

  delay(1000);
}
```

#### Fichero `funciones.ino`

```cpp
// funciones.ino
// Funciones auxiliares: construcción de la cadena CSV, timestamp, etc.

// -----------------------------------------------
// Construir la cadena CSV con todos los datos disponibles
String buildDataString() {
  char buf[120];
  // Formato: TIMESTAMP,LAT,LON,ALT,VEL,HDOP,SATS
  snprintf(buf, sizeof(buf),
    "%02d%02d%02d:%02d%02d%02d,%.6f,%.6f,%.1f,%.2f,%.2f,%d",
    datoGPS.anyo % 100, datoGPS.mes,    datoGPS.dia,
    datoGPS.hora,       datoGPS.minuto, datoGPS.segundo,
    datoGPS.latitud, datoGPS.longitud,
    datoGPS.altitud, datoGPS.velocidad,
    datoGPS.hdop,    datoGPS.satellites
  );
  return String(buf);
}
```

---

### 6. Manejo de errores cubiertos

| Situación | Respuesta del sistema |
|-----------|-----------------------|
| SD no insertada al arrancar | `setupSD()` devuelve `false`, se registra en Serial, sistema sigue sin SD |
| Fichero no se puede crear | `createDataFile()` devuelve `false`, se registra en log |
| Nombre de fichero duplicado | Se detecta con `SD.exists()` y se elimina el anterior |
| Escritura sin fichero abierto | `writeDataLine()` detecta el estado y registra el error |
| Corte de alimentación | Usar `dataFile.flush()` en cada escritura para no perder datos |

---

### 7. Verificación en Excel / Google Earth

Una vez retirada la tarjeta SD y leída en el portátil:

- **Excel:** Abrir el `.csv` directamente. Si los decimales usan `.` (punto), cambiar la configuración regional o usar "Importar datos" con separador coma.
- **Google Earth:** Convertir el CSV a formato KML con un script Python:

```python
# csv_to_kml.py
# Genera un fichero KML para visualizar la ruta en Google Earth

import csv

INPUT_CSV = "250129093045.csv"
OUTPUT_KML = "ruta.kml"

with open(INPUT_CSV, newline='') as f:
    reader = csv.DictReader(f)
    coords = [(row['LON'], row['LAT'], row['ALT']) for row in reader]

kml_header = """<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2">
<Document><name>Ruta TFG</name>
<Placemark><name>Trayectoria</name>
<LineString><altitudeMode>absolute</altitudeMode>
<coordinates>"""

kml_footer = """</coordinates>
</LineString></Placemark>
</Document></kml>"""

with open(OUTPUT_KML, 'w') as f:
    f.write(kml_header)
    for lon, lat, alt in coords:
        f.write(f"{lon},{lat},{alt}\n")
    f.write(kml_footer)

print(f"Fichero KML generado: {OUTPUT_KML}")
```

---

### 8. Buenas prácticas de programación aplicadas

| Práctica | Implementación |
|----------|---------------|
| **Modularidad** | Código separado en `gps_setup.ino`, `sd_manager.ino`, `funciones.ino`, `main.ino` |
| **Declaración de variables** | Structs tipados (`DatosGPS`) en lugar de variables globales sueltas |
| **Comentarios** | Cada función documentada con su propósito y parámetros |
| **Gestión de errores** | Comprobación de retornos en todas las operaciones críticas |
| **Flush tras escritura** | `flush()` en cada `println()` para no perder datos ante corte de luz |
| **Nombres descriptivos** | `setupSD()`, `createDataFile()`, `writeDataLine()`, etc. |

---

## Entrega 2B — Optimización GPS y Ficheros SD Estructurados {#entrega-2b}

### Objetivo

Mejorar la configuración del GPS para operar a máxima velocidad, filtrar las tramas NMEA necesarias, y estructurar correctamente los ficheros en la tarjeta SD: un **fichero de datos** (CSV) y un **fichero de log** (debug).

---

### 1. Configuración Avanzada del GPS (NEO-6M)

#### 1.1 Maximizar la velocidad de refresco

El módulo NEO-6M por defecto opera a **9600 baudios y 1 Hz**. Para telemetría vehicular se debe aumentar hasta **5 Hz o 10 Hz** (según capacidad del módulo) y subir la velocidad de comunicación.

**Secuencia de comandos UBX para configurar a 5 Hz y 115200 baudios:**

```cpp
// En setup_gps.ino

// 1. Cambiar baudrate a 115200
// Mensaje UBX-CFG-PRT
uint8_t setBaud115200[] = {
  0xB5, 0x62, 0x06, 0x00, 0x14, 0x00,
  0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00,
  0x00, 0xC2, 0x01, 0x00, 0x07, 0x00, 0x03, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0xC0, 0x7E  // Checksum
};
Serial1.write(setBaud115200, sizeof(setBaud115200));
delay(100);
Serial1.begin(115200);

// 2. Configurar tasa de refresco a 5 Hz (200 ms)
// Mensaje UBX-CFG-RATE
uint8_t setRate5Hz[] = {
  0xB5, 0x62, 0x06, 0x08, 0x06, 0x00,
  0xC8, 0x00,  // measRate = 200 ms (5 Hz)
  0x01, 0x00,  // navRate = 1
  0x01, 0x00,  // timeRef = GPS
  0xDE, 0x6A   // Checksum
};
Serial1.write(setRate5Hz, sizeof(setRate5Hz));
delay(100);
```

#### 1.2 Filtrar tramas NMEA

Solo se necesitan **GGA** (posición, altitud, HDOP, satélites) y **RMC** (timestamp, velocidad, rumbo). Deshabilitar el resto reduce la carga en el buffer serie.

```cpp
// Deshabilitar todas las tramas NMEA excepto GGA y RMC
// Mensaje UBX-CFG-MSG

// Deshabilitar GLL
uint8_t disableGLL[] = {0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2A};
// Deshabilitar GSA
uint8_t disableGSA[] = {0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x31};
// Deshabilitar GSV
uint8_t disableGSV[] = {0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x38};
// Deshabilitar VTG
uint8_t disableVTG[] = {0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x46};

Serial1.write(disableGLL, sizeof(disableGLL)); delay(50);
Serial1.write(disableGSA, sizeof(disableGSA)); delay(50);
Serial1.write(disableGSV, sizeof(disableGSV)); delay(50);
Serial1.write(disableVTG, sizeof(disableVTG)); delay(50);
```

---

### 2. Estructura de Ficheros en la SD

#### 2.1 Fichero de datos (CSV de telemetría)

- **Nombre único:** `YYMMDDHHMMSS.csv` (generado a partir del primer fix GPS válido)
- **Formato:** Una línea por muestra, separada por comas
- **Cabecera:** Primera línea con nombres de campos

```
TIMESTAMP,LAT,LON,ALT,VEL,HDOP,SATS
20250129:001530,40.416775,-3.703790,650.5,0.00,1.2,9
20250129:001531,40.416776,-3.703791,650.5,0.00,1.2,9
```

#### 2.2 Fichero de log (debug)

- **Nombre fijo:** `LOG.TXT` (único por sesión, se añade al existente o se crea nuevo)
- **Contenido:** Eventos del sistema, estados de dispositivos, errores

```
[20250129:001500] SISTEMA INICIADO
[20250129:001501] GPS: Comunicación OK a 115200 baud
[20250129:001510] GPS: Buscando satélites...
[20250129:001528] GPS: Fix válido - 9 satélites - HDOP=1.2
[20250129:001528] SD: Fichero creado: 250129001528.csv
[20250129:001530] SD: Grabación de datos iniciada
```

#### 2.3 Código modular para la SD

```cpp
// sd_manager.ino

#include <SD.h>
#include <SPI.h>

#define PIN_SD_CS  53   // Chip Select en Mega 2560

File dataFile;
File logFile;
char dataFileName[20];
bool sdOK = false;

// -----------------------------------------------
bool setupSD() {
  if (!SD.begin(PIN_SD_CS)) {
    return false;
  }
  sdOK = true;
  // Abrir (o crear) fichero de log fijo
  logFile = SD.open("LOG.TXT", FILE_WRITE);
  logEvent("SISTEMA INICIADO");
  return true;
}

// -----------------------------------------------
void logEvent(const char* msg) {
  if (!sdOK || !logFile) return;
  logFile.print("[");
  logFile.print(getTimestamp());   // Función que devuelve String con timestamp
  logFile.print("] ");
  logFile.println(msg);
  logFile.flush();                 // Forzar escritura al instante
}

// -----------------------------------------------
bool createDataFile(const char* timestamp) {
  // Nombre único basado en timestamp del primer fix
  snprintf(dataFileName, sizeof(dataFileName), "%s.csv", timestamp);
  if (SD.exists(dataFileName)) {
    SD.remove(dataFileName);       // Evitar colisiones en debug
  }
  dataFile = SD.open(dataFileName, FILE_WRITE);
  if (!dataFile) return false;
  // Escribir cabecera
  dataFile.println("TIMESTAMP,LAT,LON,ALT,VEL,HDOP,SATS");
  dataFile.flush();
  return true;
}

// -----------------------------------------------
void writeDataLine(const String& linea) {
  if (!sdOK || !dataFile) return;
  dataFile.println(linea);
  dataFile.flush();
}
```

---

### 3. Cadena de datos acumulada (base para todas las entregas)

La "string de telemetría" crece con cada entrega. Al final de la entrega 2B tiene esta forma:

```
TIMESTAMP,LAT,LON,ALT,VEL,HDOP,SATS
```

> **Nota:** Esta cadena se irá ampliando con acelerómetro, temperatura IR, OBD2, etc.

---

## Entrega 3 — Acelerómetro 3 Ejes {#entrega-3}

### Hardware

| Parámetro | Valor |
|-----------|-------|
| Módulo recomendado | Y31N AHRS (o equivalente con inclinómetro integrado) |
| Ejes | 3 (ax, ay, az) + ángulos (yaw, pitch, roll) |
| Rango | ±16 g |
| Precisión | 0.01 g |
| Frecuencia máx. | 100 Hz |
| Velocidad serie | 115200 baudios |
| Interfaz | Puerto serie HW (usar Serial2 en Mega) |
| Alimentación | 3.3V / 5V |

> ⚠️ **Importante:** No se ve afectado por el campo magnético, lo que lo hace fiable en entornos con motores eléctricos o cableado de alta corriente.

---

### Conexionado

| Pin Acelerómetro | Pin Arduino Mega |
|-----------------|-----------------|
| TX | RX2 (Pin 17) |
| RX | TX2 (Pin 16) |
| VCC | 5V |
| GND | GND |

> Recordar siempre **cruzar TX↔RX**.

---

### Código

```cpp
// acelerometro.ino

#define SERIAL_ACEL Serial2
#define BAUD_ACEL   115200

struct DatosAcel {
  float ax, ay, az;      // Aceleraciones en g
  float yaw, pitch, roll; // Ángulos en grados
  bool valido;
};

DatosAcel datoAcel;

// -----------------------------------------------
void setupAcelerometro() {
  SERIAL_ACEL.begin(BAUD_ACEL);
  logEvent("ACEL: Inicializando...");
  delay(500);
  logEvent("ACEL: OK");
}

// -----------------------------------------------
// Parsear trama de texto: "ax,ay,az,yaw,pitch,roll\r\n"
bool leerAcelerometro() {
  if (SERIAL_ACEL.available() > 0) {
    String linea = SERIAL_ACEL.readStringUntil('\n');
    linea.trim();
    if (linea.length() == 0) return false;

    // Parsear los 6 campos separados por coma
    int idx = 0;
    float valores[6];
    char buf[80];
    linea.toCharArray(buf, sizeof(buf));
    char* token = strtok(buf, ",");
    while (token != NULL && idx < 6) {
      valores[idx++] = atof(token);
      token = strtok(NULL, ",");
    }
    if (idx == 6) {
      datoAcel.ax    = valores[0];
      datoAcel.ay    = valores[1];
      datoAcel.az    = valores[2];
      datoAcel.yaw   = valores[3];
      datoAcel.pitch = valores[4];
      datoAcel.roll  = valores[5];
      datoAcel.valido = true;
      return true;
    }
  }
  datoAcel.valido = false;
  return false;
}
```

---

### Cadena de datos actualizada (Entrega 3)

```
TIMESTAMP,LAT,LON,ALT,VEL,HDOP,SATS,AX,AY,AZ,YAW,PITCH,ROLL
20250129:001530,40.4168,-3.7038,650.5,0.00,1.2,9,0.02,-0.01,1.00,0.0,1.5,-0.3
```

---

### Consejos de calibración

- Colocar el sensor en posición de reposo conocida y ajustar el **offset de posición cero** mediante la aplicación del fabricante.
- Verificar que el eje Z apunte hacia arriba: en reposo debe leer ≈ +1.0 g (gravedad terrestre).
- Para detección de vibraciones, configurar una frecuencia de muestreo alta (≥50 Hz) y analizar la FFT de la señal.

---

## Entrega 4 — Sensor de Temperatura Infrarroja MLX90614 {#entrega-4}

### Hardware

| Parámetro | Valor |
|-----------|-------|
| Sensor | MLX90614 |
| Interfaz | I2C (SCL / SDA) |
| Dirección I2C por defecto | 0x5A |
| Rango de medida | -70°C a +380°C |
| Precisión | ±0.5°C |
| Variantes | Abierto (campo amplio) / Tubo corto / Tubo largo (focalizado) |
| Emisividad (neumáticos) | 0.95 |

---

### Conexionado con Arduino Mega

| Pin MLX90614 | Pin Arduino Mega |
|-------------|-----------------|
| SDA | SDA (Pin 20) |
| SCL | SCL (Pin 21) |
| VCC | 3.3V |
| GND | GND |

> ⚠️ Usar resistencias pull-up de **4.7 kΩ** en SDA y SCL si no las lleva la placa.

---

### Librería

```
Adafruit_MLX90614 (disponible en el Gestor de Librerías de Arduino IDE)
```

---

### Código

```cpp
// infrarrojo.ino

#include <Wire.h>
#include <Adafruit_MLX90614.h>

Adafruit_MLX90614 mlx = Adafruit_MLX90614();

struct DatosIR {
  float tempAmbiente;   // Temperatura ambiente (°C)
  float tempObjeto;     // Temperatura del objeto medido (°C)
  bool valido;
};

DatosIR datoIR;

// -----------------------------------------------
bool setupIR() {
  if (!mlx.begin()) {
    logEvent("IR: ERROR - Sensor no encontrado");
    datoIR.valido = false;
    return false;
  }
  // Configurar emisividad para neumáticos
  mlx.writeEmissivity(0.95);
  logEvent("IR: OK - Emisividad=0.95");
  datoIR.valido = true;
  return true;
}

// -----------------------------------------------
void leerIR() {
  datoIR.tempAmbiente = mlx.readAmbientTempC();
  datoIR.tempObjeto   = mlx.readObjectTempC();
  datoIR.valido = true;
}
```

---

### Tabla de Emisividades de Referencia

| Material | Emisividad |
|----------|-----------|
| Neumático de goma | 0.95 |
| Aluminio pulido | 0.05 |
| Aluminio anodizado | 0.77 |
| Acero pintado | 0.90 |
| Acero inoxidable | 0.16 |
| Freno de disco (hierro fundido) | 0.70 |

---

### Cadena de datos actualizada (Entrega 4)

```
TIMESTAMP,LAT,LON,ALT,VEL,HDOP,SATS,AX,AY,AZ,YAW,PITCH,ROLL,TEMP_AMB,TEMP_NEUM
20250129:001530,40.4168,-3.7038,650.5,60.3,1.2,9,0.02,-0.01,1.00,0.0,1.5,-0.3,22.5,45.8
```

---

## Entrega 5 — Pantalla TFT Táctil ILI9488 {#entrega-5}

### Hardware

| Parámetro | Valor |
|-----------|-------|
| Display | TFT ILI9488 táctil |
| Resolución | 320 × 480 píxeles |
| Interfaz | SPI (o paralelo 16 bits) |
| Alimentación | 3.3V / 5V |
| Escudo recomendado | Mega Shield (versión WELDED) |

---

### Principios de diseño de la pantalla

> ⚡ **Regla fundamental:** Dibujar la estructura estática **una sola vez en el `setup()`**. Solo actualizar los valores que cambian. **Nunca repintar toda la pantalla** en cada ciclo de `loop()`.

#### Elementos recomendados para la pantalla

| Zona | Contenido | Tipo de actualización |
|------|-----------|----------------------|
| Superior | Logo UC3M + Título | Estático (setup) |
| Izquierda | Lat / Lon / Alt / Vel | Numérico dinámico |
| Centro | Posición (tipo radar) | Gráfico dinámico |
| Cobertura GPS | Barras tipo WiFi | Gráfico dinámico |
| Aceleración | Punto en osciloscopio | Gráfico dinámico |
| Temperatura IR | Valor numérico | Numérico dinámico |
| Inferior | Contador SD / Estado GPRS | Mixto |
| Esquina | **Testigo de vida** | Animación continua |

---

### Técnica de actualización eficiente

```cpp
// pantalla.ino

#include <TFT_eSPI.h>  // Librería compatible con ILI9488

TFT_eSPI tft = TFT_eSPI();

// Colores definidos como constantes
#define COLOR_FONDO    TFT_BLACK
#define COLOR_ESTATICO TFT_DARKGREY
#define COLOR_ACTIVO   TFT_GREEN
#define COLOR_ALERTA   TFT_RED
#define COLOR_TITULO   TFT_CYAN

// Variables para detectar cambios
float lat_ant = 0, lon_ant = 0, vel_ant = 0;
int sats_ant = 0;

// -----------------------------------------------
void setupPantalla() {
  tft.init();
  tft.setRotation(0);  // Portrait
  tft.fillScreen(COLOR_FONDO);

  // --- Dibujar estructura ESTÁTICA (solo una vez) ---
  tft.setTextColor(COLOR_TITULO);
  tft.setTextSize(2);
  tft.setCursor(60, 10);
  tft.println("TELEMETRIA UC3M");

  // Etiquetas fijas en gris
  tft.setTextColor(COLOR_ESTATICO);
  tft.setTextSize(1);
  tft.setCursor(5, 50);  tft.print("LAT:");
  tft.setCursor(5, 65);  tft.print("LON:");
  tft.setCursor(5, 80);  tft.print("ALT:");
  tft.setCursor(5, 95);  tft.print("VEL:");
  tft.setCursor(5, 110); tft.print("HDOP:");
  tft.setCursor(5, 125); tft.print("SATS:");

  // Círculo estilo "radar" para posición relativa
  tft.drawCircle(240, 200, 60, TFT_DARKGREY);
  tft.drawCircle(240, 200, 30, TFT_DARKGREY);
  tft.drawLine(180, 200, 300, 200, TFT_DARKGREY);
  tft.drawLine(240, 140, 240, 260, TFT_DARKGREY);
}

// -----------------------------------------------
// Actualizar solo el campo que ha cambiado
void actualizarCampo(int x, int y, float valor_nuevo, float &valor_ant,
                     const char* unidad, int decimales) {
  if (abs(valor_nuevo - valor_ant) < 0.001) return; // Sin cambio

  // Borrar valor anterior (sobrescribir con fondo)
  tft.fillRect(x, y, 100, 12, COLOR_FONDO);

  // Escribir nuevo valor
  tft.setTextColor(COLOR_ACTIVO);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(valor_nuevo, decimales);
  tft.print(" ");
  tft.print(unidad);

  valor_ant = valor_nuevo;
}

// -----------------------------------------------
// Testigo de vida: pequeño punto parpadeante
void actualizarTestigoVida() {
  static bool estado = false;
  static unsigned long tAnt = 0;
  if (millis() - tAnt > 500) {
    tft.fillCircle(310, 10, 5, estado ? TFT_GREEN : COLOR_FONDO);
    estado = !estado;
    tAnt = millis();
  }
}

// -----------------------------------------------
// Indicador de satélites tipo WiFi
void dibujarCoberturaGPS(int numSats) {
  int x = 5, y = 420;
  tft.fillRect(x, y - 30, 60, 35, COLOR_FONDO);
  for (int i = 0; i < 5; i++) {
    uint16_t color = (numSats >= (i + 1) * 2) ? TFT_GREEN : TFT_DARKGREY;
    tft.fillRect(x + i * 12, y - (i * 6), 8, 6 + i * 6, color);
  }
}
```

---

### Script Python para visualización en tierra (previsualización de datos)

```python
# visualizacion_serie.py
# Permite ver los datos de la pantalla TFT en el portátil durante desarrollo

import serial
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

PUERTO = 'COM3'     # Cambiar por el puerto correspondiente
BAUD   = 115200
MAX_MUESTRAS = 100

ser = serial.Serial(PUERTO, BAUD, timeout=1)

tiempos = deque(maxlen=MAX_MUESTRAS)
acels   = deque(maxlen=MAX_MUESTRAS)
vels    = deque(maxlen=MAX_MUESTRAS)

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))

def actualizar(frame):
    linea = ser.readline().decode('utf-8', errors='ignore').strip()
    campos = linea.split(',')
    if len(campos) >= 13:
        try:
            t   = campos[0]
            vel = float(campos[4])
            ax  = float(campos[7])
            tiempos.append(t[-6:])  # Solo HHMMSS
            vels.append(vel)
            acels.append(ax)
            ax1.clear()
            ax1.plot(list(vels), color='cyan')
            ax1.set_title('Velocidad (km/h)')
            ax2.clear()
            ax2.plot(list(acels), color='orange')
            ax2.set_title('Aceleración X (g)')
        except ValueError:
            pass

ani = animation.FuncAnimation(fig, actualizar, interval=200)
plt.tight_layout()
plt.show()
```

---

## Entrega 6 — OBD2 con ELM327 {#entrega-6}

### Hardware requerido

> ⚠️ **CRÍTICO:** Comprar el **ELM327 versión 1.5 de doble piso** (PIC18F25K80 + placa Bluetooth azul en segundo piso desmontable). **NO comprar la versión 2.x**.

El proceso de adaptación es:
1. Enchufarlo al vehículo y probar con la app **Torque** (Bluetooth) para verificar funcionamiento.
2. Desmontar la tapa superior y **desoldar el módulo Bluetooth**.
3. Sacar **4 cables trenzados** (VCC, GND, TX, RX) a través de un agujero en la carcasa.
4. Conectar esos cables directamente a un puerto serial HW del Arduino Mega.

---

### Conexionado ELM327 → Arduino Mega

| Pin ELM327 | Pin Arduino Mega |
|-----------|-----------------|
| TX | RX3 (Pin 15) |
| RX | TX3 (Pin 14) |
| VCC | 5V (o desde el conector OBD del vehículo) |
| GND | GND |

---

### Protocolo y librería

- **Librería:** [ELMduino](https://github.com/PowerBroker2/ELMduino)
- **Protocolo de comunicación:** Comandos AT (Hayes)
- **Velocidad:** 115200 baudios (tras configuración inicial)

---

### PIDs útiles (OBD-II estándar)

| PID | Descripción | Unidad |
|-----|-------------|--------|
| 0x0C | RPM del motor | rpm |
| 0x0D | Velocidad del vehículo | km/h |
| 0x05 | Temperatura refrigerante | °C |
| 0x0F | Temperatura del aire admisión | °C |
| 0x11 | Posición del acelerador | % |
| 0x04 | Carga calculada del motor | % |
| 0x5C | Temperatura aceite del motor | °C |
| 0x1F | Tiempo de marcha del motor | s |

---

### Código

```cpp
// obd2.ino

#include <ELMduino.h>

#define SERIAL_OBD Serial3

ELM327 myELM327;

struct DatosOBD {
  float rpm;
  float velocidad;
  float tempRefrigerante;
  float posicionAcel;
  float cargaMotor;
  bool valido;
};

DatosOBD datoOBD;

// -----------------------------------------------
bool setupOBD2() {
  SERIAL_OBD.begin(115200);
  delay(1000);
  if (!myELM327.begin(SERIAL_OBD, true, 2000)) {
    logEvent("OBD2: ERROR - No se pudo conectar al ELM327");
    return false;
  }
  logEvent("OBD2: OK - Conectado al ELM327");
  return true;
}

// -----------------------------------------------
void leerOBD2() {
  // Leer RPM
  float rpm = myELM327.rpm();
  if (myELM327.nb_rx_state == ELM_SUCCESS) {
    datoOBD.rpm = rpm;
  }

  // Leer velocidad
  float vel = myELM327.kph();
  if (myELM327.nb_rx_state == ELM_SUCCESS) {
    datoOBD.velocidad = vel;
  }

  // Leer temperatura refrigerante
  float temp = myELM327.engineCoolantTemp();
  if (myELM327.nb_rx_state == ELM_SUCCESS) {
    datoOBD.tempRefrigerante = temp;
  }

  datoOBD.valido = true;
}
```

---

### Secuencia de comandos AT para pruebas manuales

Conectar el ELM327 al PC mediante el convertidor USB-TTL a 38400 baudios y probar:

```
ATZ          ; Reset del ELM327
ATE0         ; Desactivar eco
ATH1         ; Mostrar cabeceras
ATSP0        ; Detección automática de protocolo
0100         ; PIDs soportados (01-20)
010C         ; Leer RPM
010D         ; Leer velocidad
0105         ; Leer temp. refrigerante
```

---

### Cadena de datos actualizada (Entrega 6)

```
TIMESTAMP,LAT,LON,ALT,VEL_GPS,HDOP,SATS,AX,AY,AZ,YAW,PITCH,ROLL,TEMP_AMB,TEMP_NEUM,RPM,VEL_OBD,TEMP_REFRIG,ACEL_POS,CARGA_MOTOR
20250129:001530,40.4168,-3.7038,650.5,60.3,1.2,9,0.02,-0.01,1.00,0.0,1.5,-0.3,22.5,45.8,2500,60,85,35,45
```

---

## Entrega 7 — Comunicaciones GPRS/MQTT con SIM800L {#entrega-7}

### Hardware

| Parámetro | Valor |
|-----------|-------|
| Módulo | SIM800L (versión con antena roscada externa) |
| Interfaz | Puerto serie HW (Serial2 si está libre, o Serial3) |
| Velocidad | 115200 baudios |
| Alimentación | 3.7V – 4.2V (¡NO 5V directos!) |
| Consumo pico | **2 A** al conectar a la red GSM |

> ⚠️ **CRÍTICO — Pico de consumo de 2 A:**  
> El módulo SIM800L puede resetarse si la fuente no puede suministrar 2 A instantáneos.  
> **Solución:** Colocar un condensador electrolítico de **1000–2200 µF / 6.3V** directamente en los pines de alimentación del módulo. Usar cables de sección ≥ 0.5 mm².

---

### Conexionado

| Pin SIM800L | Pin Arduino Mega |
|------------|-----------------|
| TX | RX2 (Pin 17) |
| RX | TX2 (Pin 16) |
| VCC | Fuente externa 4V / 2A con condensador |
| GND | GND común |
| RST | Pin 4 (opcional, para reset por software) |

---

### Secuencia de conexión (Comandos AT)

```
ATZ;E0;+CMEE=1;&F;+GSV;+CSQ;+GSN;+CCID    ; Sin PIN
ATZ;E0;+CMEE=1;&F;+GSV;+CSQ;+GSN;+CPIN=1234;+CCID  ; Con PIN

; Esperar confirmación de red (puede tardar 20-30 s)
AT+CREG?     ; 0,1 = conectado a red local
AT+CGREG?    ; 0,1 = conectado a GPRS
AT+COPS?     ; Operador conectado
AT+CSTT="internet"   ; APN (cambiar según operador: "internet", "MOVISTAR", etc.)
AT+CIICR             ; Activar conexión de datos GPRS
AT+CIFSR             ; Obtener IP asignada
```

---

### Protocolo MQTT

**MQTT (Message Queuing Telemetry Transport)** es el estándar del sector IoT para telemetría. Arquitectura publicación/suscripción mediante un **broker** central.

```
Arduino (publicador) → Broker MQTT (test.mosquitto.org) → PC en tierra (suscriptor)
```

#### Conceptos clave

| Concepto | Descripción |
|----------|-------------|
| **Broker** | Servidor que gestiona los mensajes (ej: `test.mosquitto.org`) |
| **Topic** | Canal temático (ej: `uc3m/tfg/telemetria`) |
| **Publish** | El Arduino publica datos en un topic |
| **Subscribe** | La app en tierra se suscribe al topic y recibe los datos |
| **QoS** | Calidad de servicio (0=fire&forget, 1=al menos una vez, 2=exactamente una vez) |

#### Broker público para pruebas

```
Broker: test.mosquitto.org
Puerto TCP: 1883
Cliente web: https://testclient-cloud.mqtt.cool/
```

---

### Código Arduino — Cliente MQTT sobre GPRS

```cpp
// gprs_mqtt.ino
// Basado en librería PubSubClient adaptada para SIM800L

#define SERIAL_GPRS Serial2
#define PIN_RST_GPRS 4

// APN según operador
const char APN[]      = "internet";
const char MQTT_HOST[] = "test.mosquitto.org";
const int  MQTT_PORT   = 1883;
const char MQTT_TOPIC[] = "uc3m/tfg/telemetria";
const char CLIENT_ID[]  = "ArduinoTFG_UC3M";

// -----------------------------------------------
bool enviarComandoAT(const String& cmd, const String& respEsperada, int timeout = 2000) {
  SERIAL_GPRS.println(cmd);
  unsigned long tInicio = millis();
  String respuesta = "";
  while (millis() - tInicio < (unsigned long)timeout) {
    while (SERIAL_GPRS.available()) {
      respuesta += (char)SERIAL_GPRS.read();
    }
    if (respuesta.indexOf(respEsperada) != -1) return true;
  }
  return false;
}

// -----------------------------------------------
bool setupGPRS() {
  SERIAL_GPRS.begin(115200);
  logEvent("GPRS: Iniciando...");

  if (!enviarComandoAT("ATZ", "OK", 3000))       { logEvent("GPRS: ERROR Reset"); return false; }
  enviarComandoAT("ATE0", "OK");
  enviarComandoAT("AT+CMEE=1", "OK");
  delay(2000);  // Esperar registro en red

  if (!enviarComandoAT("AT+CREG?", "0,1", 5000)) { logEvent("GPRS: ERROR Red no disponible"); return false; }
  
  String cmdAPP = "AT+CSTT=\"" + String(APN) + "\"";
  enviarComandoAT(cmdAPP, "OK");
  enviarComandoAT("AT+CIICR", "OK", 5000);
  enviarComandoAT("AT+CIFSR", ".", 3000);  // Obtener IP

  logEvent("GPRS: Conectado a GPRS OK");
  return true;
}

// -----------------------------------------------
// Publicar un string en el broker MQTT mediante socket TCP
bool publicarMQTT(const String& payload) {
  // Abrir socket TCP al broker
  String cmd = "AT+CIPSTART=\"TCP\",\"" + String(MQTT_HOST) + "\"," + String(MQTT_PORT);
  if (!enviarComandoAT(cmd, "CONNECT OK", 10000)) return false;

  // Construir paquete MQTT CONNECT
  // (Implementación simplificada — usar librería PubSubClient en producción)
  // ... (ver implementación completa en librerías)

  return true;
}
```

---

### Script Python — Suscriptor MQTT y Visualización en Tierra

```python
# cliente_mqtt_tierra.py
# Requiere: pip install paho-mqtt matplotlib

import paho.mqtt.client as mqtt
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
import json

BROKER = "test.mosquitto.org"
PORT   = 1883
TOPIC  = "uc3m/tfg/telemetria"

# Buffers de datos para gráficos
MAX_MUESTRAS = 200
velocidades  = deque(maxlen=MAX_MUESTRAS)
aceleraciones= deque(maxlen=MAX_MUESTRAS)
hdops        = deque(maxlen=MAX_MUESTRAS)
timestamps   = deque(maxlen=MAX_MUESTRAS)
latitudes    = []
longitudes   = []

# -----------------------------------------------
def on_connect(client, userdata, flags, rc):
    print(f"Conectado al broker MQTT. Código: {rc}")
    client.subscribe(TOPIC)

def on_message(client, userdata, msg):
    linea = msg.payload.decode('utf-8').strip()
    campos = linea.split(',')
    try:
        if len(campos) >= 8:
            timestamps.append(campos[0])
            latitudes.append(float(campos[1]))
            longitudes.append(float(campos[2]))
            velocidades.append(float(campos[4]))
            hdops.append(float(campos[5]))
            aceleraciones.append(float(campos[7]))
            print(f"[{campos[0]}] Vel={campos[4]} km/h | Sats={campos[6]} | AX={campos[7]}")
    except (ValueError, IndexError):
        pass

# -----------------------------------------------
# Configurar cliente MQTT
client = mqtt.Client(CLIENT_ID="SuscriptorTierra")
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, PORT, 60)
client.loop_start()

# -----------------------------------------------
# Visualización en tiempo real
fig, axes = plt.subplots(2, 2, figsize=(14, 8))
fig.suptitle('Telemetría Vehicular — TFG UC3M', fontsize=14, fontweight='bold')

def actualizar(frame):
    if not velocidades: return

    axes[0,0].clear()
    axes[0,0].plot(list(velocidades), color='cyan', linewidth=1.5)
    axes[0,0].set_title('Velocidad GPS (km/h)')
    axes[0,0].set_ylabel('km/h'); axes[0,0].grid(True, alpha=0.3)

    axes[0,1].clear()
    axes[0,1].plot(list(aceleraciones), color='orange', linewidth=1.5)
    axes[0,1].set_title('Aceleración X (g)')
    axes[0,1].axhline(0, color='white', linewidth=0.5)
    axes[0,1].grid(True, alpha=0.3)

    axes[1,0].clear()
    if latitudes and longitudes:
        axes[1,0].plot(longitudes, latitudes, 'g.-', markersize=3)
        axes[1,0].set_title('Trayectoria GPS')
        axes[1,0].set_xlabel('Longitud'); axes[1,0].set_ylabel('Latitud')
        axes[1,0].grid(True, alpha=0.3)

    axes[1,1].clear()
    axes[1,1].plot(list(hdops), color='red', linewidth=1.5)
    axes[1,1].set_title('HDOP (precisión GPS)')
    axes[1,1].axhline(1.0, color='green', linestyle='--', label='Excelente (<1)')
    axes[1,1].axhline(2.0, color='yellow', linestyle='--', label='Bueno (<2)')
    axes[1,1].legend(fontsize=7); axes[1,1].grid(True, alpha=0.3)

    plt.tight_layout()

ani = animation.FuncAnimation(fig, actualizar, interval=500)
plt.style.use('dark_background')
plt.show()
```

---

## Entrega 8 — Integración Final, Encapsulado y TFG Escrito {#entrega-8}

### Objetivo

Integrar todo el hardware en una **envolvente mecánica profesional**, alimentar el sistema de forma autónoma y redactar el TFG completo.

---

### 1. Encapsulado Mecánico

#### Selección de la envolvente

- Buscar una caja de **plástico ABS o aluminio** con dimensiones suficientes para el Arduino Mega + todos los módulos.
- Definir el **grado de protección IP** según el entorno de uso:

| Código IP | Protección contra sólidos | Protección contra líquidos |
|-----------|--------------------------|---------------------------|
| IP54 | Polvo limitado | Salpicaduras desde cualquier dirección |
| IP65 | Polvo total | Chorros de agua |
| IP67 | Polvo total | Inmersión hasta 1 m / 30 min |

> Para telemetría vehicular exterior se recomienda **mínimo IP54**, idealmente **IP65**.

#### Perforaciones y conectores

| Elemento | Tipo de conector recomendado |
|----------|------------------------------|
| GPS (antena externa) | Conector SMA hembra (panel) |
| GPRS (antena externa) | Conector SMA hembra (panel) |
| Sensor IR | Conector circular M12 4 pines |
| OBD2 (cable) | Conector M12 4 pines o paso de cable con prensaestopas |
| Alimentación | Conector circular DC barrel jack o XT30 |
| USB de programación | USB-B hembra (panel) o paso con prensaestopas |
| Pantalla TFT | Apertura rectangular con sellado de caucho |

---

### 2. Alimentación

#### Opción A — Vehículo (12V)

```
Batería 12V del vehículo → Convertidor DC-DC 12V→5V 5A → Arduino Mega + módulos
```

- Usar un regulador tipo **LM2596** o módulo step-down con protección contra inversión de polaridad.
- Potencia mínima recomendada: **5V × 3A = 15W** (pico SIM800L incluido).

#### Opción B — Batería autónoma

```
LiPo 7.4V 2200mAh → Convertidor 5V 3A → Sistema
```

- Añadir un **módulo de carga TP4056** si se usa LiPo.
- Estimar autonomía: ~3-4 horas de operación continua.

---

### 3. Montaje PCB y Cableado

- Soldar todos los módulos sobre una **placa de prototipos PCB** (no en protoboard).
- Usar **cables con terminales crimpeados** o soldados, no cables Dupont volantes.
- **Embridar** todos los cables con bridas de nylon.
- Etiquetar cada conector con cinta termorretráctil marcada.
- Aplicar barniz protector sobre las soldaduras expuestas a vibraciones.

---

### 4. Checklist final del sistema

| Elemento | Estado |
|----------|--------|
| GPS NEO-6M operativo a 5 Hz | ☐ |
| Tarjeta SD grabando CSV con nombre único | ☐ |
| Fichero LOG.TXT activo | ☐ |
| Acelerómetro calibrado y funcionando | ☐ |
| Sensor IR con emisividad configurada | ☐ |
| Pantalla TFT mostrando todos los datos | ☐ |
| OBD2 ELM327 leyendo PIDs correctamente | ☐ |
| SIM800L conectado a red con condensador 1000µF | ☐ |
| MQTT publicando datos al broker | ☐ |
| Script Python recibiendo y visualizando en tierra | ☐ |
| Encapsulado IP54+ con conectores robustos | ☐ |
| Alimentación 5V/3A estable | ☐ |

---

### 5. Estructura del TFG escrito

```
1. Introducción y motivación
2. Estado del arte (sistemas de telemetría vehicular)
3. Descripción del sistema diseñado
   3.1 Arquitectura hardware
   3.2 Arquitectura software
4. Descripción de cada subsistema
   4.1 GPS y protocolo NMEA
   4.2 Almacenamiento en SD
   4.3 Acelerómetro
   4.4 Sensor infrarrojo
   4.5 Pantalla TFT
   4.6 OBD2 / ELM327
   4.7 Comunicaciones GPRS / MQTT
5. Integración y pruebas
   5.1 Pruebas unitarias por módulo
   5.2 Prueba de integración completa
   5.3 Validación en vehículo real
6. Resultados
   6.1 Datos capturados (gráficas, tablas)
   6.2 Trayectorias en Google Earth / Maps
   6.3 Telemetría en tiempo real
7. Presupuesto (tabla detallada con precios reales)
8. Conclusiones y trabajo futuro
9. Bibliografía
Anexos: Esquemas de conexionado, código fuente comentado, datasheet de módulos
```

> 💡 **Consejo para la defensa:** Incluir **muchas fotos** del proceso de montaje, del sistema funcionando, de la pantalla TFT, y de los datos capturados en gráficas. El día de la defensa eso tiene mucho peso.

---

### 6. Presupuesto estimado del proyecto

| Componente | Unidades | Precio est. (€) | Total (€) |
|-----------|----------|-----------------|-----------|
| Arduino Mega 2560 R3 | 1 | 12.00 | 12.00 |
| Módulo GPS NEO-6M + antena | 1 | 8.00 | 8.00 |
| Módulo MicroSD | 1 | 1.50 | 1.50 |
| Tarjeta MicroSD 16GB | 1 | 5.00 | 5.00 |
| Acelerómetro Y31N AHRS | 1 | 15.00 | 15.00 |
| Sensor IR MLX90614 | 1 | 8.00 | 8.00 |
| Pantalla TFT ILI9488 320x480 | 1 | 10.00 | 10.00 |
| Shield Mega para TFT (WELDED) | 1 | 4.00 | 4.00 |
| ELM327 v1.5 doble piso | 1 | 3.00 | 3.00 |
| Módulo SIM800L con antena | 1 | 5.00 | 5.00 |
| Tarjeta SIM de datos | 1 | 1.00 | 1.00 |
| Convertidor USB-TTL CH340 | 1 | 2.00 | 2.00 |
| Convertidor DC-DC 12V→5V 3A | 1 | 3.00 | 3.00 |
| Condensador 1000µF 6.3V | 2 | 0.30 | 0.60 |
| Caja IP65 ABS | 1 | 8.00 | 8.00 |
| Conectores M12 + SMA (panel) | varios | 10.00 | 10.00 |
| PCB prototipo + tornillería | 1 | 3.00 | 3.00 |
| Cables y componentes varios | — | 5.00 | 5.00 |
| **TOTAL** | | | **~103 €** |

---

*Documento generado como guía técnica para el TFG de Dinámica Vehicular — UC3M*  
*Tutor: Carlos Rodríguez Sánchez | Basado en entregas 2B–8*