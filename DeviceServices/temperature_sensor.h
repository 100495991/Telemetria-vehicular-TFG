#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

// =============================================================================
// temperature_sensor.h  —  Declaraciones del módulo de temperatura IR MLX90614
// =============================================================================

#include <Arduino.h>
#include <SdFat.h>
#include <Adafruit_MLX90614.h>

// -----------------------------------------------------------------------------
// Estructura de datos  —  equivalente al diccionario "info" de los otros módulos
// -----------------------------------------------------------------------------
struct InfoTemp {
    float temperatura_ambiente;  // °C
    float temperatura_objeto;    // °C — superficie apuntada (neumático)
    bool  valido;
};

// -----------------------------------------------------------------------------
// Variables globales compartidas — definidas UNA SOLA VEZ en temperature_sensor.cpp
// -----------------------------------------------------------------------------
extern InfoTemp info_temp;
extern Adafruit_MLX90614 termometroIR;

// -----------------------------------------------------------------------------
// Prototipos de funciones
// -----------------------------------------------------------------------------
void setupTemperatureSensor();
void inicializarInfoTemp();
void leerTemperatura();  // Debe llamarse en cada iteración del loop()

// -----------------------------------------------------------------------------
// Serialización CSV — cada módulo se encarga de sus propias columnas
// -----------------------------------------------------------------------------
// Escriben en 'dst' (como mucho 'espacio' bytes, '\0' incluido) y devuelven los
// caracteres escritos. Sin lectura fiable, escribirDatosTemp deja las columnas vacias.
size_t escribirCabeceraTemp(char* dst, size_t espacio);
size_t escribirDatosTemp(char* dst, size_t espacio);

#endif // TEMPERATURE_SENSOR_H
