// =============================================================================
// sd_manager.cpp  —  Implementación del módulo de gestión de la SD
// =============================================================================

#include "sd_manager.h"
#include "gps.h"
#include "accelerometer.h"
#include "temperature_sensor.h"
#include "gprs.h"
#include "obd2.h"

// -----------------------------------------------------------------------------
// Definición de variables globales (declaradas como extern en sd_manager.h)
// -----------------------------------------------------------------------------
SdFat32 sd;
bool  sdOK         = false;
bool  dataFileOpen = false;
char  dataFileName[30];
File32 dataFile;
File32 logFile;

// Buffer único donde se compone la trama. Es estático (no local) para no meter
// 400 bytes en la pila del loop() y no fragmentar la SRAM con String.
static char bufferTrama[TRAMA_MAX];

// =============================================================================
// UTILIDADES DE SERIALIZACION CSV
// =============================================================================

size_t csvEscritos(int resultadoSnprintf, size_t espacio) {
    if (resultadoSnprintf < 0 || espacio == 0) return 0;
    // snprintf devuelve lo que HABRIA escrito: si no cabe, se trunco a espacio-1
    if ((size_t)resultadoSnprintf >= espacio) return espacio - 1;
    return (size_t)resultadoSnprintf;
}

size_t csvCopiarCabecera(char* dst, size_t espacio, const char* cabeceraPGM) {
    if (espacio == 0) return 0;
    strncpy_P(dst, cabeceraPGM, espacio);
    dst[espacio - 1] = '\0';
    return strlen(dst);
}

size_t csvCamposVacios(char* dst, size_t espacio, const char* cabeceraPGM) {
    if (espacio == 0) return 0;

    // Se cuentan las comas de la cabecera (en PROGMEM): n comas = n+1 columnas,
    // y una fila vacia de n+1 columnas son exactamente n comas.
    size_t pos = 0;
    for (const char* p = cabeceraPGM; ; p++) {
        char c = pgm_read_byte(p);
        if (c == '\0') break;
        if (c != ',') continue;
        if (pos >= espacio - 1) break;   // sin sitio: se trunca
        dst[pos++] = ',';
    }
    dst[pos] = '\0';
    return pos;
}

// -----------------------------------------------------------------------------
// Anexa un separador de modulos al buffer, si cabe
// -----------------------------------------------------------------------------
static size_t anexarSeparador(char* dst, size_t espacio) {
    if (espacio < 2) return 0;
    dst[0] = ',';
    dst[1] = '\0';
    return 1;
}

// =============================================================================
// COMPOSICION DE LA TRAMA COMPLETA
//
// El orden de los modulos es el mismo en cabecera y datos. Anadir un sensor
// nuevo solo obliga a tocar estas dos funciones, y siempre en pareja.
// =============================================================================

const char* construirCabeceraCSV() {
    size_t pos = 0;
    pos += escribirCabeceraGPS  (bufferTrama + pos, TRAMA_MAX - pos);
    pos += anexarSeparador      (bufferTrama + pos, TRAMA_MAX - pos);
    pos += escribirCabeceraAccel(bufferTrama + pos, TRAMA_MAX - pos);
    pos += anexarSeparador      (bufferTrama + pos, TRAMA_MAX - pos);
    pos += escribirCabeceraTemp (bufferTrama + pos, TRAMA_MAX - pos);
    pos += anexarSeparador      (bufferTrama + pos, TRAMA_MAX - pos);
    pos += escribirCabeceraGPRS (bufferTrama + pos, TRAMA_MAX - pos);
    pos += anexarSeparador      (bufferTrama + pos, TRAMA_MAX - pos);
    pos += escribirCabeceraObd2 (bufferTrama + pos, TRAMA_MAX - pos);

    // Si la trama se ha truncado, el CSV quedaria descuadrado respecto a los
    // datos: mejor enterarse por el log que depurarlo despues sobre la marcha.
    if (pos >= TRAMA_MAX - 1) {
        logEvento(F("[SD] ERROR - Cabecera truncada, ampliar TRAMA_MAX"));
    }
    return bufferTrama;
}

const char* construirLineaCSV() {
    size_t pos = 0;
    pos += escribirDatosGPS  (bufferTrama + pos, TRAMA_MAX - pos);
    pos += anexarSeparador   (bufferTrama + pos, TRAMA_MAX - pos);
    pos += escribirDatosAccel(bufferTrama + pos, TRAMA_MAX - pos);
    pos += anexarSeparador   (bufferTrama + pos, TRAMA_MAX - pos);
    pos += escribirDatosTemp (bufferTrama + pos, TRAMA_MAX - pos);
    pos += anexarSeparador   (bufferTrama + pos, TRAMA_MAX - pos);
    pos += escribirDatosGPRS (bufferTrama + pos, TRAMA_MAX - pos);
    pos += anexarSeparador   (bufferTrama + pos, TRAMA_MAX - pos);
    pos += escribirDatosObd2 (bufferTrama + pos, TRAMA_MAX - pos);

    if (pos >= TRAMA_MAX - 1) {
        logEvento(F("[SD] ERROR - Linea de datos truncada, ampliar TRAMA_MAX"));
    }
    return bufferTrama;
}


// -----------------------------------------------------------------------------
// Inicializa la tarjeta SD y abre LOG.TXT
// -----------------------------------------------------------------------------
void setupSDCard() {
    // Serial.print(F("[SD] Inicializando... "));

    if (!sd.begin(PIN_SD_CS, SD_SCK_MHZ(SD_MHZ))) {
        // Serial.println(F("[SD] ERROR - tarjeta no detectada o no formateada en FAT32"));
        sd.initErrorPrint(&Serial);  // detalle: codigo de error exacto (cableado/CS/tarjeta/velocidad)
        sdOK = false;
        return;
    }

    sdOK = true;
    // Serial.println(F("[SD] Inicializacion correcta"));

    logFile = sd.open("LOG.TXT", FILE_WRITE);
    if (!logFile) {
        // Serial.println(F("[SD] ADVERTENCIA - No se pudo abrir LOG.TXT"));
    }

    logEvento(F("=== SISTEMA INICIADO ==="));
    logEvento(F("[SD] Inicializacion correcta"));
}

// -----------------------------------------------------------------------------
// Escribe un evento en LOG.TXT con timestamp si está disponible.
//
// Plantilla comun a las dos sobrecargas publicas: acepta tanto const char*
// (mensajes construidos en tiempo de ejecucion, p.ej. con snprintf) como
// const __FlashStringHelper* (literales envueltos en F(), que Print sabe
// imprimir leyendo directamente de flash). Los mensajes fijos SIEMPRE deben
// pasarse como logEvento(F("...")): sin el F(), el literal se copia entero a
// SRAM (ver .data en el mapa de memoria) y con decenas de logEvento() en el
// proyecto eso se come cientos de bytes de RAM para nada.
// -----------------------------------------------------------------------------
template <typename T>
static void logEventoImpl(T msg) {
    // Imprimir también por monitor serie - Comentar si se utiliza el OBD2
    // Serial.print(F("[LOG] "));
    // Serial.println(msg);

    if (!sdOK || !logFile) return;

    if (info_gps.valido) {
        logFile.print('[');
        logFile.print(info_gps.timestamp_iso);
        logFile.print(F("] "));
    }
    logFile.println(msg);
    logFile.flush();
}

void logEvento(const char* msg) { logEventoImpl(msg); }
void logEvento(const __FlashStringHelper* msg) { logEventoImpl(msg); }

// -----------------------------------------------------------------------------
// Crea el fichero CSV con nombre único basado en el timestamp del primer fix
// Solo se llama una vez, cuando info_gps.valido pasa a true por primera vez
// -----------------------------------------------------------------------------

void crearFicheroCSV() {
    if (!sdOK || dataFileOpen) return;
 
    // Nombre único: YYYYMMDDHHMMSS.csv, derivado de timestamp_iso quitando '-', 'T' y ':'.
    // Se corta en el '.' de los milisegundos (timestamp_iso trae ".mmm"): el
    // nombre no necesita esa resolucion, con segundos ya es unico de sobra.
    uint8_t pos = 0;
    for (const char *c = info_gps.timestamp_iso; *c != '\0' && pos < sizeof(dataFileName) - 5; c++) {
        if (*c == '.') break;
        if (*c != '-' && *c != 'T' && *c != ':') {
            dataFileName[pos++] = *c;
        }
    }
    snprintf(dataFileName + pos, sizeof(dataFileName) - pos, ".csv");

    char msg[60];
    snprintf(msg, sizeof(msg), "[SD] Intentando crear fichero: %s", dataFileName);
    logEvento(msg);
 
    if (sd.exists(dataFileName)) {
        sd.remove(dataFileName);
        logEvento(F("[SD] AVISO - fichero duplicado eliminado"));
    }
 
    dataFile = sd.open(dataFileName, FILE_WRITE);
    if (!dataFile) {
        logEvento(F("[SD] ERROR - No se pudo crear el fichero CSV"));
        return;
    }
 
    // ── Cabecera CSV  ─────────────────────────────────────────────────────
    // La misma cadena que se publica por MQTT: se compone una sola vez en
    // construirCabeceraCSV() y aqui solo se vuelca.
    dataFile.println(construirCabeceraCSV());
    dataFile.flush();

    // Cerrar y reabrir en modo append: crear una entrada de directorio nueva
    // es la operacion mas fragil ante un corte de luz/reset (a diferencia de
    // LOG.TXT, que ya existia de sesiones anteriores). Este close() unico
    // consolida esa entrada nueva en la tarjeta; a partir de aqui cada
    // escribirLineaCSV() solo AMPLIA un fichero ya existente, igual que
    // LOG.TXT, que si sobrevive a un corte abrupto.
    dataFile.close();
    dataFile = sd.open(dataFileName, FILE_WRITE);
    if (!dataFile) {
        logEvento(F("[SD] ERROR - No se pudo reabrir el fichero CSV tras la cabecera"));
        return;
    }

    dataFileOpen = true;

    // Reutilizamos el mismo buffer 'msg' declarado arriba
    snprintf(msg, sizeof(msg), "[SD] Fichero creado: %s", dataFileName);
    logEvento(msg);
}
 
// -----------------------------------------------------------------------------
// Escribe una línea CSV con los datos actuales de todos los módulos.
// Es exactamente la misma cadena que se publica por MQTT.
// -----------------------------------------------------------------------------
void escribirLineaCSV() {
    if (!sdOK) {
        return;
    }
    if (!dataFileOpen) {
        logEvento(F("[SD] ERROR - fichero no marcado como abierto"));
        return;
    }
    if (!dataFile) {
        logEvento(F("[SD] ERROR - handle de fichero invalido"));
        return;
    }

    dataFile.println(construirLineaCSV());
    dataFile.flush();   // Escritura inmediata: protege contra cortes de luz
}

// -----------------------------------------------------------------------------
// Cierra ambos ficheros de forma segura
// Llamar antes de cortar la alimentación si es posible
// -----------------------------------------------------------------------------
void cerrarSD() {
    if (dataFile) {
        dataFile.flush();
        dataFile.close();
    }
    if (logFile) {
        logFile.flush();
        logFile.close();
    }
    dataFileOpen = false;
}
