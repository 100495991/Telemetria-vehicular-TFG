#ifndef SD_MANAGER_H
#define SD_MANAGER_H

// =============================================================================
// sd_manager.h  —  Declaraciones del módulo de gestión de la SD
//
// Pines SPI en Arduino Mega:
//   50 → MISO  (fijo, hardware)
//   51 → MOSI  (fijo, hardware)
//   52 → SCK   (fijo, hardware)
//   53 → SS    (Chip Select, configurable)
// =============================================================================

#include <Arduino.h>
#include <SdFat.h>
#include "config.h"


extern SdFat32 sd;
extern bool  sdOK;
extern bool  dataFileOpen;
extern char  dataFileName[30];
extern File32 dataFile;
extern File32 logFile;

// -----------------------------------------------------------------------------
// Tamaño del buffer donde se compone la trama completa (cabecera o datos).
// Debe ser >= que la cabecera unida de todos los módulos, que es la cadena más
// larga. MQTT_MAX_PACKET_SIZE (gprs.h) esta atado a este valor, asi que subir
// TRAMA_MAX ya amplia tambien el buffer de PubSubClient automaticamente.
// -----------------------------------------------------------------------------
#define TRAMA_MAX 600

// Prototipos de funciones
void setupSDCard();
void logEvento(const char* msg);
void logEvento(const __FlashStringHelper* msg);  // usar con logEvento(F("...")) para no gastar SRAM en literales
void crearFicheroCSV();
void escribirLineaCSV();
void cerrarSD();

// -----------------------------------------------------------------------------
// Composición de la trama única (misma cadena para la SD y para MQTT).
// Ambas devuelven un puntero a un buffer estático interno: hay que usar el
// contenido antes de la siguiente llamada, no se puede guardar el puntero.
// -----------------------------------------------------------------------------
const char* construirCabeceraCSV();  // nombres de columna de todos los módulos
const char* construirLineaCSV();     // valores actuales de todos los módulos

// -----------------------------------------------------------------------------
// Utilidades compartidas de serialización CSV.
//
// Todos los módulos exponen escribirCabeceraX()/escribirDatosX() con la misma
// firma: escriben en 'dst' como mucho 'espacio' bytes (incluido el '\0') y
// devuelven cuántos caracteres han escrito realmente.
// -----------------------------------------------------------------------------

// Traduce el valor devuelto por snprintf() a caracteres realmente escritos,
// teniendo en cuenta el truncado (snprintf devuelve lo que HABRÍA escrito).
size_t csvEscritos(int resultadoSnprintf, size_t espacio);

// Copia a 'dst' una cabecera almacenada en PROGMEM.
size_t csvCopiarCabecera(char* dst, size_t espacio, const char* cabeceraPGM);

// Escribe la fila "sin datos" correspondiente a esa cabecera: tantos separadores
// como columnas tenga, con todos los campos vacíos. Al derivarse de la propia
// cabecera, añadir una columna nueva no puede descuadrar el CSV.
size_t csvCamposVacios(char* dst, size_t espacio, const char* cabeceraPGM);

#endif // SD_MANAGER_H
