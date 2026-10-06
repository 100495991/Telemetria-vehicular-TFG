#ifndef GPRS_H
#define GPRS_H

// -----------------------------------------------
// Estos defines deben ir ANTES de incluir TinyGsmClient.h / PubSubClient.h
#define TINY_GSM_MODEM_SIM800      // Indicar a TinyGSM que el modem es un SIM800

// DEBUG: descomentar para volcar por Serial todo el trafico AT crudo (lo que
// se envia y lo que contesta el modem), usando el propio mecanismo de log de
// TinyGSM. Util para diagnosticar la maquina de estados de gestionarGPRS();
// dejarlo comentado en uso normal, consume flash extra y CPU en cada AT.
// #define TINY_GSM_DEBUG Serial

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include "config.h"
#include "sd_manager.h"
#include "secrets.h"

// Buffer de PubSubClient (por defecto son solo 256 bytes, insuficiente para
// la trama de telemetria completa). Atado a TRAMA_MAX (sd_manager.h) en vez
// de un numero suelto: si TRAMA_MAX crece al anadir un sensor, este buffer
// crece con el automaticamente, sin que se puedan desincronizar. El margen
// de 64 cubre el topic mas largo (MQTT_TOPIC_CABECERA) mas la cabecera MQTT.
#define MQTT_MAX_PACKET_SIZE (TRAMA_MAX + 64)



// -----------------------------------------------
// Parametros de conexion.
#define PIN_SIM        "0574" // "5550"

#define APN            "lowi.private.omv.ese.omv.es" //"orangeworld"
#define GPRS_USER      ""
#define GPRS_PASS      ""

#define MQTT_BROKER             "telemetria-vehicular.duckdns.org"   // p.ej. "tutfg.duckdns.org"
#define MQTT_PORT               1883
#define MQTT_TOPIC              "uc3m/tfg/telemetria"
#define MQTT_TOPIC_CABECERA     "uc3m/tfg/telemetria/cabecera"
#define MQTT_CLIENT_ID          "ArduinoTFG_UC3M"

#define INTERVALO_PUBLICACION 500UL
#define TIMEOUT_INIT          5000UL   // Ventana para reintentar la secuencia de init si el modulo no responde
#define TIMEOUT_CREG          3000UL   // Cada cuanto se sondea AT+CREG? mientras se espera cobertura
#define INTERVALO_RSSI        5000UL   // Refresco de AT+CSQ para pantalla/CSV

// -----------------------------------------------
// Activacion de datos GPRS/MQTT paso a paso.
// modem.gprsConnect() y mqtt.connect() "a pelo" son llamadas que pueden
// bloquear el programa durante MAS DE UN MINUTO de un tiron si la red falla
// (comprobado en el propio codigo de TinyGSM: varios waitResponse() internos
// de 60-85 segundos). Para que eso nunca congele el resto de modulos
// (GPS, acelerometro, temperatura, SD, pantalla...), aqui se reconstruye esa
// misma conexion a mano, comando AT a comando AT, y cada sondeo de respuesta
// se acota a TIMEOUT_ATCORTO: si el modem no ha contestado en ese ratito, se
// devuelve el control al loop() y se sigue sondeando en la siguiente vuelta.
//
// OJO con este valor: el puerto serie del GPS (y el del acelerometro) van a
// 115200 baudios con el buffer hardware de solo 64 bytes, que se llena en
// unos 5ms. Si TIMEOUT_ATCORTO fuera "grande" (p.ej. 300ms), mientras el
// GPRS esta conectando el loop() solo daria unas 3 vueltas por segundo y el
// GPS perderia tramas por buffer lleno. Por eso se deja lo mas pequeño que
// tiene sentido: apenas alcanza para ver si la respuesta YA esta en el
// buffer del modem, pero permite decenas de vueltas de loop() por segundo
// incluso mientras se esta estableciendo la conexion.
#define TIMEOUT_ATCORTO         10UL   // Tope de bloqueo de CADA sondeo individual de respuesta AT
#define TIMEOUT_CIPSHUT       65000UL  // Ventana total para +CIPSHUT (peor caso segun el manual AT del SIM800)
#define TIMEOUT_CSTT          8000UL   // Ventana total para +CSTT (fija el APN)
#define TIMEOUT_CIICR         20000UL  // Ventana total para +CIICR (trae el enlace de radio; el paso mas lento)
#define TIMEOUT_CIFSR          8000UL  // Ventana total para +CIFSR (asigna IP)

// Estos dos SI son llamadas unicas de la libreria (no se pueden trocear sin
// reescribir TinyGSM/PubSubClient por dentro), asi que en su lugar se les da
// un timeout explicito corto: como mucho unos segundos de bloqueo, una sola
// vez por intento de conexion, en vez de los 75s/15s por defecto.
#define TIMEOUT_SOCKET_S       8       // Timeout (segundos) para abrir el socket TCP hacia el broker
#define TIMEOUT_MQTT_S         5       // Timeout (segundos) que espera PubSubClient al CONNACK

#define INTERVALO_COMPROBAR_RED 15000UL // Ya conectados, cada cuanto se revisa que la red movil sigue registrada
#define MAX_REINTENTOS_GPRS    6        // Fallos seguidos activando datos/MQTT antes de reiniciar el modulo

// mqtt.connected() de PubSubClient es un flag LOCAL: si la red movil corta
// la sesion TCP por su cuenta (NAT/firewall del operador cerrando una
// conexion inactiva) el modem puede quedarse creyendo que el socket sigue
// abierto -- registrado en red, LEDs normales -- mientras publish() falla o
// se pierde sin que connected() lo note nunca. Por eso no basta con confiar
// en connected(): si pasa este tiempo sin conseguir un publish CONFIRMADO
// (ok == true) estando en GPRS_MQTT_ACTIVO, se fuerza cerrar el socket y
// reconectar desde cero, ignorando lo que diga connected().
#define TIMEOUT_SIN_PUBLICAR_OK 20000UL

// Con CIPQSEND=0, un mqtt.publish() que devuelve false es ya un fallo de
// entrega real (SEND FAIL), no un simple problema de encolado. No hace falta
// esperar a TIMEOUT_SIN_PUBLICAR_OK: tras este numero de fallos SEGUIDOS se
// fuerza la reconexion de inmediato, sin esperar el resto de la ventana.
#define MAX_FALLOS_PUBLICACION_SEGUIDOS 3

struct InfoGPRS {
    bool          estado;             // true = conectado al broker MQTT
    int           rssi;               // Calidad de señal (AT+CSQ): 0-31
    unsigned long paquetesEnviados;   // Contador de publicaciones MQTT correctas
};

extern InfoGPRS infoGPRS;   // definida en gprs.cpp

// -----------------------------------------------
// Maquina de estados
// Cada paso hace poco trabajo y vuelve enseguida: gestionarGPRS() esta
// pensada para llamarse una vez por loop() y nunca quedarse "atascada"
// dentro de un unico estado durante mucho tiempo.
enum EstadoGPRS {
  GPRS_ENVIAR_INIT,        // Manda la secuencia AT de init (ATZ, CMEE, PIN...)
  GPRS_ESPERA_INIT,        // Espera esa respuesta, en sondeos cortos
  GPRS_ESPERA_RED,         // Sondea AT+CREG? hasta tener cobertura
  GPRS_ENVIAR_CIPSHUT,     // Manda AT+CIPSHUT (cierra cualquier contexto GPRS/IP a medias de un intento anterior)
  GPRS_ESPERA_CIPSHUT,
  GPRS_CONFIGURAR_MODOS,   // Ajustes de socket (CIPMUX/CIPQSEND/CIPRXGET), locales e instantaneos
  GPRS_ENVIAR_CSTT,        // Manda AT+CSTT (APN/usuario/contrasena)
  GPRS_ESPERA_CSTT,
  GPRS_ENVIAR_CIICR,       // Manda AT+CIICR (activa el enlace de radio)
  GPRS_ESPERA_CIICR,
  GPRS_ENVIAR_CIFSR,       // Manda AT+CIFSR (pide la IP asignada)
  GPRS_ESPERA_CIFSR,
  GPRS_CONECTAR_SOCKET,    // Abre el socket TCP hacia el broker (timeout corto)
  GPRS_CONECTAR_MQTT,      // Handshake MQTT sobre ese socket (timeout corto)
  GPRS_MQTT_ACTIVO         // Conectado: publica periodicamente y vigila la conexion
};

// -----------------------------------------------
// Objetos globales TinyGSM / PubSubClient (definidos en gprs_manager.cpp)
extern TinyGsm       modem;
extern TinyGsmClient gsmClient;
extern PubSubClient  mqtt;

size_t escribirCabeceraGPRS(char* dst, size_t espacio);
size_t escribirDatosGPRS(char* dst, size_t espacio);

void setupGPRS();
void gestionarGPRS();
int  rssiPorcentaje();   // Convierte infoGPRS.rssi (0-31) a porcentaje 0-100

#endif // GPRS_H