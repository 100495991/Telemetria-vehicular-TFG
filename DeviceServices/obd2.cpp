// =============================================================================
// obd2.cpp  —  Implementación del módulo OBD-II (ELM327, vía ELMduino)
//
// El ELM327 se consulta con ELMduino en modo NO bloqueante: cada método (rpm(),
// kph(), etc.) manda el comando AT/PID una vez y hay que seguir llamandolo en
// las siguientes vueltas del loop() hasta que myELM327.nb_rx_state pasa a
// ELM_SUCCESS (o a un error). La libreria guarda ese progreso en una UNICA
// maquina de estados interna (nb_query_state), compartida por TODAS las
// llamadas: si se lanza una query de un PID distinto antes de que la anterior
// termine, se corta la que estaba en curso. Por eso aqui se consulta un solo
// PID a la vez, con una maquina de estados propia (EstadoObd2) que solo avanza
// al siguiente PID cuando el actual ha terminado (con exito o con error).
// =============================================================================

#include "obd2.h"
#include "sd_manager.h"

// -----------------------------------------------------------------------------
// Definición de las variables globales (declaradas como extern en el .h)
// -----------------------------------------------------------------------------
InfoObd2 info_obd2;
ELM327   myELM327;

// -----------------------------------------------------------------------------
// Un PID por vuelta de la maquina de estados, en este orden
// -----------------------------------------------------------------------------
enum EstadoObd2 {
    OBD_RPM,
    OBD_VELOCIDAD,
    OBD_CARGA_MOTOR,
    OBD_ACELERADOR,
    OBD_TEMP_REFRIGERANTE,
    OBD_TIEMPO_ARRANQUE,
    OBD_TASA_CONSUMO
};
static EstadoObd2 estadoObd2 = OBD_RPM;

// true solo si myELM327.begin() detecto el adaptador en setupObd2(). Si es
// false, leerObd2() no debe llamar a ningun metodo de ELMduino: sin un ELM327
// real al otro lado, esas llamadas se quedan esperando una respuesta que
// nunca llega y el estado (OBD_RPM, OBD_VELOCIDAD...) nunca avanza.
static bool obd2Disponible = false;

// -----------------------------------------------------------------------------
// Rangos de cordura: los limites que el estandar OBD-II puede llegar a
// codificar en los bytes de respuesta de cada PID. Fuera de estos margenes la
// lectura no puede venir de una trama bien formada, asi que solo puede ser una
// respuesta corrupta o mal interpretada.
// -----------------------------------------------------------------------------
static const float RPM_MAX          = 16383.75f;  // PID 0x0C: 2 bytes, factor 1/4
static const float VELOCIDAD_MAX    = 255.0f;      // PID 0x0D: 1 byte
static const float PORCENTAJE_MAX   = 100.0f;      // PID 0x04 / 0x11: 1 byte, factor 100/255
static const float TEMP_REFRIG_MIN  = -40.0f;      // PID 0x05: 1 byte, offset -40
static const float TEMP_REFRIG_MAX  = 215.0f;
static const float TASA_CONSUMO_MAX = 3212.75f;    // PID 0x5E: 2 bytes, factor 1/20

// Fallos seguidos antes de dar por perdida la comunicacion con el ELM327
static const uint8_t MAX_FALLOS_CONSECUTIVOS = 14;  // ~2 vueltas completas de los 7 PIDs
static uint8_t fallosConsecutivos = 0;

// -----------------------------------------------------------------------------
// true si la lectura no es NaN y cae dentro del rango que el PID puede codificar
// -----------------------------------------------------------------------------
static bool lecturaValida(float valor, float minimo, float maximo) {
    return !isnan(valor) && valor >= minimo && valor <= maximo;
}

// -----------------------------------------------------------------------------
// Inicializa la estructura con valores por defecto
// -----------------------------------------------------------------------------
void inicializarInfoObd2() {
    info_obd2.rpm                = 0.0f;
    info_obd2.velocidad_kmh      = 0.0f;
    info_obd2.carga_motor        = 0.0f;
    info_obd2.pos_acelerador     = 0.0f;
    info_obd2.temp_refrigerante  = 0.0f;
    info_obd2.tiempo_arranque_s  = 0;
    info_obd2.tasa_consumo_lh    = 0.0f;
    info_obd2.valido             = false;
}

// -----------------------------------------------------------------------------
// Sondeo rapido: comprueba si hay ALGO respondiendo por SERIAL_OBD antes de
// lanzar el handshake completo de myELM327.begin(). Ese handshake es
// bloqueante y, si el adaptador no esta conectado, puede tardar mas de 10 s
// en descartarlo (varios comandos de configuracion con timeout largo cada
// uno). Aqui basta mandar un AT cualquiera (ATI = version del ELM327) y ver
// si llega una sola respuesta en un plazo corto: si no hay NADA en la linea,
// no tiene sentido intentar el handshake completo.
// Devuelve true si se ha detectado alguna respuesta (aunque no sea "OK").
// -----------------------------------------------------------------------------
static const unsigned long TIMEOUT_SONDA_MS = 300;

static bool sondaRapidaELM327() {
    while (SERIAL_OBD.available()) SERIAL_OBD.read(); // limpia basura residual

    SERIAL_OBD.print(F("ATI\r"));

    unsigned long inicio = millis();
    while (millis() - inicio < TIMEOUT_SONDA_MS) {
        if (SERIAL_OBD.available()) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Intenta (re)conectar con el ELM327: sondeo rapido + handshake de ELMduino.
// Usada tanto en el arranque (setupObd2) como en los reintentos periodicos
// desde leerObd2() cuando el adaptador no estaba disponible. Dos llamadas
// bloqueantes cortas (~300 ms de sondeo + hasta 2 s de handshake si hay algo
// al otro lado), por eso quien la llama debe espaciar los intentos en el
// tiempo en vez de invocarla en cada vuelta del loop().
// -----------------------------------------------------------------------------
static bool intentarConectarObd2() {
    logEvento(F("[OBD2] Conectando con el ELM327..."));

    // Sondeo rapido (unos 300 ms como mucho): si no hay ni un adaptador
    // conectado a SERIAL_OBD, no tiene sentido esperar el handshake completo
    // de myELM327.begin(), que en el peor caso (protocolo AUTO + vehiculo sin
    // responder) puede bloquear varios segundos.
    if (!sondaRapidaELM327()) {
        logEvento(F("[OBD2] AVISO - ELM327 no responde por serie, se reintentara mas adelante"));
        return false;
    }

    // Vaciar lo que haya contestado el ATI del sondeo: myELM327.begin() manda
    // su propia secuencia de comandos y no espera encontrarse esa respuesta
    // suelta en el buffer.
    delay(20);
    while (SERIAL_OBD.available()) SERIAL_OBD.read();

    bool conectado = myELM327.begin(SERIAL_OBD, false, 2000);

    if (!conectado) {
        // Comprobar si el ELM327 esta presente pero no responde al protocolo OBD-II
        if (myELM327.nb_rx_state == ELM_UNABLE_TO_CONNECT) {
            logEvento(F("[OBD2] AVISO - ELM327 detectado pero sin conexion con el vehiculo, se reintentara mas adelante"));
        } else {
            logEvento(F("[OBD2] AVISO - ELM327 no responde por serie, se reintentara mas adelante"));
        }
        return false;
    }

    logEvento(F("[OBD2] ELM327 conectado correctamente"));
    return true;
}

// -----------------------------------------------------------------------------
// Inicializa el puerto serie y hace el primer intento de conexion con el ELM327
// -----------------------------------------------------------------------------
void setupObd2() {
    inicializarInfoObd2();

    SERIAL_OBD.begin(BAUD_OBD);

    obd2Disponible = intentarConectarObd2();
}

// -----------------------------------------------------------------------------
// Se llama tras un fallo (error del ELM327 o lectura fuera de rango). Cuenta
// los fallos seguidos y, al superar el limite, invalida la estructura para que
// el CSV deje de publicar los ultimos valores (que ya no son de fiar).
// -----------------------------------------------------------------------------
static void registrarFallo() {
    if (fallosConsecutivos < 255) fallosConsecutivos++;

    if (fallosConsecutivos == MAX_FALLOS_CONSECUTIVOS) {
        logEvento(F("[OBD2] AVISO - Sin datos fiables del ELM327, datos marcados como no validos"));
        info_obd2.valido = false;
    }
}

// -----------------------------------------------------------------------------
// Se llama tras una lectura valida. Da por buena la comunicacion y resetea el
// contador de fallos.
// -----------------------------------------------------------------------------
static void registrarExito() {
    fallosConsecutivos = 0;
    info_obd2.valido   = true;
}

// Cada cuanto se reintenta la conexion mientras el ELM327 no este disponible.
// intentarConectarObd2() es bloqueante (hasta ~2.3 s si no hay nada al otro
// lado), por eso no se prueba en cada vuelta del loop() sino cada pocos
// segundos: da margen a que aparezca el adaptador o el vehiculo se encienda
// sin congelar el resto de modulos (GPS, pantalla...) mientras tanto.
static const unsigned long INTERVALO_REINTENTO_MS = 5000;
static unsigned long ultimoIntentoReconexion = 0;

// -----------------------------------------------------------------------------
// Debe llamarse en cada iteración del loop().
// Consulta un PID por vuelta y avanza al siguiente cuando el actual responde
// (con exito o con error): nunca se lanza una query nueva antes de que la
// anterior haya terminado.
// Si el ELM327 no estaba disponible (fallo en el setup o desconexion previa),
// reintenta la conexion cada INTERVALO_REINTENTO_MS en vez de dejarlo
// desactivado para siempre.
// -----------------------------------------------------------------------------
void leerObd2() {
    if (!obd2Disponible) {
        if (millis() - ultimoIntentoReconexion < INTERVALO_REINTENTO_MS) return;
        ultimoIntentoReconexion = millis();

        obd2Disponible = intentarConectarObd2();
        if (obd2Disponible) {
            // Arranca limpio: maquina de estados desde el primer PID y
            // contador de fallos a cero, para no arrastrar fallos de antes
            // de que el adaptador estuviera disponible.
            estadoObd2 = OBD_RPM;
            fallosConsecutivos = 0;
        }
        return;
    }

    switch (estadoObd2) {

        case OBD_RPM: {
            float val = myELM327.rpm();

            if (myELM327.nb_rx_state == ELM_SUCCESS) {
                if (lecturaValida(val, 0.0f, RPM_MAX)) {
                    info_obd2.rpm = val;
                    registrarExito();
                } else {
                    registrarFallo();
                }
                estadoObd2 = OBD_VELOCIDAD;
            } else if (myELM327.nb_rx_state != ELM_GETTING_MSG) {
                myELM327.printError();
                registrarFallo();
                estadoObd2 = OBD_VELOCIDAD;
            }
            break;
        }

        case OBD_VELOCIDAD: {
            int32_t val = myELM327.kph();

            if (myELM327.nb_rx_state == ELM_SUCCESS) {
                if (lecturaValida((float)val, 0.0f, VELOCIDAD_MAX)) {
                    info_obd2.velocidad_kmh = (float)val;
                    registrarExito();
                } else {
                    registrarFallo();
                }
                estadoObd2 = OBD_CARGA_MOTOR;
            } else if (myELM327.nb_rx_state != ELM_GETTING_MSG) {
                myELM327.printError();
                registrarFallo();
                estadoObd2 = OBD_CARGA_MOTOR;
            }
            break;
        }

        case OBD_CARGA_MOTOR: {
            float val = myELM327.engineLoad();

            if (myELM327.nb_rx_state == ELM_SUCCESS) {
                if (lecturaValida(val, 0.0f, PORCENTAJE_MAX)) {
                    info_obd2.carga_motor = val;
                    registrarExito();
                } else {
                    registrarFallo();
                }
                estadoObd2 = OBD_ACELERADOR;
            } else if (myELM327.nb_rx_state != ELM_GETTING_MSG) {
                myELM327.printError();
                registrarFallo();
                estadoObd2 = OBD_ACELERADOR;
            }
            break;
        }

        case OBD_ACELERADOR: {
            float val = myELM327.throttle();

            if (myELM327.nb_rx_state == ELM_SUCCESS) {
                if (lecturaValida(val, 0.0f, PORCENTAJE_MAX)) {
                    info_obd2.pos_acelerador = val;
                    registrarExito();
                } else {
                    registrarFallo();
                }
                estadoObd2 = OBD_TEMP_REFRIGERANTE;
            } else if (myELM327.nb_rx_state != ELM_GETTING_MSG) {
                myELM327.printError();
                registrarFallo();
                estadoObd2 = OBD_TEMP_REFRIGERANTE;
            }
            break;
        }

        case OBD_TEMP_REFRIGERANTE: {
            float val = myELM327.engineCoolantTemp();

            if (myELM327.nb_rx_state == ELM_SUCCESS) {
                if (lecturaValida(val, TEMP_REFRIG_MIN, TEMP_REFRIG_MAX)) {
                    info_obd2.temp_refrigerante = val;
                    registrarExito();
                } else {
                    registrarFallo();
                }
                estadoObd2 = OBD_TIEMPO_ARRANQUE;
            } else if (myELM327.nb_rx_state != ELM_GETTING_MSG) {
                myELM327.printError();
                registrarFallo();
                estadoObd2 = OBD_TIEMPO_ARRANQUE;
            }
            break;
        }

        case OBD_TIEMPO_ARRANQUE: {
            uint16_t val = myELM327.runTime();

            // PID 0x1F ocupa 2 bytes sin signo: cualquier valor devuelto por
            // un uint16_t ya es, por construccion, un valor posible - no hace
            // falta comprobacion de rango, solo que la query haya tenido exito.
            if (myELM327.nb_rx_state == ELM_SUCCESS) {
                info_obd2.tiempo_arranque_s = val;
                registrarExito();
                estadoObd2 = OBD_TASA_CONSUMO;
            } else if (myELM327.nb_rx_state != ELM_GETTING_MSG) {
                myELM327.printError();
                registrarFallo();
                estadoObd2 = OBD_TASA_CONSUMO;
            }
            break;
        }

        case OBD_TASA_CONSUMO: {
            float val = myELM327.fuelRate();

            // PID opcional: muchos vehiculos responden NO_DATA/error aqui. Un
            // fallo de este PID en concreto no debe tirar la comunicacion
            // general por la borda, pero tampoco se aprueba sin comprobar rango.
            if (myELM327.nb_rx_state == ELM_SUCCESS) {
                if (lecturaValida(val, 0.0f, TASA_CONSUMO_MAX)) {
                    info_obd2.tasa_consumo_lh = val;
                    registrarExito();
                } else {
                    registrarFallo();
                }
                estadoObd2 = OBD_RPM;
            } else if (myELM327.nb_rx_state != ELM_GETTING_MSG) {
                myELM327.printError();
                registrarFallo();
                estadoObd2 = OBD_RPM;
            }
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// Cabecera CSV — columnas de este módulo, en el mismo orden que escribirDatosObd2
// -----------------------------------------------------------------------------
static const char CABECERA_OBD2[] PROGMEM =
    "obd_rpm,obd_velocidad_kmh,obd_carga_motor_pct,obd_pos_acelerador_pct,obd_temp_refrigerante_c,obd_tiempo_arranque_s,obd_tasa_consumo_lh";

size_t escribirCabeceraObd2(char* dst, size_t espacio) {
    return csvCopiarCabecera(dst, espacio, CABECERA_OBD2);
}

// -----------------------------------------------------------------------------
// Línea CSV — valores de InfoObd2, en el mismo orden que escribirCabeceraObd2
// -----------------------------------------------------------------------------
size_t escribirDatosObd2(char* dst, size_t espacio) {
    // Sin comunicacion fiable con el ELM327 se dejan las celdas vacias en vez
    // de repetir el ultimo valor bueno: en el CSV debe notarse que ahi no hubo medida.
    if (!info_obd2.valido) {
        return csvCamposVacios(dst, espacio, CABECERA_OBD2);
    }

    char rpmStr[10], velStr[8], cargaStr[8], acelStr[8], tempStr[8], consumoStr[10];

    dtostrf(info_obd2.rpm,               0, 1, rpmStr);
    dtostrf(info_obd2.velocidad_kmh,     0, 1, velStr);
    dtostrf(info_obd2.carga_motor,       0, 1, cargaStr);
    dtostrf(info_obd2.pos_acelerador,    0, 1, acelStr);
    dtostrf(info_obd2.temp_refrigerante, 0, 1, tempStr);
    dtostrf(info_obd2.tasa_consumo_lh,   0, 2, consumoStr);

    return csvEscritos(snprintf(dst, espacio, "%s,%s,%s,%s,%s,%u,%s",
        rpmStr, velStr, cargaStr, acelStr, tempStr,
        info_obd2.tiempo_arranque_s, consumoStr), espacio);
}
