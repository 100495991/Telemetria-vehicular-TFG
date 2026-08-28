// =============================================================================
// accelerometer.cpp  —  Parser binario WitMotion WT31N
//
// Paquete 0x51 (aceleracion):
//   [0x55][0x51][AxL][AxH][AyL][AyH][AzL][AzH][TL][TH][Sum]
//
// Paquete 0x53 (angulos): se recibe solo por la temperatura. Los angulos que
// entrega el modulo NO se usan: medidos contra una calibracion de 6 posiciones
// daban ~7 grados de error cerca de la vertical, ademas de tener un punto
// singular en +-90 en el eje Y (apartado 3.1 del datasheet). Roll y pitch se
// calculan aqui a partir del vector de gravedad completo, que no tiene esa
// singularidad.
//
// Checksum: suma de bytes 0..9 mod 256
// =============================================================================

#include "accelerometer.h"
#include "sd_manager.h"
#include <math.h>

InfoAccel accel;

static uint8_t _pkt[11];
static uint8_t _pktIdx = 0;

// Sin tramas durante este tiempo, la lectura se considera caducada
static const unsigned long TIMEOUT_ACCEL_MS = 1000;

// -----------------------------------------------------------------------------
// CALIBRACION DE 6 POSICIONES
//
// Obtenida con calibracion_wt31n.ino. Trabaja en cuentas crudas, de modo que
// la constante 16 del datasheet queda absorbida en ESC_RAW y no aparece aqui.
//
//   g = (raw - OFF_RAW[i]) / ESC_RAW[i]
//
// Escala medida: 2046 cuentas/g en los tres ejes, dispersion 0.0 %, lo que
// confirma el fondo de escala de +-16 g de la formula del apartado 5.1.1
// (y desmiente los +-2 g de la tabla de especificaciones del 3.1).
//
// Recalibrar si se cambia de sensor o de placa.
// -----------------------------------------------------------------------------
static const float OFF_RAW[3] = {  127.44f,  -48.93f,    2.94f };
static const float ESC_RAW[3] = { 2047.01f, 2045.80f, 2047.04f };

// -----------------------------------------------------------------------------
// MATRIZ DE MONTAJE
//
// Convierte los ejes del sensor a los ejes del vehiculo (ISO 8855):
//   X = adelante,  Y = hacia la izquierda,  Z = hacia arriba
//
// Cada fila es un eje del vehiculo expresado en ejes del sensor. La matriz de
// abajo es la IDENTIDAD: solo es correcta si el sensor esta montado en
// horizontal, con su eje X apuntando al morro y su eje Z hacia el techo.
//
// PARA RELLENARLA, ver el procedimiento al final de este comentario.
//
// Sustituye a los intercambios de ejes de la version anterior. Un intercambio
// simple de dos ejes (ax <-> az) invierte la quiralidad del sistema y hace que
// los signos salgan mal en algunas orientaciones; una matriz de rotacion real
// tiene determinante +1 y no tiene ese problema. La comprobacion esta abajo,
// en verificarMontaje().
//
// PROCEDIMIENTO
//   1. Monta el dispositivo en el vehiculo en su posicion definitiva.
//   2. Con el coche parado y en llano, imprime las lecturas. El eje que marque
//      cerca de +1 g es el que apunta hacia ARRIBA  -> fila Z de la matriz.
//      Si marca -1 g, ese eje apunta hacia abajo    -> fila Z con signo -.
//   3. Frena fuerte en linea recta. El eje que reacciona es el longitudinal
//      -> fila X. Al frenar, la lectura debe ser NEGATIVA en el eje X del
//      vehiculo; si sale positiva, invierte el signo de esa fila.
//   4. La fila Y es la que queda. Su signo se fija con la regla de la mano
//      derecha, o se comprueba en una curva: girando a la izquierda, la
//      aceleracion lateral en Y debe salir positiva.
//   5. Ejecuta verificarMontaje() una vez y mira el log.
// -----------------------------------------------------------------------------
static const int8_t MONTAJE[3][3] = {
    //  sX   sY   sZ
    {    0,   -1,   0 },   // vehiculo X (adelante)
    {    1,   0,   0 },   // vehiculo Y (izquierda)
    {    0,   0,   1 },   // vehiculo Z (arriba)
};

// -----------------------------------------------------------------------------
void setupAccelerometer() {
    SERIAL_ACCEL.begin(BAUD_ACCEL);
    inicializarInfoAccel();
    verificarMontaje();
    logEvento(F("[ACCEL] Acelerometro inicializado correctamente"));
}

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
// Comprueba que MONTAJE es una rotacion valida: cada fila y cada columna debe
// tener exactamente un elemento no nulo, y el determinante debe ser +1. Un
// determinante -1 significa que se ha colado una reflexion y los signos de la
// aceleracion lateral saldran invertidos.
// -----------------------------------------------------------------------------
void verificarMontaje() {
    for (uint8_t f = 0; f < 3; f++) {
        uint8_t nf = 0, nc = 0;
        for (uint8_t c = 0; c < 3; c++) {
            if (MONTAJE[f][c] != 0) nf++;
            if (MONTAJE[c][f] != 0) nc++;
        }
        if (nf != 1 || nc != 1) {
            logEvento(F("[ACCEL] ERROR - MONTAJE mal formada"));
            return;
        }
    }

    int det = MONTAJE[0][0] * (MONTAJE[1][1]*MONTAJE[2][2] - MONTAJE[1][2]*MONTAJE[2][1])
            - MONTAJE[0][1] * (MONTAJE[1][0]*MONTAJE[2][2] - MONTAJE[1][2]*MONTAJE[2][0])
            + MONTAJE[0][2] * (MONTAJE[1][0]*MONTAJE[2][1] - MONTAJE[1][1]*MONTAJE[2][0]);

    if (det != 1) {
        logEvento(F("[ACCEL] ERROR - MONTAJE con determinante -1 (reflexion)"));
    }
}

// -----------------------------------------------------------------------------
static void parsearPaquete() {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 10; i++) sum += _pkt[i];
    if (sum != _pkt[10]) return;

    int16_t raw0 = (int16_t)(((int16_t)_pkt[3] << 8) | _pkt[2]);
    int16_t raw1 = (int16_t)(((int16_t)_pkt[5] << 8) | _pkt[4]);
    int16_t raw2 = (int16_t)(((int16_t)_pkt[7] << 8) | _pkt[6]);
    int16_t rawT = (int16_t)(((int16_t)_pkt[9] << 8) | _pkt[8]);

    accel.temperatura = rawT / 340.0f + 36.25f;

    switch (_pkt[1]) {
        case 0x51: {
            // 1) Cuentas crudas -> g, con offset y escala por eje
            float s[3];
            s[0] = ((float)raw0 - OFF_RAW[0]) / ESC_RAW[0];
            s[1] = ((float)raw1 - OFF_RAW[1]) / ESC_RAW[1];
            s[2] = ((float)raw2 - OFF_RAW[2]) / ESC_RAW[2];

            // 2) Ejes del sensor -> ejes del vehiculo
            float v[3];
            for (uint8_t f = 0; f < 3; f++) {
                v[f] = MONTAJE[f][0]*s[0] + MONTAJE[f][1]*s[1] + MONTAJE[f][2]*s[2];
            }
            accel.ax = v[0];
            accel.ay = v[1];
            accel.az = v[2];

            // 3) Actitud a partir del vector completo.
            //
            // atan2 es insensible a un error de escala comun a los tres ejes, y
            // usar el modulo de (ay,az) en el pitch evita el punto singular de
            // +-90 que tiene el eje Y del modulo.
            //
            // OJO: esto supone que la unica aceleracion presente es la gravedad.
            // Es cierto con el vehiculo parado o a velocidad constante, pero no
            // durante un frenazo o una curva: ahi el vector medido es la suma de
            // gravedad y aceleracion propia, y el angulo sale contaminado. Para
            // inclinacion de la calzada conviene filtrar paso bajo (ver nota al
            // final del fichero).
            float horiz = sqrtf(accel.ay*accel.ay + accel.az*accel.az);
            accel.pitch = atan2f(-accel.ax, horiz)   * 57.2957795f + 0.4f;
            accel.roll  = atan2f( accel.ay, accel.az) * 57.2957795f + 1.5f;
            break;
        }

        case 0x53:
            // Solo se aprovecha la temperatura, ya extraida arriba. Los angulos
            // del modulo se descartan a proposito.
            break;

        default:
            return;
    }

    accel.valido            = true;
    accel.ultima_lectura_ms = millis();
}

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

    if (accel.valido && millis() - accel.ultima_lectura_ms > TIMEOUT_ACCEL_MS) {
        accel.valido = false;
        logEvento(F("[ACCEL] AVISO - Sin tramas del IMU, datos marcados como no validos"));
    }
}

// -----------------------------------------------------------------------------
static const char CABECERA_ACCEL[] PROGMEM =
    "accel_ax_g,accel_ay_g,accel_az_g,accel_roll_deg,accel_pitch_deg,accel_temperature";

size_t escribirCabeceraAccel(char* dst, size_t espacio) {
    return csvCopiarCabecera(dst, espacio, CABECERA_ACCEL);
}

// -----------------------------------------------------------------------------
size_t escribirDatosAccel(char* dst, size_t espacio) {
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
void imprimirAccel() {
    Serial.println(F("┌─ INFO ACELEROMETRO ───────────────────────────┐"));
    Serial.print(F("  ax (g) adelante  : ")); Serial.println(accel.ax,    4);
    Serial.print(F("  ay (g) izquierda : ")); Serial.println(accel.ay,    4);
    Serial.print(F("  az (g) arriba    : ")); Serial.println(accel.az,    4);
    Serial.print(F("  roll  (°)        : ")); Serial.println(accel.roll,  2);
    Serial.print(F("  pitch (°)        : ")); Serial.println(accel.pitch, 2);
    Serial.print(F("  |a| (g)          : "));
    Serial.println(sqrtf(accel.ax*accel.ax + accel.ay*accel.ay + accel.az*accel.az), 4);
    Serial.print(F("  temperatura (°C) : ")); Serial.println(accel.temperatura, 2);
    Serial.print(F("  valido           : ")); Serial.println(accel.valido ? "Si" : "No");
    Serial.print(F("  ultima_lectura   : ")); Serial.print(accel.ultima_lectura_ms); Serial.println(F(" ms"));
    Serial.println(F("└───────────────────────────────────────────────┘\n"));
}

// =============================================================================
// NOTA sobre la inclinacion de la calzada
//
// Si en algun momento se quiere separar la pendiente de la carretera de la
// aceleracion propia del vehiculo, el camino es filtrar paso bajo el vector de
// aceleracion antes de calcular los angulos:
//
//   static float fx = 0, fy = 0, fz = 1;
//   const float A = 0.02f;                      // ~ 0.3 Hz a 10 Hz de muestreo
//   fx += A * (accel.ax - fx);
//   fy += A * (accel.ay - fy);
//   fz += A * (accel.az - fz);
//
// y usar fx/fy/fz en los atan2. Frenazos y curvas duran pocos segundos y el
// filtro los elimina; la pendiente de una rampa persiste y sobrevive. No es
// perfecto (una aceleracion mantenida en una recta larga si contamina), pero
// sin giroscopo es lo mejor que se puede hacer. El WT31N no lleva giroscopo
// accesible: emite el paquete 0x52 segun el codigo de ejemplo del datasheet,
// pero la tabla de especificaciones solo declara acelerometro.
// =============================================================================