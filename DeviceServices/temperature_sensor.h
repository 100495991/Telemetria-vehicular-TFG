#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

// =============================================================================
// temperature_sensor.h  —  Declaraciones del módulo de temperatura IR MLX90614
// =============================================================================

#include <Arduino.h>
#include <Adafruit_MLX90614.h>
#include <Wire.h>

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
void leerTemperatura();  // Debe llamarse en cada iteración del loop()
size_t escribirCabeceraTemp(char* dst, size_t espacio);
size_t escribirDatosTemp(char* dst, size_t espacio);

#endif // TEMPERATURE_SENSOR_H
