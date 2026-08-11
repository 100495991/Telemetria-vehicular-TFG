-- =============================================================================
-- Esquema base de la telemetria — TFG UC3M
--
-- Aqui solo se crea la tabla con lo minimo (id + momento de recepcion). Las
-- columnas de sensores NO se definen a mano: las crea el ingestor de forma
-- dinamica a partir de la cabecera que publica el vehiculo por MQTT.
--
--   - Columnas CONOCIDAS: el ingestor las crea al arrancar con su tipo correcto
--     (ver el mapa COLUMNAS en platform/ingestor/ingestor.py).
--   - Columnas NUEVAS que aparezcan en la cabecera: se crean solas como VARCHAR.
--
-- Asi, anadir un sensor al firmware no obliga a tocar ni este fichero ni la
-- plataforma. Este script solo se ejecuta al inicializar una BD vacia.
-- =============================================================================

CREATE TABLE IF NOT EXISTS telemetria (
    id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    received_at  DATETIME(3)     NOT NULL,   -- lo pone el ingestor al recibir
    PRIMARY KEY (id),
    KEY idx_received_at (received_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
