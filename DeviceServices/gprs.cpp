// Gestion de conexion GSM/GPRS y MQTT usando TinyGSM + PubSubClient
// Secuencia: init modulo (ATZ;E0;CMEE;GSV;CSQ;CPIN;CCID) -> espera CREG OK
//            -> configurar modos de socket -> CSTT/CIICR/CIFSR (GPRS a mano)
//            -> abrir socket TCP -> MQTT connect -> publicacion periodica
//
// Por que "a mano" y no con modem.gprsConnect()/mqtt.connect() directos:
// esas llamadas de alto nivel son sincronas y, si la red falla, pueden
// bloquear el Arduino durante MAS DE UN MINUTO de un tiron (son varias
// llamadas AT encadenadas, cada una con su propio timeout largo). Como el
// Mega solo tiene un loop() y ningun otro modulo (GPS, acelerometro,
// temperatura, SD, pantalla) puede avanzar mientras tanto, aqui se
// reconstruye la misma conexion paso a paso: se manda un comando AT y se
// vuelve enseguida al loop(); la respuesta se sondea en sondeos cortos
// (TIMEOUT_ATCORTO) en las siguientes vueltas, hasta que llega o vence el
// plazo total de ese paso. Los dos unicos puntos que siguen siendo una
// llamada de libreria "de una pieza" (abrir el socket TCP y el handshake
// MQTT) llevan un timeout explicito corto en vez del que traen por defecto.
//
// Recuperacion automatica: TODO fallo (tanto durante la activacion como ya
// en GPRS_MQTT_ACTIVO: MQTT perdido, cobertura perdida, sesion zombi) pasa
// por el mismo punto unico, fallarConexionDatos(), que cierra socket/MQTT y
// hace que la maquina de estados vuelva a GPRS_ESPERA_RED (o, tras varios
// fallos seguidos, a GPRS_ENVIAR_INIT). No hay ningun estado de "conexion
// perdida permanentemente": mientras se siga llamando a gestionarGPRS() en
// cada loop(), el modulo reintentara solo cuando el SIM800L se apague y se
// vuelva a encender, o cuando recupere cobertura.

#include "gprs.h"

// -----------------------------------------------
// Objetos TinyGSM / PubSubClient
TinyGsm       modem(SERIAL_GPRS);
TinyGsmClient gsmClient(modem);
PubSubClient  mqtt(gsmClient);

// -----------------------------------------------
// Estado interno del modulo (no se expone en el .h)
static EstadoGPRS estadoGPRS = GPRS_ENVIAR_INIT;
static unsigned long tEstado            = 0;
static unsigned long tUltimaPublicacion  = 0;
static unsigned long tUltimaCompRed      = 0;   // Ultima vez que se reviso CREG ya estando conectados
static unsigned long tUltimoEnvioOk      = 0;   // Ultima vez que un mqtt.publish() confirmo exito (no solo connected())

// Fallos seguidos activando el GPRS o el MQTT (CSTT/CIICR/CIFSR/socket/mqtt).
// Si se acumulan demasiados, probablemente el modem se ha quedado en un
// estado raro y hace falta repetir la secuencia de init desde cero.
static uint8_t contadorFallosGPRS = 0;

InfoGPRS infoGPRS = { false, 99, 0 };

// -----------------------------------------------
// Sondea, con un timeout MUY corto, si el ultimo comando AT enviado ya tiene
// respuesta. Como el timeout es corto, esta llamada nunca bloquea el resto
// del sistema mas de TIMEOUT_ATCORTO ms: si el modem tarda mas en responder,
// toca volver a llamarla en la siguiente vuelta de loop().
// Devuelve 1 = OK, 2 = ERROR, 0 = todavia sin respuesta.
static int8_t sondearRespuestaAT() {
    return modem.waitResponse(TIMEOUT_ATCORTO);
}

// -----------------------------------------------
// Punto UNICO de "reconectar desde cero": aqui se llega tanto si falla
// cualquier paso de la activacion de datos/MQTT como si se cae la conexion
// ya estando en GPRS_MQTT_ACTIVO (MQTT perdido, cobertura perdida, sesion
// zombi). Siempre cierra socket TCP y sesion MQTT antes de reintentar: sin
// eso, el siguiente intento de abrir el socket puede chocar con uno que el
// modem todavia cree abierto (CIPSTART sobre un mux ya en uso falla en vez
// de abrir uno nuevo). mqtt.disconnect()/gsmClient.stop() son seguros de
// llamar aunque ya estuvieran cerrados.
// Ademas cuenta el fallo y decide si basta con volver a comprobar la
// cobertura (GPRS_ESPERA_RED) o si, tras demasiados fallos seguidos, hace
// falta reiniciar la secuencia de init completa (por si el modem se ha
// quedado en un estado del que solo se sale con un ATZ/&F).
// Plantilla para aceptar tanto literales F("...") (se quedan en flash) como
// char* construidos en tiempo de ejecucion (p.ej. con snprintf), igual que
// las dos sobrecargas de logEvento().
template <typename T>
static void fallarConexionDatos(T motivo, unsigned long ahora) {
    logEvento(motivo);

    mqtt.disconnect();
    gsmClient.stop();
    infoGPRS.estado = false;
    infoGPRS.rssi = 0;

    contadorFallosGPRS++;
    if (contadorFallosGPRS >= MAX_REINTENTOS_GPRS) {
        logEvento(F("GSM: Demasiados fallos seguidos activando datos/MQTT, reiniciando modulo"));
        contadorFallosGPRS = 0;
        estadoGPRS = GPRS_ENVIAR_INIT;
    } else {
        tEstado = ahora - TIMEOUT_CREG;   // fuerza una comprobacion de CREG inmediata
        estadoGPRS = GPRS_ESPERA_RED;
    }
}


int rssiPorcentaje() {
    if (infoGPRS.rssi < 0 || infoGPRS.rssi > 31) return 0;
    return (infoGPRS.rssi * 100) / 31;
}

// -----------------------------------------------
// Replica el mismo calculo que PubSubClient::publish() hace por dentro
// (MQTT_MAX_HEADER_SIZE + 2 bytes de longitud del topic + topic + payload)
// para saber ANTES de llamar a publish() si la trama cabe en el buffer.
// Sin esto, un payload demasiado grande y un fallo real de red/desconexion
// se ven identicos desde fuera: publish() devuelve false en ambos casos.
static bool cabeEnBufferMqtt(const char* topic, const char* payload) {
    size_t necesario = MQTT_MAX_HEADER_SIZE + 2 + strlen(topic) + strlen(payload);
    if (necesario <= mqtt.getBufferSize()) return true;

    char msg[64];
    snprintf(msg, sizeof(msg), "MQTT: trama de %u bytes no cabe en el buffer de %u",
             (unsigned)necesario, mqtt.getBufferSize());
    logEvento(msg);
    return false;
}

// -----------------------------------------------
// Refresco de la calidad de señal (AT+CSQ). Es un comando bloqueante, asi que
// NO se lanza en cada vuelta del loop: se limita a uno cada INTERVALO_RSSI ms.
// AT+CSQ devuelve 99 cuando la señal es desconocida, y TinyGSM devuelve tambien
// 99 si el comando no obtiene respuesta: en ambos casos no hay enlace util, se
// refleja como 0.
static void refrescarRSSI() {
    static unsigned long tUltimoRSSI = 0;
    if (millis() - tUltimoRSSI < INTERVALO_RSSI) return;
    tUltimoRSSI = millis();

    int8_t csq = modem.getSignalQuality();
    infoGPRS.rssi = (csq >= 0 && csq <= 31) ? csq : 0;
}

// =============================================================================
// SERIALIZACION CSV — columnas propias del enlace
// =============================================================================
static const char CABECERA_GPRS[] PROGMEM =
    "gprs_conectado,gprs_rssi_pct,gprs_paquetes_enviados";

size_t escribirCabeceraGPRS(char* dst, size_t espacio) {
    return csvCopiarCabecera(dst, espacio, CABECERA_GPRS);
}

size_t escribirDatosGPRS(char* dst, size_t espacio) {
    // Las tres columnas son siempre conocidas: sin cobertura el RSSI vale 0,
    // que en la escala CSQ ya significa "señal nula", no "dato ausente".
    return csvEscritos(snprintf(dst, espacio, "%d,%d,%lu",
        infoGPRS.estado ? 1 : 0,
        rssiPorcentaje(),
        infoGPRS.paquetesEnviados), espacio);
}


// -----------------------------------------------
void setupGPRS() {
    SERIAL_GPRS.begin(BAUD_GPRS);
    delay(200);
    estadoGPRS = GPRS_ENVIAR_INIT;

    // OJO: el #define MQTT_MAX_PACKET_SIZE de gprs.h NO llega a PubSubClient.cpp
    // (se compila como unidad independiente y se queda con su default de 256
    // bytes). La cabecera completa supera ese tamaño, asi que el buffer hay que
    // ampliarlo en tiempo de ejecucion o la publicacion se descarta en silencio.
    if (!mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE)) {
        logEvento(F("MQTT: ERROR - Sin memoria para ampliar el buffer de publicacion"));
    }

    mqtt.setServer(MQTT_BROKER, MQTT_PORT);

    // Por defecto PubSubClient espera hasta 15s el CONNACK del broker antes
    // de rendirse. Se acorta para que, si el broker no contesta, el bloqueo
    // sea breve y predecible en vez de dejar el resto del sistema colgado.
    mqtt.setSocketTimeout(TIMEOUT_MQTT_S);

    logEvento(F("GSM: Iniciando gestor GPRS/MQTT"));
}

// -----------------------------------------------
// Llamar en cada loop(). No bloquea de forma indefinida: cada paso tiene
// timeout corto y el estado se retoma en la siguiente vuelta si falla.
void gestionarGPRS() {
    unsigned long ahora = millis();

    switch (estadoGPRS) {

        // ---- Paso 1: secuencia de inicializacion compuesta, igual que hacias a mano ----
        //
        // OJO: aqui solo van comandos "esenciales" (reset, eco, errores
        // verbosos, config de fabrica y PIN). +GSV/+CSQ/+GSN (fabricante,
        // señal, IMEI) se han quitado a proposito: ninguno se llega a leer
        // en el resto del codigo (el RSSI se refresca aparte, en
        // refrescarRSSI()), y sus respuestas alargan tanto la contestacion
        // del modem que se sale de los 10 ms de TIMEOUT_ATCORTO. Como
        // waitResponse() no recuerda nada entre sondeos, si el "OK" final
        // queda partido justo en la frontera entre dos sondeos (visto en
        // debug: una "O" suelta en un sondeo, la "K" ya en el siguiente,
        // sin la "O" con la que emparejarla) el match nunca se completa y
        // GPRS_ESPERA_INIT se queda reintentando para siempre aunque el
        // modem SI este contestando.
        case GPRS_ENVIAR_INIT: {

            char cmd[32];
            if (strlen(PIN_SIM) > 0) {
                snprintf(cmd, sizeof(cmd), "Z;E0;+CMEE=1;&F;+CPIN=%s;", PIN_SIM);
            } else {
                snprintf(cmd, sizeof(cmd), "Z;E0;+CMEE=1;&F;");
            }

            modem.sendAT(cmd);
            tEstado = ahora;
            estadoGPRS = GPRS_ESPERA_INIT;
            break;
        }

        case GPRS_ESPERA_INIT: {

            // La sobrecarga waitResponse(timeout, String&) vuelca en 'respuesta'
            // todo lo que envia el modulo. Un waitResponse() a secas
            // consumiria el stream sin dejarlo ver. Timeout CORTO a
            // proposito: si el modem no ha contestado todavia, se vuelve al
            // loop() y se sigue sondeando en la siguiente vuelta, en vez de
            // quedarse aqui esperando de un tiron.
            String respuesta;
            int8_t res = modem.waitResponse(TIMEOUT_ATCORTO, respuesta);

            if (res == 1) {
                logEvento(F("GSM: Secuencia de init OK"));
                contadorFallosGPRS = 0;
                tEstado = ahora - TIMEOUT_CREG;   // fuerza la primera comprobacion de CREG ya
                estadoGPRS = GPRS_ESPERA_RED;
            } else if (ahora - tEstado > TIMEOUT_INIT) {
                // Sin respuesta tras varios sondeos cortos: puede que el modem
                // este apagado o reiniciandose. Se reintenta la secuencia de
                // init indefinidamente hasta que vuelva a contestar.
                logEvento(F("GSM: Sin respuesta a init, reintentando secuencia"));
                estadoGPRS = GPRS_ENVIAR_INIT;
            }
            // Si no ha llegado respuesta y aun no vence el plazo, se mantiene
            // el estado: se volvera a sondear en la siguiente vuelta de loop().
            break;
        }

        // ---- Paso 2: comprobar CREG periodicamente hasta tener cobertura aceptada ----
        case GPRS_ESPERA_RED: {
            // AT+CREG? tambien es un comando bloqueante (aunque corto): para
            // no mandarlo en cada vuelta de loop() se limita a una vez cada
            // TIMEOUT_CREG ms, igual que se hace con el RSSI en refrescarRSSI().
            if (ahora - tEstado < TIMEOUT_CREG) break;
            tEstado = ahora;

            // isNetworkConnected() internamente manda AT+CREG? y AT+CGREG?
            if (modem.isNetworkConnected()) {
                logEvento(F("GSM: Red registrada (CREG OK)"));
                refrescarRSSI();  // ya hay cobertura: se puede leer el CSQ
                estadoGPRS = GPRS_ENVIAR_CIPSHUT;
            } else {
                // Sin cobertura o pendiente/rechazado -> se sigue sondeando cada
                // TIMEOUT_CREG ms, sin limite de reintentos ni reinicio de init:
                // un rechazo de red (CREG denegado) se resuelve solo del lado
                // del operador, no reiniciando el modem.
                logEvento(F("GSM: Sin cobertura aun, reintentando CREG..."));
            }
            break;
        }

        // ---- Paso 2.5: cerrar cualquier contexto GPRS/IP que haya quedado a
        //      medias de un intento anterior (socket caido, MQTT perdido,
        //      etc.). Sin esto, el +CSTT del paso 4 falla al instante porque
        //      el modem cree que el contexto ya esta activo -- es justo lo
        //      que causaba el bucle de "Fallo configurando el APN" seguido.
        //      Seguro de llamar aunque no haya nada que cerrar: el modem
        //      contesta "SHUT OK" enseguida en ese caso tambien. ----
        case GPRS_ENVIAR_CIPSHUT: {
            logEvento(F("GSM: Cerrando contexto GPRS previo (CIPSHUT)..."));
            modem.sendAT(GF("+CIPSHUT"));
            tEstado = ahora;
            estadoGPRS = GPRS_ESPERA_CIPSHUT;
            break;
        }

        case GPRS_ESPERA_CIPSHUT: {
            // +CIPSHUT no responde OK/ERROR: contesta "SHUT OK" como texto
            // suelto, igual que +CIFSR devuelve la IP. Se sondea con la
            // variante que vuelca la respuesta cruda.
            String respuesta;
            modem.waitResponse(TIMEOUT_ATCORTO, respuesta);

            if (respuesta.indexOf(F("SHUT OK")) >= 0) {
                estadoGPRS = GPRS_CONFIGURAR_MODOS;
            } else if (ahora - tEstado > TIMEOUT_CIPSHUT) {
                // Best-effort: si ni siquiera CIPSHUT contesta, el modem esta
                // en un estado raro de verdad, pero no tiene sentido
                // bloquear aqui para siempre. Se continua igual: si el
                // contexto seguia sucio, el CSTT que viene despues fallara
                // y activara la recuperacion normal (fallarConexionDatos).
                logEvento(F("GSM: CIPSHUT sin respuesta, se continua igualmente"));
                estadoGPRS = GPRS_CONFIGURAR_MODOS;
            }
            break;
        }

        // ---- Paso 3: ajustes de socket, necesarios para que TinyGSM pueda
        //      despues leer/escribir con +CIPSEND/+CIPRXGET en modo
        //      multi-conexion. Son comandos locales al modulo (no dependen de
        //      la red) y responden casi al instante, asi que aqui si se
        //      esperan en el sitio: como mucho unos ~30ms en total (tres
        //      sondeos de TIMEOUT_ATCORTO), una sola vez por intento de
        //      conexion, nada comparable a los mas de 60s que podia costar
        //      antes el gprsConnect() de la libreria de un tiron ----
        case GPRS_CONFIGURAR_MODOS: {
            modem.sendAT(GF("+CIPMUX=1"));    // modo multi-conexion (CIPSTART con mux)
            modem.waitResponse(TIMEOUT_ATCORTO);
            modem.sendAT(GF("+CIPQSEND=1"));  // modo de envio rapido (sin "SEND OK" extra)
            modem.waitResponse(TIMEOUT_ATCORTO);
            modem.sendAT(GF("+CIPRXGET=1"));  // lectura manual de datos entrantes
            modem.waitResponse(TIMEOUT_ATCORTO);

            estadoGPRS = GPRS_ENVIAR_CSTT;
            break;
        }

        // ---- Paso 4: activar el contexto de datos GPRS, a mano y por partes ----
        case GPRS_ENVIAR_CSTT: {
            logEvento(F("GSM: Configurando APN..."));
            modem.sendAT(GF("+CSTT=\""), APN, GF("\",\""), GPRS_USER, GF("\",\""), GPRS_PASS, GF("\""));
            tEstado = ahora;
            estadoGPRS = GPRS_ESPERA_CSTT;
            break;
        }

        case GPRS_ESPERA_CSTT: {
            int8_t r = sondearRespuestaAT();
            if (r == 1) {
                estadoGPRS = GPRS_ENVIAR_CIICR;
            } else if (r == 2 || ahora - tEstado > TIMEOUT_CSTT) {
                fallarConexionDatos(F("GSM: Fallo configurando el APN (+CSTT)"), ahora);
            }
            // r == 0 y plazo no vencido: se sigue sondeando la siguiente vuelta
            break;
        }

        case GPRS_ENVIAR_CIICR: {
            logEvento(F("GSM: Activando el enlace de radio (CIICR)..."));
            modem.sendAT(GF("+CIICR"));
            tEstado = ahora;
            estadoGPRS = GPRS_ESPERA_CIICR;
            break;
        }

        case GPRS_ESPERA_CIICR: {
            // Este es el paso que de verdad puede tardar (el modem esta
            // estableciendo el enlace de datos con la torre): por eso
            // TIMEOUT_CIICR es mucho mayor que el resto, aunque cada sondeo
            // individual siga acotado a TIMEOUT_ATCORTO.
            int8_t r = sondearRespuestaAT();
            if (r == 1) {
                estadoGPRS = GPRS_ENVIAR_CIFSR;
            } else if (r == 2 || ahora - tEstado > TIMEOUT_CIICR) {
                fallarConexionDatos(F("GSM: Fallo activando el enlace de datos (+CIICR)"), ahora);
            }
            break;
        }

        case GPRS_ENVIAR_CIFSR: {
            modem.sendAT(GF("+CIFSR;E0"));
            tEstado = ahora;
            estadoGPRS = GPRS_ESPERA_CIFSR;
            break;
        }

        case GPRS_ESPERA_CIFSR: {
            // +CIFSR no responde OK/ERROR: devuelve la IP asignada como texto
            // suelto. Por eso se usa la variante con 'respuesta' y se mira si
            // ha llegado algo, igual que en GPRS_ESPERA_INIT.
            String respuesta;
            modem.waitResponse(TIMEOUT_ATCORTO, respuesta);
            respuesta.trim();

            if (respuesta.length() > 0) {
                char msg[40];
                snprintf(msg, sizeof(msg), "GSM: GPRS conectado. IP=%s", respuesta.c_str());
                logEvento(msg);
                estadoGPRS = GPRS_CONECTAR_SOCKET;
            } else if (ahora - tEstado > TIMEOUT_CIFSR) {
                fallarConexionDatos(F("GSM: Fallo obteniendo IP (+CIFSR)"), ahora);
            }
            break;
        }

        // ---- Paso 5: abrir el socket TCP hacia el broker ----
        case GPRS_CONECTAR_SOCKET: {
            logEvento(F("GSM: Abriendo socket TCP hacia el broker MQTT..."));
            // Timeout explicito y corto: sin el, TinyGSM esperaria hasta 75s
            // de un tiron si el broker no contesta.
            if (gsmClient.connect(MQTT_BROKER, MQTT_PORT, TIMEOUT_SOCKET_S)) {
                estadoGPRS = GPRS_CONECTAR_MQTT;
            } else {
                fallarConexionDatos(F("GSM: No se pudo abrir el socket hacia el broker"), ahora);
            }
            break;
        }

        // ---- Paso 6: handshake MQTT sobre el socket ya abierto ----
        case GPRS_CONECTAR_MQTT: {
            if (!gsmClient.connected()) {
                // El socket se ha caido justo antes de llegar aqui: se reabre.
                estadoGPRS = GPRS_CONECTAR_SOCKET;
                break;
            }

            logEvento(F("MQTT: Conectando al broker..."));
            // Como el socket ya esta abierto, PubSubClient no vuelve a tocar
            // la red para conectar: solo espera el CONNACK, acotado por el
            // mqtt.setSocketTimeout(TIMEOUT_MQTT_S) de setupGPRS().
            if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
                logEvento(F("MQTT: Conectado correctamente"));
                tUltimaPublicacion = ahora;
                tUltimaCompRed = ahora;
                tUltimoEnvioOk = ahora;
                contadorFallosGPRS = 0;

                infoGPRS.estado = true;
                const char* cabecera = construirCabeceraCSV();
                if (cabeEnBufferMqtt(MQTT_TOPIC_CABECERA, cabecera) &&
                    !mqtt.publish(MQTT_TOPIC_CABECERA, cabecera, true)) {
                    logEvento(F("MQTT: AVISO - No se pudo publicar la cabecera"));
                }

                estadoGPRS = GPRS_MQTT_ACTIVO;
            } else {
                char msg[40];
                snprintf(msg, sizeof(msg), "MQTT: Fallo de conexion (rc=%d)", mqtt.state());
                fallarConexionDatos(msg, ahora);
            }
            break;
        }

        // ---- Estado estable: publicar periodicamente y vigilar la conexion ----
        case GPRS_MQTT_ACTIVO: {
            mqtt.loop();  // mantiene vivo el cliente MQTT (PINGREQ, etc.) - llamada ligera, no bloqueante

            refrescarRSSI();  // actualiza la señal cada INTERVALO_RSSI ms

            if (!mqtt.connected()) {
                fallarConexionDatos(F("MQTT: Conexion perdida, reconectando"), ahora);
                break;
            }

            // Aunque PubSubClient siga viendo la conexion "viva" (todavia no
            // ha hecho falta leer/escribir en el socket ni ha fallado un
            // PINGREQ), se comprueba de vez en cuando que la red movil sigue
            // registrada: es la unica forma de enterarse de una perdida de
            // cobertura si en ese rato no ha habido trafico MQTT.
            if (ahora - tUltimaCompRed >= INTERVALO_COMPROBAR_RED) {
                tUltimaCompRed = ahora;
                if (!modem.isNetworkConnected()) {
                    fallarConexionDatos(F("GSM: Se perdio la cobertura, reconectando desde cero"), ahora);
                    break;
                }
            }

            if (ahora - tUltimaPublicacion >= INTERVALO_PUBLICACION) {
                // ---- Por ahora: mensaje estatico. Mas adelante: buildDataString() ----
                const char* trama = construirLineaCSV();

                if (cabeEnBufferMqtt(MQTT_TOPIC, trama)) {
                    if (mqtt.publish(MQTT_TOPIC, trama)) {
                        infoGPRS.paquetesEnviados++;
                        tUltimoEnvioOk = ahora;
                    } else {
                        logEvento(F("MQTT: Fallo al publicar (desconexion)"));
                    }
                }
                tUltimaPublicacion = ahora;
            }

            // Vigilante de sesion "zombi": mqtt.connected() es un flag local
            // y puede seguir dando "true" aunque la red movil haya cortado
            // el socket por su cuenta (NAT/firewall del operador). Si pasa
            // TIMEOUT_SIN_PUBLICAR_OK sin un publish CONFIRMADO, no nos
            // fiamos mas de connected(): se reconecta desde cero.
            if (ahora - tUltimoEnvioOk > TIMEOUT_SIN_PUBLICAR_OK) {
                fallarConexionDatos(F("MQTT: Demasiado tiempo sin publicar con exito pese a 'conectado', se fuerza reconexion"), ahora);
                break;
            }
            break;
        }
    }
}