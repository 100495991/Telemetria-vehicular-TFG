"""
Ingestor MQTT -> MySQL

Se suscribe al broker Mosquitto, recibe cada trama del vehiculo y la inserta
como una fila en la tabla `telemetria`.

El vehiculo publica la cabecera (los
nombres de columna) en su propio topic con retain. Aqui se usa esa cabecera
para mapear cada campo de la trama a su columna de la base de datos POR NOMBRE:
asi el firmware puede reordenar o anadir sensores y el ingestor se adapta sin
tocar codigo, siempre que los nombres coincidan con los de la tabla.

Reglas de datos:
  - Campo vacio  -> NULL  (no 0): "sin lectura" != "lectura de 0".
  - Columna que la BD no conoce -> se ignora (se avisa una vez).
  - Trama recibida antes de la cabecera -> se retiene y se vuelca al conocerla.
"""

import os
import re
import csv
import time
import logging
from datetime import datetime, timezone

import pymysql
import paho.mqtt.client as mqtt

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("ingestor")

# -----------------------------------------------------------------------------
# Configuracion desde variables de entorno (las define docker-compose)
# -----------------------------------------------------------------------------
MQTT_HOST = os.environ.get("MQTT_HOST", "mosquitto")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "uc3m/tfg/telemetria")
MQTT_TOPIC_CABECERA = os.environ.get("MQTT_TOPIC_CABECERA", "uc3m/tfg/telemetria/cabecera")
MQTT_USER = os.environ.get("MQTT_USER", "")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "")

DB_HOST = os.environ.get("DB_HOST", "db")
DB_PORT = int(os.environ.get("DB_PORT", "3306"))
DB_NAME = os.environ.get("DB_NAME", "telemetria")
DB_USER = os.environ.get("DB_USER", "telemetria")
DB_PASSWORD = os.environ.get("DB_PASSWORD", "telemetria")

TABLA = "telemetria"

# -----------------------------------------------------------------------------
# Columnas que la BD acepta y como castear cada una. El nombre es el mismo que
# publica el firmware; 'received_at' lo pone el propio ingestor, no el vehiculo.
# -----------------------------------------------------------------------------
def _a_float(v):
    return float(v)

def _a_int(v):
    # El firmware puede mandar "1"/"0" o enteros; float() primero tolera "1.0"
    return int(float(v))

def _a_str(v):
    return str(v)

# Columnas CONOCIDAS: nombre -> (funcion de casteo, tipo SQL con el que se crean).
# La cabecera MQTT solo trae los NOMBRES, no los tipos, asi que para las columnas
# conocidas el tipo lo fijamos aqui. Cualquier columna que llegue en la cabecera y
# NO este en este mapa se crea automaticamente como TIPO_POR_DEFECTO (VARCHAR):
# nunca rompe la ingesta; si luego quieres el tipo correcto, la anades aqui.
TIPO_POR_DEFECTO = "VARCHAR(255)"

COLUMNAS = {
    "timestamp_local":       (_a_str,   "VARCHAR(32)"),  # hora local Madrid, no UTC (la pone el firmware)
    "status":                (_a_str,   "CHAR(1)"),
    "latitud":               (_a_float, "DOUBLE"),
    "latitud_hemisferio":    (_a_str,   "CHAR(1)"),
    "longitud":              (_a_float, "DOUBLE"),
    "longitud_hemisferio":   (_a_str,   "CHAR(1)"),
    "velocidad_kmh":         (_a_float, "DOUBLE"),
    "rumbo_grados":          (_a_float, "DOUBLE"),
    "fix_quality":           (_a_int,   "INT"),
    "fix_quality_desc":      (_a_str,   "VARCHAR(32)"),
    "n_satelites_en_uso":    (_a_int,   "INT"),
    "altitud_m":             (_a_float, "DOUBLE"),
    "hdop":                  (_a_float, "DOUBLE"),
    "hdop_calidad":          (_a_str,   "VARCHAR(16)"),
    "accel_ax_g":            (_a_float, "DOUBLE"),
    "accel_ay_g":            (_a_float, "DOUBLE"),
    "accel_az_g":            (_a_float, "DOUBLE"),
    "accel_roll_deg":        (_a_float, "DOUBLE"),
    "accel_pitch_deg":       (_a_float, "DOUBLE"),
    "accel_temperature":     (_a_float, "DOUBLE"),
    "temperatura_ambiente":  (_a_float, "DOUBLE"),
    "temperatura_neumatico": (_a_float, "DOUBLE"),
    "gprs_conectado":        (_a_int,   "TINYINT"),
    "gprs_rssi_pct":         (_a_int,   "INT"),
    "gprs_paquetes_enviados":(_a_int,   "BIGINT"),
    # OBD-II (ELM327): se declaran ya para que la columna se cree con el tipo
    # correcto (DOUBLE/INT) en vez de caer al VARCHAR por defecto.
    "obd_rpm":                   (_a_float, "DOUBLE"),   # PID 0x0C, rpm
    "obd_velocidad_kmh":         (_a_float, "DOUBLE"),   # PID 0x0D, km/h
    "obd_carga_motor_pct":       (_a_float, "DOUBLE"),   # PID 0x04, %
    "obd_pos_acelerador_pct":    (_a_float, "DOUBLE"),   # PID 0x11, %
    "obd_temp_refrigerante_c":   (_a_float, "DOUBLE"),   # PID 0x05, °C
    "obd_tiempo_arranque_s":     (_a_int,   "INT"),       # PID 0x1F, s
    "obd_tasa_consumo_lh":       (_a_float, "DOUBLE"),   # PID 0x5E, L/h
}

# Columnas que gestiona el sistema, no el vehiculo: nunca se crean desde la cabecera
COLUMNAS_RESERVADAS = {"id", "received_at"}

# Un nombre de columna valido para poder usarlo en DDL sin riesgo de inyeccion:
# la cabecera viene del topic MQTT (entrada no confiable), asi que se filtra.
IDENTIFICADOR_VALIDO = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,63}$")

def cast_de(nombre):
    """Funcion de casteo de una columna (str si es dinamica/desconocida)."""
    entrada = COLUMNAS.get(nombre)
    return entrada[0] if entrada else _a_str

def tipo_sql_de(nombre):
    """Tipo SQL con el que crear una columna (VARCHAR si es desconocida)."""
    entrada = COLUMNAS.get(nombre)
    return entrada[1] if entrada else TIPO_POR_DEFECTO

# -----------------------------------------------------------------------------
# Estado del proceso
# -----------------------------------------------------------------------------
columnas_vehiculo = None      # lista de nombres publicada por el vehiculo
columnas_db = set()           # columnas que EXISTEN ahora mismo en la tabla
pendientes = []               # [(received_at, trama)] a la espera de cabecera
MAX_PENDIENTES = 1000

db = None                     # conexion pymysql viva


# -----------------------------------------------------------------------------
# Base de datos
# -----------------------------------------------------------------------------
def conectar_db():
    """Conecta a MySQL reintentando: al arrancar, la BD puede tardar en aceptar."""
    global db
    intento = 0
    while True:
        intento += 1
        try:
            db = pymysql.connect(
                host=DB_HOST, port=DB_PORT,
                user=DB_USER, password=DB_PASSWORD, database=DB_NAME,
                autocommit=True, charset="utf8mb4",
                connect_timeout=5,
            )
            log.info("Conectado a MySQL %s:%s/%s", DB_HOST, DB_PORT, DB_NAME)
            return
        except Exception as e:
            if intento == 1 or intento % 5 == 0:
                log.warning("MySQL no disponible aun (intento %d): %s", intento, e)
            time.sleep(2)


# -----------------------------------------------------------------------------
# Esquema dinamico
#
# La tabla arranca solo con id + received_at, y las columnas de sensores se van
# creando segun lo que anuncia la cabecera del vehiculo. Asi, anadir un sensor
# nuevo al firmware NO obliga a tocar la plataforma: la columna aparece sola.
# -----------------------------------------------------------------------------
def asegurar_tabla_base():
    """Crea la tabla con lo minimo imprescindible si aun no existe."""
    with db.cursor() as cur:
        cur.execute(
            f"CREATE TABLE IF NOT EXISTS {TABLA} ("
            "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
            "  received_at DATETIME(3) NOT NULL,"
            "  PRIMARY KEY (id),"
            "  KEY idx_received_at (received_at)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
        )
    cargar_columnas_existentes()


def cargar_columnas_existentes():
    """Refresca 'columnas_db' con las columnas que hay ahora en la tabla."""
    global columnas_db
    with db.cursor() as cur:
        cur.execute(
            "SELECT COLUMN_NAME FROM information_schema.columns "
            "WHERE table_schema = %s AND table_name = %s",
            (DB_NAME, TABLA),
        )
        columnas_db = {fila[0] for fila in cur.fetchall()}


def asegurar_columna(nombre: str) -> bool:
    """
    Garantiza que 'nombre' existe como columna. La crea si falta, con el tipo
    conocido o VARCHAR por defecto. Devuelve False si el nombre no es un
    identificador valido (se rechaza para no permitir inyeccion via DDL).
    """
    if nombre in columnas_db:
        return True
    if nombre in COLUMNAS_RESERVADAS:
        return False
    if not IDENTIFICADOR_VALIDO.match(nombre):
        log.warning("Nombre de columna invalido, se ignora: %r", nombre)
        return False

    tipo = tipo_sql_de(nombre)
    # 'nombre' ya validado contra la regex -> seguro entre backticks
    with db.cursor() as cur:
        cur.execute(f"ALTER TABLE {TABLA} ADD COLUMN `{nombre}` {tipo} NULL")
    columnas_db.add(nombre)
    conocida = nombre in COLUMNAS
    log.info("Columna %s creada (%s)%s", nombre, tipo,
             "" if conocida else " [dinamica, desde la cabecera]")
    return True


def insertar(received_at, valores: dict):
    """
    Inserta una fila. 'valores' es {columna: valor|None} ya casteado.
    Reconecta y reintenta una vez si la conexion se ha caido.
    """
    global db
    columnas = ["received_at"] + list(valores.keys())
    marcadores = ", ".join(["%s"] * len(columnas))
    cols_sql = ", ".join(f"`{c}`" for c in columnas)
    sql = f"INSERT INTO {TABLA} ({cols_sql}) VALUES ({marcadores})"
    params = [received_at] + list(valores.values())

    for reintento in (False, True):
        try:
            with db.cursor() as cur:
                cur.execute(sql, params)
            return True
        except Exception as e:
            if not reintento:
                log.warning("Fallo al insertar, reconectando a MySQL: %s", e)
                conectar_db()
            else:
                log.error("Fila descartada tras reintentar: %s", e)
                return False


# -----------------------------------------------------------------------------
# Parseo de tramas
# -----------------------------------------------------------------------------
def trocear(linea: str):
    """Divide una linea CSV respetando comillas (usa el modulo csv)."""
    return next(csv.reader([linea]), [])


def mapear(trama: str) -> dict:
    """
    Empareja los campos de la trama con las columnas segun la cabecera del
    vehiculo. Devuelve {columna_bd: valor|None}. Solo incluye columnas que ya
    existen en la tabla (las crea procesar_cabecera al recibir la cabecera).
    """
    campos = trocear(trama)

    if len(campos) != len(columnas_vehiculo):
        # Firmware y cabecera retenida desincronizados: se ajusta sin perder el
        # dato (rellena o recorta), igual criterio que el message_router.
        log.warning("Trama con %d campos y cabecera de %d; se ajusta",
                    len(campos), len(columnas_vehiculo))
        if len(campos) < len(columnas_vehiculo):
            campos += [""] * (len(columnas_vehiculo) - len(campos))
        else:
            campos = campos[:len(columnas_vehiculo)]

    fila = {}
    for nombre, valor in zip(columnas_vehiculo, campos):
        if nombre not in columnas_db:
            continue   # nombre invalido/reservado que no se pudo crear: se omite

        valor = valor.strip()
        if valor == "":
            fila[nombre] = None          # sin lectura -> NULL
        else:
            try:
                fila[nombre] = cast_de(nombre)(valor)
            except ValueError:
                # Valor corrupto en esa columna: NULL en vez de reventar la fila
                log.warning("Valor invalido en '%s': %r -> NULL", nombre, valor)
                fila[nombre] = None
    return fila


def procesar_trama(trama: str, received_at):
    if columnas_vehiculo is None:
        if len(pendientes) < MAX_PENDIENTES:
            pendientes.append((received_at, trama))
            if len(pendientes) == 1:
                log.info("Trama recibida antes que la cabecera, esperando...")
        return
    insertar(received_at, mapear(trama))


def procesar_cabecera(texto: str):
    global columnas_vehiculo
    columnas = trocear(texto)
    if not columnas:
        log.warning("Cabecera vacia, se ignora")
        return
    if columnas == columnas_vehiculo:
        return   # el vehiculo la republica en cada reconexion
    log.info("Cabecera recibida (%d columnas): %s", len(columnas), ", ".join(columnas))
    columnas_vehiculo = columnas

    # Crea en la tabla las columnas que aun no existan (esquema dirigido por la
    # cabecera). Si alguna falla, se hace visible pero no bloquea al resto.
    for nombre in columnas:
        try:
            asegurar_columna(nombre)
        except Exception as e:
            log.error("No se pudo crear la columna '%s': %s", nombre, e)

    if pendientes:
        log.info("Volcando %d tramas que esperaban cabecera", len(pendientes))
        for received_at, trama in pendientes:
            insertar(received_at, mapear(trama))
        pendientes.clear()


# -----------------------------------------------------------------------------
# Callbacks MQTT
# -----------------------------------------------------------------------------
def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        # Cabecera primero: al ir con retain, el broker la entrega al suscribir
        client.subscribe([(MQTT_TOPIC_CABECERA, 0), (MQTT_TOPIC, 0)])
        log.info("Conectado a MQTT, suscrito a %s y %s", MQTT_TOPIC_CABECERA, MQTT_TOPIC)
    else:
        log.error("MQTT rechazo la conexion: %s", reason_code)


def on_message(client, userdata, msg):
    carga = msg.payload.decode("utf-8", errors="ignore").strip()
    if msg.topic == MQTT_TOPIC_CABECERA:
        procesar_cabecera(carga)
    else:
        # Milisegundos, hora del servidor. UTC para que sea inequivoco.
        procesar_trama(carga, datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3])


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties=None):
    log.warning("Desconectado de MQTT (%s); paho reintentara", reason_code)


# -----------------------------------------------------------------------------
def main():
    conectar_db()

    # Garantiza la tabla base y crea desde ya las columnas CONOCIDAS, para que la
    # API pueda consultarlas aunque aun no haya llegado ninguna cabecera. Las
    # columnas nuevas/desconocidas se anadiran cuando el vehiculo las anuncie.
    asegurar_tabla_base()
    for nombre in COLUMNAS:
        try:
            asegurar_columna(nombre)
        except Exception as e:
            log.error("No se pudo asegurar la columna '%s': %s", nombre, e)
    log.info("Esquema listo (%d columnas)", len(columnas_db))

    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                         client_id="IngestorTFG")
    if MQTT_USER:
        client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.reconnect_delay_set(min_delay=1, max_delay=30)

    # El broker puede tardar en levantar; reintentar el primer connect
    while True:
        try:
            client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
            break
        except Exception as e:
            log.warning("Mosquitto no disponible aun: %s", e)
            time.sleep(2)

    log.info("Ingestor en marcha")
    client.loop_forever()


if __name__ == "__main__":
    main()
