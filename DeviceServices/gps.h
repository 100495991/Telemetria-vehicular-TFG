#ifndef GPS_H
#define GPS_H

// =============================================================================
// gps.h  —  Declaraciones del módulo GPS
//
// Define la estructura InfoGPS (equivalente al dict "info" de Python)
// y los prototipos de todas las funciones de gestión del GPS.
// =============================================================================

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include "config.h"

// Ajuste horario para convertir la hora UTC del GPS a hora real de Madrid.
// OJO: es un offset fijo, no calcula el cambio de horario solo -> hay que
// tocarlo a mano dos veces al año (2 = verano/CEST, 1 = invierno/CET).
#define TIMEZONE_OFFSET_HOURS 2

// TinyGPS++ NUNCA vuelve a poner isValid() a false por si solo: location.commit()
// (que marca valid=true y guarda lat/lng) solo se llama cuando la trama trae fix,
// asi que si se pierde el fix, isValid() se queda en true para siempre con el
// ULTIMO valor bueno. Por eso la frescura del dato se comprueba con age() (ms
// desde el ultimo commit) en vez de isValid() a secas.
#define GPS_MAX_EDAD_FIX_MS 3000UL

// -----------------------------------------------------------------------------
// Estructura de datos  —  equivalente al diccionario "info" de Python
// Mismos campos que tenías: GPRMC + GPGGA + GPGSA
// -----------------------------------------------------------------------------
struct InfoGPS {
    // --- Timestamp ---
    // Hora LOCAL de Madrid (GPS UTC + TIMEZONE_OFFSET_HOURS), no UTC.
    char   timestamp_iso[20];  // "YYYY-MM-DDThh:mm:ss" — calculado una vez en actualizarGPS()
    int    anyo;
    int    mes;
    int    dia;
    int    hora;
    int    minuto;
    int    segundo;
    int    centesimas;

    // --- De GPRMC ---
    char   status;                   // 'A' = válido, 'V' = warning
    double latitud;
    char   latitud_hemisferio;       // 'N' / 'S'
    double longitud;
    char   longitud_hemisferio;      // 'E' / 'W'
    double velocidad_kmh;
    double rumbo_grados;
    double variacion_magnetica;
    char   variacion_magnetica_dir;

    // --- De GPGGA ---
    int    fix_quality;              // 0=sin fix, 1=GPS, 2=DGPS
    int    n_satelites_en_uso;
    double altitud_m;
    double separacion_geoide;

    // --- HDOP / VDOP ---
    double hdop;
    double vdop;

    // --- Control interno ---
    bool   valido;
};

// -----------------------------------------------------------------------------
// Variable global compartida con sd_manager.cpp
// Se define UNA SOLA VEZ en gps.cpp; aquí solo se declara como extern
// -----------------------------------------------------------------------------
extern InfoGPS    info_gps;
extern TinyGPSPlus gps;

// -----------------------------------------------------------------------------
// Prototipos de funciones
// -----------------------------------------------------------------------------
void        setupGPS();
void        leerGPS();
void        actualizarGPS();
void        inicializarInfoGPS();
void        imprimirInfo();
const char* hdopCalidad(double hdop);
const char* fixQualityDesc(int fq);

// -----------------------------------------------------------------------------
// Serialización CSV — cada módulo se encarga de sus propias columnas
// -----------------------------------------------------------------------------
// Escriben en 'dst' (como mucho 'espacio' bytes, '\0' incluido) y devuelven los
// caracteres escritos. Sin fix valido, escribirDatosGPS deja las columnas vacias.
size_t escribirCabeceraGPS(char* dst, size_t espacio);
size_t escribirDatosGPS(char* dst, size_t espacio);

#endif // GPS_H
