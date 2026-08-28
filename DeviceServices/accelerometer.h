#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

// =============================================================================
// accelerometer.h  —  Declaraciones del módulo IMU WitMotion WT31N
//
// El WT31N emite dos tipos de paquete binario (11 bytes cada uno):
//   0x55 0x51 → aceleración  (±2g, 3 ejes)
//   0x55 0x53 → ángulos      (Roll ±180°, Pitch ±90°)
// No tiene giroscopio ni salida de Yaw.
// =============================================================================

#include <Arduino.h>
#include "config.h"

// -----------------------------------------------------------------------------
// Estructura de datos del WT31N
// -----------------------------------------------------------------------------
struct InfoAccel {
    // aceleraciones
    float ax;
    float ay;
    float az;
    // angulos
    float roll;
    float pitch;

    float temperatura;

    bool  valido;
    unsigned long ultima_lectura_ms;
};

// -----------------------------------------------------------------------------
// Variables globales compartidas — definidas UNA SOLA VEZ en accelerometer.cpp
// -----------------------------------------------------------------------------
extern InfoAccel accel;

// -----------------------------------------------------------------------------
// Prototipos de funciones
// -----------------------------------------------------------------------------
void setupAccelerometer();
void inicializarInfoAccel();
void leerAccel();
void imprimirAccel();

// -----------------------------------------------------------------------------
// Serialización CSV — cada módulo se encarga de sus propias columnas
// -----------------------------------------------------------------------------
// Escriben en 'dst' (como mucho 'espacio' bytes, '\0' incluido) y devuelven los
// caracteres escritos. Sin trama valida, escribirDatosAccel deja las columnas vacias.
size_t escribirCabeceraAccel(char* dst, size_t espacio);
size_t escribirDatosAccel(char* dst, size_t espacio);

#endif // ACCELEROMETER_H
