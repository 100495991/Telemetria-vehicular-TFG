// =============================================================================
// gps.cpp  —  Implementación del módulo GPS
// =============================================================================

#include "gps.h"
#include "sd_manager.h"
#include <SparkFun_u-blox_GNSS_Arduino_Library.h> // solo hace falta aqui: gpsConfig es local a setupGPS()

// Convierte una macro numerica (p.ej. BAUD_GPS) en su literal de texto EN
// TIEMPO DE COMPILACION, para poder incrustarla dentro de un F("...") sin
// gastar RAM ni snprintf en tiempo de ejecucion. Truco de dos pasos: hace
// falta el nivel intermedio (_GPS_STR_HELPER) para que el preprocesador
// expanda BAUD_GPS a "115200" ANTES de aplicarle el operador #; si se
// aplicase # directamente sobre BAUD_GPS, el resultado seria el texto
// "BAUD_GPS", no "115200".
#define _GPS_STR_HELPER(x) #x
#define _GPS_STR(x) _GPS_STR_HELPER(x)


// -----------------------------------------------------------------------------
// Definición de las variables globales (declaradas como extern en gps.h)
// -----------------------------------------------------------------------------
TinyGPSPlus gps;
InfoGPS     info_gps;

// -----------------------------------------------------------------------------
// Inicializa el modulo GPS: baudrate, tasa de refresco y tramas NMEA
//
// gpsConfig es LOCAL a esta funcion (no un global como antes): solo hace
// falta para la configuracion inicial del modulo y nunca se vuelve a tocar
// desde el loop(). Como variable local, sus ~257 bytes de la clase (y los
// ~276 bytes que reserva en el heap con new[] dentro de begin()) se liberan
// solos al terminar setupGPS(), en vez de quedar reservados para siempre.
// -----------------------------------------------------------------------------
void setupGPS() {
    SFE_UBLOX_GNSS gpsConfig;

    // saveConfiguration() (mas abajo) graba el baudrate en la memoria NO
    // volatil del NEO-6M: sobrevive a resets y a nuevas subidas de sketch.
    // Asi que solo la PRIMERA vez el modulo esta a 9600 (de fabrica); en
    // cualquier ejecucion posterior ya arranca directamente a 115200. Si
    // aqui solo se probara 9600, esas veces gpsConfig.begin() fallaria,
    // haria return, y Serial1 se quedaria a 9600 mientras el modulo habla
    // de verdad a 115200: todo lo que llega se lee con framing incorrecto
    // (el modulo puede tener fix perfectamente -el LED parpadea- pero
    // TinyGPS++ nunca reconoce una trama NMEA valida). Por eso se prueba
    // primero a 9600 y, si no contesta, se reintenta a 115200 antes de
    // rendirse.
    SERIAL_GPS.begin(9600);
    if (gpsConfig.begin(SERIAL_GPS)) {
        // Primer arranque (o el modulo se reseteo a fabrica): lo pasamos a
        // 115200 y confirmamos que responde ya a esa velocidad.
        gpsConfig.setSerialRate(BAUD_GPS, COM_PORT_UART1);
        SERIAL_GPS.begin(BAUD_GPS);
        delay(100);
        if (!gpsConfig.begin(SERIAL_GPS)) {
            Serial.println(F("[GPS] ERROR - No respondio a " _GPS_STR(BAUD_GPS) " tras el cambio de baudrate"));
            return;
        }
    } else {
        // No contesto a 9600: probablemente ya esta a 115200 de una
        // ejecucion anterior (config guardada con saveConfiguration()).
        SERIAL_GPS.begin(BAUD_GPS);
        delay(100);
        if (!gpsConfig.begin(SERIAL_GPS)) {
            Serial.println(F("[GPS] ERROR - No se detecta ni a 9600 ni a " _GPS_STR(BAUD_GPS) " baud"));
            return;
        }
        Serial.println(F("[GPS] Modulo ya estaba a " _GPS_STR(BAUD_GPS) " baud (config de un arranque anterior)"));
    }

    gpsConfig.setNavigationFrequency(FREQ_GPS);
    // Desactivar tramas NMEA innecesarias (dejar solo GGA y RMC)
    gpsConfig.disableNMEAMessage(UBX_NMEA_GLL, COM_PORT_UART1);
    gpsConfig.disableNMEAMessage(UBX_NMEA_GSA, COM_PORT_UART1);
    gpsConfig.disableNMEAMessage(UBX_NMEA_GSV, COM_PORT_UART1);
    gpsConfig.disableNMEAMessage(UBX_NMEA_VTG, COM_PORT_UART1);

    gpsConfig.saveConfiguration();

    inicializarInfoGPS();

    Serial.println(F("[GPS] Configuracion aplicada: " _GPS_STR(BAUD_GPS) " baud, " _GPS_STR(FREQ_GPS) "Hz, GGA+RMC"));
}

// -----------------------------------------------------------------------------
// Debe llamarse en cada iteración del loop()
// Alimenta TinyGPS++ con los bytes que llegan por Serial1
// -----------------------------------------------------------------------------
void leerGPS() {
    while (Serial1.available() > 0) {
        gps.encode(Serial1.read());
    }
}

// -----------------------------------------------------------------------------
// Inicializa la estructura con valores por defecto
// -----------------------------------------------------------------------------
void inicializarInfoGPS() {
    snprintf(info_gps.timestamp_iso, sizeof(info_gps.timestamp_iso), "0000-00-00T00:00:00");
    info_gps.anyo                    = 0;
    info_gps.mes                     = 0;
    info_gps.dia                     = 0;
    info_gps.hora                    = 0;
    info_gps.minuto                  = 0;
    info_gps.segundo                 = 0;
    info_gps.centesimas              = 0;
    info_gps.status                  = 'V';
    info_gps.latitud                 = 0.0;
    info_gps.latitud_hemisferio      = '?';
    info_gps.longitud                = 0.0;
    info_gps.longitud_hemisferio     = '?';
    info_gps.velocidad_kmh           = 0.0;
    info_gps.rumbo_grados            = 0.0;
    info_gps.variacion_magnetica     = 0.0;
    info_gps.variacion_magnetica_dir = '?';
    info_gps.fix_quality             = 0;
    info_gps.n_satelites_en_uso      = 0;
    info_gps.altitud_m               = 0.0;
    info_gps.separacion_geoide       = 0.0;
    info_gps.hdop                    = 99.99;
    info_gps.vdop                    = 99.99;
    info_gps.valido                  = false;
}

// -----------------------------------------------------------------------------
// Vuelca los datos de TinyGPS++ a la estructura InfoGPS
// -----------------------------------------------------------------------------
void actualizarGPS() {

    // ── Timestamp local (Madrid) ─────────────────────────────────────────
    // El GPS entrega la hora en UTC; se le suma TIMEZONE_OFFSET_HOURS aqui
    // mismo para que tanto el CSV/MQTT como el LCD muestren la hora real del
    // coche, no UTC.
    if (gps.date.isValid() && gps.time.isValid()) {
        struct tm t = {};
        t.tm_year = gps.date.year() - 1900;
        t.tm_mon  = gps.date.month() - 1;
        t.tm_mday = gps.date.day();
        t.tm_hour = gps.time.hour() + TIMEZONE_OFFSET_HOURS;  // Ajuste horario local
        t.tm_min  = gps.time.minute();
        t.tm_sec  = gps.time.second();

        // Timestamp en formato ISO 8601 (hora LOCAL, pese al formato): YYYY-MM-DDThh:mm:ss
        snprintf(info_gps.timestamp_iso, sizeof(info_gps.timestamp_iso),
                 "%04d-%02d-%02dT%02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);

        info_gps.anyo    = gps.date.year();
        info_gps.mes     = gps.date.month();
        info_gps.dia     = gps.date.day();
        info_gps.hora       = gps.time.hour() + TIMEZONE_OFFSET_HOURS;
        info_gps.minuto     = gps.time.minute();
        info_gps.segundo    = gps.time.second();
        info_gps.centesimas = gps.time.centisecond();
    }

    // ── Fix fresco ────────────────────────────────────────────────────────
    // gps.location.isValid() a secas no sirve para detectar la PERDIDA de fix
    // (ver comentario de GPS_MAX_EDAD_FIX_MS en gps.h): se exige ademas que el
    // ultimo commit sea reciente.
    bool fixFresco = gps.location.isValid() && gps.location.age() < GPS_MAX_EDAD_FIX_MS;

    // ── Status A/V ────────────────────────────────────────────────────────
    info_gps.status = fixFresco ? 'A' : 'V';

    // ── Posición ──────────────────────────────────────────────────────────
    if (fixFresco) {
        info_gps.latitud             = gps.location.lat();
        info_gps.latitud_hemisferio  = (info_gps.latitud  >= 0) ? 'N' : 'S';
        info_gps.longitud            = gps.location.lng();
        info_gps.longitud_hemisferio = (info_gps.longitud >= 0) ? 'E' : 'W';
    }

    // ── Velocidad ─────────────────────────────────────────────────────────
    if (fixFresco && gps.speed.isValid() && gps.speed.age() < GPS_MAX_EDAD_FIX_MS) {
        info_gps.velocidad_kmh = gps.speed.kmph();
    }

    // ── Rumbo ─────────────────────────────────────────────────────────────
    if (fixFresco && gps.course.isValid() && gps.course.age() < GPS_MAX_EDAD_FIX_MS) {
        info_gps.rumbo_grados = gps.course.deg();
    }

    // ── Fix quality y satélites ───────────────────────────────────────────
    info_gps.fix_quality = fixFresco ? 1 : 0;
    if (gps.satellites.isValid()) {
        info_gps.n_satelites_en_uso = gps.satellites.value();
    }

    // ── Altitud ───────────────────────────────────────────────────────────
    if (fixFresco && gps.altitude.isValid() && gps.altitude.age() < GPS_MAX_EDAD_FIX_MS) {
        info_gps.altitud_m = gps.altitude.meters();
    }

    // ── HDOP ──────────────────────────────────────────────────────────────
    if (gps.hdop.isValid()) {
        info_gps.hdop = gps.hdop.hdop();
    }

    // ── Validez global ────────────────────────────────────────────────────
    info_gps.valido = fixFresco && gps.date.isValid();
}

// -----------------------------------------------------------------------------
// Devuelve la calidad del HDOP como string
// -----------------------------------------------------------------------------
const char* hdopCalidad(double hdop) {
    if (hdop < 1.0)  return "Ideal";
    if (hdop < 2.0)  return "Excelente";
    if (hdop < 5.0)  return "Bueno";
    if (hdop < 10.0) return "Moderado";
    if (hdop < 20.0) return "Malo";
    return "Muy malo";
}

// -----------------------------------------------------------------------------
// Devuelve la descripción del fix quality
// -----------------------------------------------------------------------------
const char* fixQualityDesc(int fq) {
    switch (fq) {
        case 0:  return "Sin fix";
        case 1:  return "GPS fix";
        case 2:  return "Differential GPS fix";
        default: return "Desconocido";
    }
}

// -----------------------------------------------------------------------------
// Cabecera CSV — columnas de este módulo, en el mismo orden que escribirDatosGPS
// -----------------------------------------------------------------------------
// La cabecera vive en PROGMEM: ademas de ahorrar SRAM, csvCamposVacios() la
// recorre para saber cuantas columnas hay que dejar en blanco cuando no hay fix.
static const char CABECERA_GPS[] PROGMEM =
    "timestamp_local,status,latitud,latitud_hemisferio,"
    "longitud,longitud_hemisferio,velocidad_kmh,rumbo_grados,"
    "fix_quality,fix_quality_desc,n_satelites_en_uso,"
    "altitud_m,hdop,hdop_calidad";

size_t escribirCabeceraGPS(char* dst, size_t espacio) {
    return csvCopiarCabecera(dst, espacio, CABECERA_GPS);
}

// -----------------------------------------------------------------------------
// Línea CSV — valores de InfoGPS, en el mismo orden que escribirCabeceraGPS
// -----------------------------------------------------------------------------
size_t escribirDatosGPS(char* dst, size_t espacio) {
    // Sin fix valido no hay ni posicion ni timestamp de confianza: se dejan
    // todas las columnas vacias EXCEPTO 'status' (columna 2), que se rellena
    // con 'V' para que quede explicito que no hay fix, en vez de reenviar en
    // bucle el ultimo valor bueno (que es lo que hacia info_gps.valido antes
    // de comprobar la frescura del dato con age(), ver GPS_MAX_EDAD_FIX_MS).
    if (!info_gps.valido) {
        return csvEscritos(snprintf(dst, espacio, ",%c,,,,,,,,,,,,",
            info_gps.status), espacio);
    }

    // Buffers para dtostrf (convierte double a string con decimales fijos)
    // width = 0 evita padding con espacios a la izquierda
    char lat_str[12],  lon_str[13];
    char vel_k_str[10];
    char rum_str[10],  alt_str[10];
    char hdop_str[8];

    dtostrf(info_gps.latitud,       0, 6, lat_str);
    dtostrf(info_gps.longitud,      0, 6, lon_str);
    dtostrf(info_gps.velocidad_kmh, 0, 2, vel_k_str);
    dtostrf(info_gps.rumbo_grados,  0, 2, rum_str);
    dtostrf(info_gps.altitud_m,     0, 1, alt_str);
    dtostrf(info_gps.hdop,          0, 2, hdop_str);

    return csvEscritos(snprintf(dst, espacio,
        "%s,%c,%s,%c,%s,%c,%s,%s,%d,%s,%d,%s,%s,%s",
        info_gps.timestamp_iso,
        info_gps.status,
        lat_str,
        info_gps.latitud_hemisferio,
        lon_str,
        info_gps.longitud_hemisferio,
        vel_k_str,
        rum_str,
        info_gps.fix_quality,
        fixQualityDesc(info_gps.fix_quality),
        info_gps.n_satelites_en_uso,
        alt_str,
        hdop_str,
        hdopCalidad(info_gps.hdop)
    ), espacio);
}

// -----------------------------------------------------------------------------
// Imprime toda la estructura por el monitor serie
// -----------------------------------------------------------------------------
void imprimirInfo() {
    Serial.println(F("┌─ INFO GPS ──────────────────────────────┐"));

    Serial.print(F("  timestamp_local      : ")); Serial.println(info_gps.timestamp_iso);
    Serial.print(F("  status               : ")); Serial.println(info_gps.status);
    Serial.print(F("  latitud              : ")); Serial.println(info_gps.latitud,  6);
    Serial.print(F("  latitud_hemisferio   : ")); Serial.println(info_gps.latitud_hemisferio);
    Serial.print(F("  longitud             : ")); Serial.println(info_gps.longitud, 6);
    Serial.print(F("  longitud_hemisferio  : ")); Serial.println(info_gps.longitud_hemisferio);
    Serial.print(F("  velocidad_kmh        : ")); Serial.println(info_gps.velocidad_kmh, 2);
    Serial.print(F("  rumbo_grados         : ")); Serial.println(info_gps.rumbo_grados,  2);
    Serial.print(F("  fix_quality          : ")); Serial.print(info_gps.fix_quality);
    Serial.print(F(" (")); Serial.print(fixQualityDesc(info_gps.fix_quality)); Serial.println(F(")"));
    Serial.print(F("  n_satelites_en_uso   : ")); Serial.println(info_gps.n_satelites_en_uso);
    Serial.print(F("  altitud_m            : ")); Serial.println(info_gps.altitud_m, 1);
    Serial.print(F("  hdop                 : ")); Serial.print(info_gps.hdop, 2);
    Serial.print(F(" (")); Serial.print(hdopCalidad(info_gps.hdop)); Serial.println(F(")"));
    Serial.println(F("└─────────────────────────────────────────┘\n"));
}