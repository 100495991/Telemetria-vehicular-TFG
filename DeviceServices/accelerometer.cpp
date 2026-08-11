// =============================================================================
// accelerometer.cpp  —  Parser binario WitMotion WT31N
//
// Paquete 0x51 (aceleración ±2g):
//   [0x55][0x51][AxL][AxH][AyL][AyH][AzL][AzH][TL][TH][Sum]
//   ax = (int16(AxH<<8|AxL)) / 32768.0 * 16
//
// Paquete 0x53 (ángulos):
//   [0x55][0x53][RollL][RollH][PitchL][PitchH][0x00][0x00][TL][TH][Sum]
//   Roll  = (int16(RollH<<8|RollL))  / 32768.0 * 180
//   Pitch = (int16(PitchH<<8|PitchL))/ 32768.0 * 180
//
// Temperatura (ambos paquetes):
//   T = (int16(TH<<8|TL)) / 340.0 + 36.25
//
// Checksum: suma de bytes 0..9 mod 256
// =============================================================================

#include "accelerometer.h"
#include "sd_manager.h"

// -----------------------------------------------------------------------------
// Definición de la variable global (declarada como extern en accelerometer.h)
// -----------------------------------------------------------------------------
InfoAccel accel;

static uint8_t _pkt[11];
static uint8_t _pktIdx = 0;

// bool _orientacionVertical = true;

// Sin tramas durante este tiempo, la lectura se considera caducada
static const unsigned long TIMEOUT_ACCEL_MS = 1000;

// -----------------------------------------------------------------------------
// Inicializa el puerto serie del acelerometro y su estructura de datos
// -----------------------------------------------------------------------------
void setupAccelerometer() {
    SERIAL_ACCEL.begin(BAUD_ACCEL);
    inicializarInfoAccel();
    logEvento(F("[ACCEL] Acelerometro inicializado correctamente"));
}

// -----------------------------------------------------------------------------
// Inicializa la estructura con valores por defecto
// -----------------------------------------------------------------------------
void inicializarInfoAccel() {
    accel.ax                = 0.0f;
    accel.ay                = 0.0f;
    accel.az                = 0.0f;
    accel.roll              = 0.0f;
    accel.pitch             = 0.0f;
    accel.temperatura       = 0.0f;
    accel.valido            = false;
    accel.ultima_lectura_ms = 0;
}

// -----------------------------------------------------------------------------
// Parsea un paquete completo de 11 bytes y actualiza la estructura
// -----------------------------------------------------------------------------
static void parsearPaquete() {
    // Verificar checksum: suma de bytes 0..9 mod 256
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 10; i++) sum += _pkt[i];
    if (sum != _pkt[10]) return;

    // Combinar low+high en int16 con signo (igual que el código de ejemplo del datasheet)
    int16_t raw0 = (int16_t)((short)_pkt[3] << 8 | _pkt[2]);
    int16_t raw1 = (int16_t)((short)_pkt[5] << 8 | _pkt[4]);
    int16_t raw2 = (int16_t)((short)_pkt[7] << 8 | _pkt[6]);
    int16_t rawT = (int16_t)((short)_pkt[9] << 8 | _pkt[8]);

    float temperatura = rawT / 340.0f + 36.25f;

    switch (_pkt[1]) {
        case 0x51: { // Aceleración
            accel.ax = raw0 / 32768.0f * 16.0f;
            accel.ay = raw1 / 32768.0f * 16.0f;
            accel.az = raw2 / 32768.0f * 16.0f;
            
            if (_orientacionVertical) {
                float ax = accel.ax;
                accel.ax = accel.az;
                accel.az = ax;
            }

            accel.temperatura = temperatura;
            break;
        }

        case 0x53: { // Angulos
            // dos valores iniciales sumados por fallo de calibración.
            // roll y pitch van intercambiados respecto al datasheet por la
            // disposición física del sensor en el dispositivo: raw0 (eje X
            // del sensor) es el pitch real y raw1 (eje Y del sensor) es el
            // roll real.
            accel.roll  = 2.3f + raw1 / 32768.0f * 180.0f;
            accel.pitch = 9.7f + raw0 / 32768.0f * 180.0f;
            // raw2 siempre 0x0000 en WT31N (sin Yaw)

            if (_orientacionVertical) {
                accel.roll += 90.0f; // WT31N montado verticalmente, eje Y apunta hacia el suelo
            }

            accel.temperatura = temperatura;
            break;
        }

        default:
            return;
    }

    accel.valido            = true;
    accel.ultima_lectura_ms = millis();
}

// -----------------------------------------------------------------------------
// Debe llamarse en cada iteración del loop().
// Sincroniza en el header 0x55 y acumula los 11 bytes del paquete.
// -----------------------------------------------------------------------------
void leerAccel() {
    while (SERIAL_ACCEL.available() > 0) {
        uint8_t b = (uint8_t)SERIAL_ACCEL.read();

        if (_pktIdx == 0) {
            if (b != 0x55) continue;
        } else if (_pktIdx == 1) {
            if (b != 0x51 && b != 0x53) {
                _pktIdx = 0;
                continue;
            }
        }

        _pkt[_pktIdx++] = b;

        if (_pktIdx == 11) {
            parsearPaquete();
            _pktIdx = 0;
        }
    }

    // Si el IMU deja de emitir (cable suelto, sensor colgado), los ultimos
    // valores dejan de ser representativos: se invalidan para que el CSV y el
    // MQTT publiquen campos vacios en vez de una lectura congelada.
    if (accel.valido && millis() - accel.ultima_lectura_ms > TIMEOUT_ACCEL_MS) {
        accel.valido = false;
        logEvento(F("[ACCEL] AVISO - Sin tramas del IMU, datos marcados como no validos"));
    }
}

// -----------------------------------------------------------------------------
// Cabecera CSV — columnas de este módulo, en el mismo orden que escribirDatosAccel
// -----------------------------------------------------------------------------
static const char CABECERA_ACCEL[] PROGMEM =
    "accel_ax_g,accel_ay_g,accel_az_g,accel_roll_deg,accel_pitch_deg,accel_temperature";

size_t escribirCabeceraAccel(char* dst, size_t espacio) {
    return csvCopiarCabecera(dst, espacio, CABECERA_ACCEL);
}

// -----------------------------------------------------------------------------
// Línea CSV — valores de InfoAccel, en el mismo orden que escribirCabeceraAccel
// -----------------------------------------------------------------------------
size_t escribirDatosAccel(char* dst, size_t espacio) {
    // Sin trama valida del IMU se dejan las columnas vacias: los ceros de
    // inicializacion se confundirian con un vehiculo parado y nivelado.
    if (!accel.valido) {
        return csvCamposVacios(dst, espacio, CABECERA_ACCEL);
    }

    char ax_str[10], ay_str[10], az_str[10];
    char roll_str[9], pitch_str[9], temp_str[9];

    dtostrf(accel.ax,          0, 4, ax_str);
    dtostrf(accel.ay,          0, 4, ay_str);
    dtostrf(accel.az,          0, 4, az_str);
    dtostrf(accel.roll,        0, 2, roll_str);
    dtostrf(accel.pitch,       0, 2, pitch_str);
    dtostrf(accel.temperatura, 0, 2, temp_str);

    return csvEscritos(snprintf(dst, espacio, "%s,%s,%s,%s,%s,%s",
        ax_str, ay_str, az_str, roll_str, pitch_str, temp_str), espacio);
}

// -----------------------------------------------------------------------------
// Imprime toda la estructura por el monitor serie
// -----------------------------------------------------------------------------
void imprimirAccel() {
    Serial.println(F("┌─ INFO ACELEROMETRO ───────────────────────────┐"));
    Serial.print(F("  ax (g)           : ")); Serial.println(accel.ax,    4);
    Serial.print(F("  ay (g)           : ")); Serial.println(accel.ay,    4);
    Serial.print(F("  az (g)           : ")); Serial.println(accel.az,    4);
    Serial.print(F("  roll  (°)        : ")); Serial.println(accel.roll,  2);
    Serial.print(F("  pitch (°)        : ")); Serial.println(accel.pitch, 2);
    Serial.print(F("  temperatura (°C) : ")); Serial.println(accel.temperatura, 2);
    Serial.print(F("  valido           : ")); Serial.println(accel.valido ? "Si" : "No");
    Serial.print(F("  ultima_lectura   : ")); Serial.print(accel.ultima_lectura_ms); Serial.println(F(" ms"));
    Serial.println(F("└───────────────────────────────────────────────┘\n"));
}
