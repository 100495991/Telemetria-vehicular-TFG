"""
API backend del TFG de Telemetria Vehicular UC3M.

Expone en JSON los datos de la tabla `telemetria` para que los consuma el
dashboard web. Solo lectura: la escritura la hace el ingestor.

Endpoints:
  GET /api/health              estado del servicio y de la BD
  GET /api/latest              ultima trama recibida
  GET /api/readings?limit=&since_id=   tramas recientes (para las graficas)
  GET /api/track?limit=        puntos GPS validos (para el mapa)
  GET /api/stats               resumen (total, primera/ultima, maximos)
  GET /api/trip/csv            CSV del viaje actual (ver trip_csv())

La documentacion interactiva se genera sola en /api/docs (Swagger UI).
"""

import csv
import io
import os
import logging
from contextlib import contextmanager
from datetime import timedelta

import pymysql
from pymysql.cursors import DictCursor
from fastapi import FastAPI, Query, HTTPException
from fastapi.responses import JSONResponse, StreamingResponse

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger("api")

DB_HOST = os.environ.get("DB_HOST", "db")
DB_PORT = int(os.environ.get("DB_PORT", "3306"))
DB_NAME = os.environ.get("DB_NAME", "telemetria")
DB_USER = os.environ.get("DB_USER", "telemetria")
DB_PASSWORD = os.environ.get("DB_PASSWORD", "telemetria")

app = FastAPI(
    title="TFG Telemetria Vehicular UC3M — API",
    version="1.0.0",
    docs_url="/api/docs",
    openapi_url="/api/openapi.json",
)


@contextmanager
def get_db():
    """
    Abre una conexion por peticion. Sencillo y suficiente para el volumen de un
    TFG; si hiciera falta mas carga se cambiaria por un pool. connect() cada vez
    ademas evita quedarse con una conexion muerta tras un reinicio de MySQL.
    """
    conn = pymysql.connect(
        host=DB_HOST, port=DB_PORT,
        user=DB_USER, password=DB_PASSWORD, database=DB_NAME,
        charset="utf8mb4", cursorclass=DictCursor,
        connect_timeout=5, read_timeout=10,
    )
    try:
        yield conn
    finally:
        conn.close()


def iso(fila: dict) -> dict:
    """received_at es datetime -> str ISO para que viaje bien en JSON."""
    if fila and fila.get("received_at") is not None:
        fila["received_at"] = fila["received_at"].isoformat(sep=" ", timespec="milliseconds")
    return fila


@app.get("/api/health")
def health():
    try:
        with get_db() as conn, conn.cursor() as cur:
            cur.execute("SELECT 1 AS ok")
            cur.fetchone()
        return {"status": "ok", "db": "up"}
    except Exception as e:
        log.warning("Healthcheck fallido: %s", e)
        return JSONResponse(status_code=503, content={"status": "degraded", "db": "down"})


@app.get("/api/latest")
def latest():
    """La trama mas reciente, para las tarjetas en vivo del dashboard."""
    with get_db() as conn, conn.cursor() as cur:
        cur.execute("SELECT * FROM telemetria ORDER BY id DESC LIMIT 1")
        fila = cur.fetchone()
    if not fila:
        return JSONResponse(status_code=404, content={"detail": "Aun no hay datos"})
    return iso(fila)


@app.get("/api/readings")
def readings(
    limit: int = Query(300, ge=1, le=5000, description="Numero maximo de filas"),
    since_id: int = Query(0, ge=0, description="Devolver solo filas con id > since_id"),
):
    """
    Tramas recientes en orden cronologico (ascendente), para alimentar las
    graficas. Con since_id el dashboard pide solo lo nuevo en cada refresco.
    """
    with get_db() as conn, conn.cursor() as cur:
        if since_id:
            # Lo nuevo desde el ultimo id conocido, acotado por 'limit'
            cur.execute(
                "SELECT * FROM telemetria WHERE id > %s ORDER BY id ASC LIMIT %s",
                (since_id, limit),
            )
            filas = cur.fetchall()
        else:
            # Las 'limit' ultimas, pero devueltas en orden ascendente
            cur.execute(
                "SELECT * FROM (SELECT * FROM telemetria ORDER BY id DESC LIMIT %s) t "
                "ORDER BY id ASC",
                (limit,),
            )
            filas = cur.fetchall()
    return [iso(f) for f in filas]


@app.get("/api/track")
def track(limit: int = Query(2000, ge=1, le=20000)):
    """Puntos GPS con coordenadas validas, en orden, para dibujar la ruta.

    Igual que en /api/readings: hay que coger los ultimos 'limit' puntos (no
    los primeros) y luego ordenarlos ascendente, o si la tabla crece por
    encima de 'limit' el mapa se queda pintando para siempre el tramo mas
    antiguo en vez de la ruta reciente.
    """
    with get_db() as conn, conn.cursor() as cur:
        cur.execute(
            "SELECT * FROM ("
            "  SELECT id, received_at, latitud, longitud, velocidad_kmh "
            "  FROM telemetria "
            "  WHERE latitud IS NOT NULL AND longitud IS NOT NULL "
            "    AND NOT (latitud = 0 AND longitud = 0) "
            "  ORDER BY id DESC LIMIT %s"
            ") t ORDER BY id ASC",
            (limit,),
        )
        filas = cur.fetchall()
    return [iso(f) for f in filas]


TRIP_GAP = timedelta(hours=1)   # mismo umbral que VIAJE_ANTIGUO_MS en el frontend
TRIP_MAX_FILAS = 50000          # tope de seguridad (~7h a 2 tramas/s): evita cargar la tabla entera


@app.get("/api/trip/csv")
def trip_csv():
    """
    CSV del "viaje actual": partiendo de la fila mas reciente, retrocede hasta
    encontrar el arranque del vehiculo (gprs_paquetes_enviados = 1: ese
    contador lo reinicia el firmware a 0 en cada boot del Arduino, ver
    infoGPRS en gprs.cpp) o un hueco de mas de una hora entre dos tramas
    consecutivas -- lo primero que aparezca marca el inicio del viaje, y esa
    fila frontera se incluye.
    """
    with get_db() as conn, conn.cursor() as cur:
        cur.execute(
            "SELECT * FROM telemetria ORDER BY id DESC LIMIT %s",
            (TRIP_MAX_FILAS,),
        )
        filas_desc = cur.fetchall()  # de la mas reciente a la mas antigua

    if not filas_desc:
        raise HTTPException(status_code=404, detail="Aun no hay datos")

    viaje_desc = [filas_desc[0]]
    for i in range(1, len(filas_desc)):
        anterior, siguiente = filas_desc[i], filas_desc[i - 1]
        viaje_desc.append(anterior)

        es_arranque = anterior.get("gprs_paquetes_enviados") == 1
        hueco_grande = (siguiente["received_at"] - anterior["received_at"]) > TRIP_GAP
        if es_arranque or hueco_grande:
            break

    viaje = list(reversed(viaje_desc))  # orden cronologico para el CSV
    columnas = list(viaje[0].keys())
    # iso() muta 'received_at' de datetime a str: el nombre de fichero se
    # calcula antes, con el datetime original de la ultima fila (la mas reciente).
    nombre = f"viaje_{viaje[-1]['received_at'].strftime('%Y%m%d_%H%M%S')}.csv"

    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(columnas)
    for fila in viaje:
        fila_iso = iso(fila)
        writer.writerow([fila_iso.get(c) for c in columnas])
    buf.seek(0)
    return StreamingResponse(
        buf,
        media_type="text/csv",
        headers={"Content-Disposition": f'attachment; filename="{nombre}"'},
    )


@app.get("/api/stats")
def stats():
    """Resumen para la cabecera del dashboard."""
    with get_db() as conn, conn.cursor() as cur:
        cur.execute(
            "SELECT COUNT(*) AS total, "
            "MIN(received_at) AS primera, MAX(received_at) AS ultima, "
            "MAX(velocidad_kmh) AS vel_max, "
            "MAX(temperatura_neumatico) AS temp_neu_max "
            "FROM telemetria"
        )
        r = cur.fetchone()
    for k in ("primera", "ultima"):
        if r.get(k) is not None:
            r[k] = r[k].isoformat(sep=" ", timespec="milliseconds")
    return r
