#ifndef CONFIG_H
#define CONFIG_H

#define BAUD_DEBUG   38400   // Monitor serie (USB hacia el PC) (Mismo que ELM327)

// =====================================================
//  Asignacion de puertos serie hardware del Mega 2560
// =====================================================

//  Serial1  -> GPS NEO-6M          pines 18 (TX) / 19 (RX)
#define SERIAL_GPS      Serial1
#define BAUD_GPS        115200
#define FREQ_GPS        5

// PIN_SD_CS movido temporalmente de 53 a 9 para descartar que el pin 53
// (SS por hardware del Mega) este danado -- MISO/MOSI/SCK siguen fijos en
// 50/51/52. Si tras recablear el CS al pin 9 la SD se detecta, el problema
// era el pin 53; si sigue igual, el problema esta en el modulo SD.
#define PIN_SD_CS       53
#define SD_MHZ          50

#define SERIAL_ACCEL    Serial2 
#define BAUD_ACCEL        115200
#define _orientacionVertical  0

// Pantalla solo con colores azules
#define MODO_PANTALLA_AZUL 1

#define SERIAL_GPRS     Serial3
#define BAUD_GPRS        115200

#define SERIAL_OBD      Serial
#define BAUD_OBD        38400

#endif