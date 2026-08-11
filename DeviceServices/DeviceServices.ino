
#include "gps.h"
#include "sd_manager.h"
#include "accelerometer.h"
#include "temperature_sensor.h"
#include "pantallaLCD.h"
#include "gprs.h"
#include "obd2.h"

bool primerFixObtenido = false;

// =============================================================================
void setup() {

    Serial.begin(BAUD_DEBUG);    // Monitor serie del PC (debug)
    delay(500);
    setupSDCard();               // Inicializa SD + abre LOG.TXT
    setupGPS();                  // Inicializa Serial1 + estructura InfoGPS
    setupAccelerometer();        // Inicializa Serial2 + acelerometro
    setupTemperatureSensor();    // Inicializa I2C + sensor IR MLX90614
    setupObd2();                 // Inicializa el ELM327
    setupPantalla();             // Inicializa pantalla TFT
    setupGPRS();                 // Inicializa Serial3 + modulo GPRS

}

// =============================================================================
void loop() {
    // 1. Leer informacion de cada modulo
    leerGPS();
    leerAccel();
    leerTemperatura();
    leerObd2();
    gestionarGPRS();
    actualizarPantalla();

    // 2. Procesar cuando llega una trama nueva con posición actualizada
    if (gps.location.isUpdated()) {

        // 3. Volcar datos de TinyGPS++ a la estructura InfoGPS
        actualizarGPS();

        // 4. Gestión del fichero CSV en SD
        if (info_gps.valido) {
            if (!primerFixObtenido) {
                char hdop_str[8];
                dtostrf(info_gps.hdop, 0, 2, hdop_str);
                char msg[50];
                snprintf(msg, sizeof(msg), "GPS: Primer fix valido obtenido (HDOP=%s)", hdop_str);
                logEvento(msg);
                crearFicheroCSV();
                primerFixObtenido = true;
            }
            escribirLineaCSV();
        }
    }
}

