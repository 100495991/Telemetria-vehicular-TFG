#ifndef CONFIG_H
#define CONFIG_H

#define BAUD_DEBUG   38400   // Monitor serie (USB hacia el PC) (Mismo que ELM327)

// =====================================================
//  Asignacion de puertos serie hardware del Mega 2560
// =====================================================

#define SERIAL_GPS      Serial1
#define BAUD_GPS        115200
#define FREQ_GPS        5

#define PIN_SD_CS       53
#define SD_MHZ          50

#define SERIAL_ACCEL    Serial
#define BAUD_ACCEL        115200
#define _orientacionVertical  0

// Pantalla solo con colores azules
#define MODO_PANTALLA_AZUL 1

#define SERIAL_OBD      Serial2
#define BAUD_OBD        38400

#define SERIAL_GPRS     Serial3
#define BAUD_GPRS        115200


#endif