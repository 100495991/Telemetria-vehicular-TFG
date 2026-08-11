// =============================================================================
// temperature_sensor.cpp  —  Implementación del módulo IR MLX90614
//
// Usa la libreria Adafruit_MLX90614 (I2C por hardware, bus real del Mega
// en los pines 20/21). Las lecturas son bloqueantes pero rapidas (I2C real,
// no bit-banging), asi que leerTemperatura() se limita a muestrear como
// mucho una vez por segundo para no recargar el bus en cada vuelta del loop().
// =============================================================================

#include "temperature_sensor.h"
#include "sd_manager.h"
#include <Wire.h>

// -----------------------------------------------------------------------------
// Definición de las variables globales (declaradas como extern en el .h)
// -----------------------------------------------------------------------------
InfoTemp info_temp;
Adafruit_MLX90614 termometroIR = Adafruit_MLX90614();

static const unsigned long INTERVALO_MUESTREO_MS = 250;
static unsigned long ultimaLecturaMs = 0;

static const double EMISIVIDAD_NEUMATICO = 0.95;  // Goma del neumatico

// -----------------------------------------------------------------------------
// Rangos de cordura (datasheet MLX90614): fuera de estos margenes la lectura es
// fisicamente imposible, asi que solo puede venir de una trama I2C corrupta.
// La libreria Adafruit ignora el byte PEC al leer (read16 no comprueba el CRC),
// de modo que un bit corrompido por ruido en el bus se convierte directamente
// en una temperatura absurda del tipo -100 C. Hay que filtrarlo aqui.
// -----------------------------------------------------------------------------
static const float TEMP_OBJ_MIN = -70.0f;    // Rango de objeto: -70 .. +380 C
static const float TEMP_OBJ_MAX = 380.0f;
static const float TEMP_AMB_MIN = -40.0f;    // Rango de ambiente: -40 .. +125 C
static const float TEMP_AMB_MAX = 125.0f;

// Fallos seguidos antes de dar por colgado el bus e intentar reiniciar el sensor
static const uint8_t MAX_FALLOS_CONSECUTIVOS = 8;   // ~2 s a 250 ms por muestra
static uint8_t fallosConsecutivos = 0;

// true solo si setupTemperatureSensor() consiguio una primera lectura valida.
// Si es false, leerTemperatura() no hace nada: sin sensor real en el bus, cada
// intento fallaria igual y solo serviria para llenar el log de avisos.
static bool tempDisponible = false;

// -----------------------------------------------------------------------------
// true si la lectura no es NaN y cae dentro del rango fisico del sensor
// -----------------------------------------------------------------------------
static bool lecturaValida(float valor, float minimo, float maximo) {
    return !isnan(valor) && valor >= minimo && valor <= maximo;
}

// -----------------------------------------------------------------------------
// Inicializa la estructura con valores por defecto
// -----------------------------------------------------------------------------
void inicializarInfoTemp() {
    info_temp.temperatura_ambiente = 0.0f;
    info_temp.temperatura_objeto   = 0.0f;
    info_temp.valido               = false;
}

// -----------------------------------------------------------------------------
// Recuperación de bus I2C
// Soluciona el bloqueo del esclavo tras un reset del Arduino sin power-cycle.
// -----------------------------------------------------------------------------
void recuperarBusI2C() {
    const uint8_t PIN_SDA = 20;  // Mega: SDA
    const uint8_t PIN_SCL = 21;  // Mega: SCL

    pinMode(PIN_SDA, INPUT_PULLUP);
    pinMode(PIN_SCL, INPUT_PULLUP);

    // Si SDA ya está alto, el bus está libre, no hace falta nada más
    if (digitalRead(PIN_SDA) == HIGH) return;

    logEvento(F("[I2C] AVISO - Bus bloqueado detectado, recuperando..."));

    pinMode(PIN_SCL, OUTPUT);

    // Generamos hasta 9 pulsos de reloj: permite al esclavo terminar
    // de transmitir el byte en el que se quedó atascado
    for (uint8_t i = 0; i < 9; i++) {
        digitalWrite(PIN_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(PIN_SCL, HIGH);
        delayMicroseconds(5);
        if (digitalRead(PIN_SDA) == HIGH) break;  // ya se liberó, no seguir
    }

    // Generamos una condición STOP manual (SDA sube mientras SCL está alto)
    pinMode(PIN_SDA, OUTPUT);
    digitalWrite(PIN_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(PIN_SDA, HIGH);
    delayMicroseconds(5);

    // Devolvemos los pines a su modo normal de entrada con pull-up
    // antes de que Wire.begin() los reconfigure como I2C
    pinMode(PIN_SDA, INPUT_PULLUP);
    pinMode(PIN_SCL, INPUT_PULLUP);

    logEvento(F("[I2C] Bus recuperado correctamente"));
}

// -----------------------------------------------------------------------------
// Ajusta la emisividad SOLO si hace falta.
// writeEmissivity() escribe en la EEPROM del sensor (borrado + escritura), asi
// que hacerlo en cada arranque desgasta la celda sin necesidad; ademas, si se
// interrumpe a medias deja la EEPROM a un valor arbitrario y todas las lecturas
// posteriores salen descalibradas. Si la lectura previa falla (NaN) no se
// escribe nada: con el bus en mal estado, escribir es peor que no tocar.
// -----------------------------------------------------------------------------
static void aplicarEmisividad() {
    double actual = termometroIR.readEmissivity();
    if (isnan(actual)) return;
    if (fabs(actual - EMISIVIDAD_NEUMATICO) > 0.01) {
        termometroIR.writeEmissivity(EMISIVIDAD_NEUMATICO);
        logEvento(F("[TEMP] Emisividad ajustada a 0.95 en la EEPROM del sensor"));
    }
}

// -----------------------------------------------------------------------------
// Recupera el bus y reinicia el sensor. Devuelve true si begin() responde.
// -----------------------------------------------------------------------------
static bool reiniciarSensor() {
    recuperarBusI2C();

    Wire.begin();
    // Sin esto, si el bus se queda colgado (p.ej. sensor desconectado a
    // medias, sujetando SDA/SCL), termometroIR.begin() o cualquier lectura
    // posterior puede bloquear el programa ENTERO para siempre (Wire no
    // tiene timeout por defecto) -- ni GPS ni MQTT volverian a ejecutarse.
    // Con el timeout, una transaccion colgada se aborta y se resetea el
    // periferico TWI en vez de bloquear indefinidamente.
    Wire.setWireTimeout(25000, true);  // 25ms, resetea el bus TWI si vence

    if (!termometroIR.begin()) return false;
    aplicarEmisividad();
    return true;
}

// -----------------------------------------------------------------------------
// Inicializa el sensor IR
// -----------------------------------------------------------------------------
void setupTemperatureSensor() {
    inicializarInfoTemp();

    
    // Un unico intento de deteccion del sensor por I2C. Una vez detectado, la
    // validez de cada lectura -incluida la primera- se trata igual que en
    // leerTemperatura(): si sale fuera de rango se invalida y no se guarda,
    // sin volver a reiniciar el sensor por eso.
    if (reiniciarSensor()) {
        tempDisponible = true;
        logEvento(F("[TEMP] Sensor IR MLX90614 inicializado correctamente"));
    } else {
        // Sin sensor al primer intento (probablemente desconectado): se
        // desactiva el modulo. leerTemperatura() no volvera a tocarlo y el
        // CSV dejara sus columnas vacias, sin reintentos ni avisos repetidos.
        //
        // OJO: aqui se usa Serial.println() directo, NO logEvento(). Se
        // diagnostico (sesion de depuracion con puntos de control) que
        // logEvento() -- con CUALQUIER mensaje, con o sin F(), incluso con
        // RAM libre de sobra -- se corrompe/cuelga de forma reproducible
        // llamado justo aqui, tras la secuencia recuperarBusI2C()+Wire.begin()+
        // termometroIR.begin(). No se identifico la causa raiz (no es RAM, no
        // es el bus I2C en si -aislado en un sketch aparte funciona bien-, no
        // es el contenido del mensaje). Pendiente de investigar mas a fondo;
        // mientras tanto se evita la llamada que dispara el problema.
        Serial.println(F("[TEMP] ERROR - Sensor IR no detectado, modulo TEMP desactivado"));
        return;
    }
    
    termometroIR.begin();
    
    // Primera lectura: se guarda solo si cae dentro del rango fisico del sensor.
    float objeto   = termometroIR.readObjectTempC();
    float ambiente = termometroIR.readAmbientTempC();
    if (lecturaValida(objeto, TEMP_OBJ_MIN, TEMP_OBJ_MAX) &&
        lecturaValida(ambiente, TEMP_AMB_MIN, TEMP_AMB_MAX)) {
        info_temp.temperatura_objeto   = objeto;
        info_temp.temperatura_ambiente = ambiente;
        info_temp.valido               = true;
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

    // Se descarta la muestra completa si CUALQUIERA de las dos es irreal: si una
    // trama I2C viene corrupta, la otra del mismo ciclo tampoco es de fiar.
    if (!lecturaValida(ambiente, TEMP_AMB_MIN, TEMP_AMB_MAX) ||
        !lecturaValida(objeto,   TEMP_OBJ_MIN, TEMP_OBJ_MAX)) {

        // No se sobrescriben los ultimos valores buenos con basura: solo se
        // marca la estructura como no valida para que pantalla y CSV lo sepan.
        info_temp.valido = false;

        if (fallosConsecutivos < 255) fallosConsecutivos++;

        if (fallosConsecutivos == MAX_FALLOS_CONSECUTIVOS) {
            logEvento(F("[TEMP] AVISO - Lecturas irreales seguidas, reiniciando bus I2C y sensor..."));
            if (reiniciarSensor()) {
                logEvento(F("[TEMP] Sensor IR reiniciado"));
            } else {
                logEvento(F("[TEMP] ERROR - Sensor IR sigue sin responder"));
            }
            fallosConsecutivos = 0;   // se vuelve a dar margen antes de reintentar
        }
        return;
    }

    info_temp.temperatura_ambiente = ambiente;
    info_temp.temperatura_objeto   = objeto;
    info_temp.valido               = true;
    fallosConsecutivos             = 0;
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
