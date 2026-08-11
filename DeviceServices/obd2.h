#ifndef OBD2_H
#define OBD2_H

// =============================================================================
// obd2.h  —  Declaraciones del módulo OBD-II (ELM327, vía ELMduino)
// =============================================================================

#include <Arduino.h>
#include <SdFat.h>
#include "ELMduino.h"
#include "config.h"

// -----------------------------------------------------------------------------
// Estructura de datos  —  equivalente al diccionario "info" de los otros módulos
// -----------------------------------------------------------------------------
struct InfoObd2 {
    float    rpm;                // rev/min
    float    velocidad_kmh;      // km/h
    float    carga_motor;        // %
    float    pos_acelerador;     // %
    float    temp_refrigerante;  // °C
    uint16_t tiempo_arranque_s;  // s desde el ultimo arranque del motor — permite diferenciar viajes
    float    tasa_consumo_lh;    // L/h — PID opcional (015E), no todos los vehiculos lo soportan
    bool     valido;
};

// -----------------------------------------------------------------------------
// Variables globales compartidas — definidas UNA SOLA VEZ en obd2.cpp
// -----------------------------------------------------------------------------
extern InfoObd2 info_obd2;
extern ELM327   myELM327;

// -----------------------------------------------------------------------------
// Prototipos de funciones
// -----------------------------------------------------------------------------
void setupObd2();
void inicializarInfoObd2();
void leerObd2();  // Debe llamarse en cada iteración del loop()

// -----------------------------------------------------------------------------
// Serialización CSV — cada módulo se encarga de sus propias columnas
// -----------------------------------------------------------------------------
// Escriben en 'dst' (como mucho 'espacio' bytes, '\0' incluido) y devuelven los
// caracteres escritos. Sin lectura fiable, escribirDatosObd2 deja las columnas vacias.
size_t escribirCabeceraObd2(char* dst, size_t espacio);
size_t escribirDatosObd2(char* dst, size_t espacio);

#endif // OBD2_H
