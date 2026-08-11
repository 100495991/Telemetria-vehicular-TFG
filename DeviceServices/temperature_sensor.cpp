// =============================================================================
// temperature_sensor.cpp  —  Implementación del módulo IR MLX90614
//
// Usa la libreria Adafruit_MLX90614 (I2C por hardware, bus real del Mega
// en los pines 20/21). Las lecturas son bloqueantes pero rapidas (I2C real,
// no bit-banging), asi que leerTemperatura() se limita a muestrear como
// mucho una vez cada INTERVALO_MUESTREO_MS para no recargar el bus en cada
// vuelta del loop().
// =============================================================================

#include "temperature_sensor.h"
#include "sd_manager.h"

// -----------------------------------------------------------------------------
// Definición de las variables globales (declaradas como extern en el .h)
// -----------------------------------------------------------------------------
InfoTemp info_temp;
Adafruit_MLX90614 termometroIR = Adafruit_MLX90614();

static const unsigned long INTERVALO_MUESTREO_MS = 250;
static unsigned long ultimaLecturaMs = 0;

// true solo si el sensor respondio en setupTemperatureSensor().
// Si es false, leerTemperatura() no hace nada.
static bool tempDisponible = false;

// -----------------------------------------------------------------------------
// Inicializa la estructura con valores por defecto
// -----------------------------------------------------------------------------
void inicializarInfoTemp() {
    info_temp.temperatura_ambiente = 0.0f;
    info_temp.temperatura_objeto   = 0.0f;
    info_temp.valido               = false;
}

// -----------------------------------------------------------------------------
// Inicializa el sensor IR. Si no responde, el modulo queda desactivado.
// -----------------------------------------------------------------------------
void setupTemperatureSensor() {
    inicializarInfoTemp();

    Wire.begin();
    Wire.setWireTimeout(25000, true);

    if (termometroIR.begin()) {
        tempDisponible = true;
        logEvento(F("[TEMP] Sensor IR MLX90614 inicializado correctamente"));
    } else {
        logEvento(F("[TEMP] ERROR - Sensor IR no detectado, modulo TEMP desactivado"));
    }
}

// -----------------------------------------------------------------------------
// Debe llamarse en cada iteración del loop().
// Muestrea el sensor como mucho una vez cada INTERVALO_MUESTREO_MS.
// -----------------------------------------------------------------------------
void leerTemperatura() {
    if (!tempDisponible) return;

    unsigned long ahora = millis();
    if (ahora - ultimaLecturaMs < INTERVALO_MUESTREO_MS) return;
    ultimaLecturaMs = ahora;

    float ambiente = termometroIR.readAmbientTempC();
    float objeto   = termometroIR.readObjectTempC();

    if (isnan(ambiente) || isnan(objeto)) {
        info_temp.valido = false;
        return;
    }

    info_temp.temperatura_ambiente = ambiente;
    info_temp.temperatura_objeto   = objeto;
    info_temp.valido               = true;
}

// -----------------------------------------------------------------------------
// Cabecera CSV — columnas de este módulo, en el mismo orden que escribirDatosTemp
// -----------------------------------------------------------------------------
static const char CABECERA_TEMP[] PROGMEM =
    "temperatura_ambiente,temperatura_neumatico";

size_t escribirCabeceraTemp(char* dst, size_t espacio) {
    return csvCopiarCabecera(dst, espacio, CABECERA_TEMP);
}

// -----------------------------------------------------------------------------
// Línea CSV — valores de InfoTemp, en el mismo orden que escribirCabeceraTemp
// -----------------------------------------------------------------------------
size_t escribirDatosTemp(char* dst, size_t espacio) {
    // Sin lectura fiable se dejan las celdas vacias en vez de repetir el ultimo
    // valor bueno: en el CSV debe notarse que ahi no hubo medida.
    if (!info_temp.valido) {
        return csvCamposVacios(dst, espacio, CABECERA_TEMP);
    }

    char ambientTempStr[10];
    char objectTempStr[10];

    dtostrf(info_temp.temperatura_ambiente, 0, 2, ambientTempStr);
    dtostrf(info_temp.temperatura_objeto,   0, 2, objectTempStr);

    return csvEscritos(snprintf(dst, espacio, "%s,%s",
        ambientTempStr, objectTempStr), espacio);
}