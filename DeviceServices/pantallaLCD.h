#ifndef PANTALLALCD_H
#define PANTALLALCD_H

// =============================================================================
// pantallaLCD.h  —  Declaraciones del módulo de pantalla TFT ILI9486/ILI9488
// (MCUFRIEND_kbv + Adafruit_GFX) con menú táctil de 5 páginas.
//
// Todas las funciones de dibujo de cada página (estructura fija + valores que
// cambian) son `static` dentro de pantallaLCD.cpp: viven aisladas unas de
// otras, cada una con su propio estado (variables "ant_*" como `static`
// locales). Para editar la cuadrícula de una página basta con tocar su
// bloque "PAGINA ..." en el .cpp; no hay variables compartidas entre páginas
// que puedan romperse al modificar una de ellas.
// =============================================================================

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include "config.h"

// -----------------------------------------------------------------------------
// Páginas del menú
// -----------------------------------------------------------------------------
enum Pagina { PAG_HOME = 0, PAG_GPS, PAG_DIN, PAG_MOTOR, PAG_COMMS };

// -----------------------------------------------------------------------------
// Variables globales compartidas — definidas UNA SOLA VEZ en pantallaLCD.cpp
// -----------------------------------------------------------------------------
extern MCUFRIEND_kbv tft;
extern Pagina        paginaActual;

// -----------------------------------------------------------------------------
// Prototipos de funciones — API pública usada desde Telemetria-Vehicular.ino
// -----------------------------------------------------------------------------
void setupPantalla();     // Inicializa TFT + táctil y dibuja la página inicial
void actualizarPantalla(); // Llamar en cada iteración del loop()

#endif // PANTALLALCD_H
