/* =============================================================================
   Dashboard de telemetria — logica de cliente
   Consume la API local (mismo origen, via proxy nginx) y refresca:
     - cabecera (GPRS / GPS / reloj)      /api/latest (implicito via /api/readings)
     - cuadro de mandos y tarjetas        ultima fila recibida
     - mapa de la ruta                    /api/track
     - graficas                           /api/readings (incremental por since_id)
     - vista de historico                 /api/readings
   Campos OBD (obd_rpm, obd_velocidad_kmh, obd_carga_motor_pct,
   obd_pos_acelerador_pct, obd_temp_refrigerante_c, ...): si no llegan en una
   trama (p.ej. ELM327 sin fix aun) se muestran como "sin datos" en vez de
   inventar un valor.
   ============================================================================= */

const REFRESCO_MS = 500;       // cadencia de sondeo
const MAX_PUNTOS_GRAFICA = 60;  // ventana visible en las graficas (como el diseño)
const MAX_FILAS_HISTORIAL = 200;
const STALE_MS = 10000;         // sin datos nuevos en este tiempo -> "sin conexion"
const MAP_REFRESH_TICKS = 3;    // el mapa se resincroniza cada N ticks
const VIAJE_ANTIGUO_MS = 60 * 60 * 1000;  // sin tramas en mas de esto -> se considera un viaje anterior, no se pinta

let ultimoId = 0;
let ultimaRecepcion = null;
let tickCount = 0;
let historial = [];             // filas cronologicas (para graficas + tabla), cap MAX_FILAS_HISTORIAL

// -----------------------------------------------------------------------------
// Utilidades numericas / formato
// -----------------------------------------------------------------------------
function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }

function num(v) {
    if (v === null || v === undefined || v === "") return null;
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
}

function fmt(v, dec = 1) {
    const n = num(v);
    return n === null ? "—" : n.toFixed(dec);
}

function fmtInt(v) {
    const n = num(v);
    return n === null ? "—" : String(Math.round(n));
}

async function getJSON(url) {
    const r = await fetch(url);
    if (!r.ok) throw new Error(`${url} -> ${r.status}`);
    return r.json();
}

// El ingestor guarda received_at en UTC sin sufijo de zona ("2026-07-23 18:15:52.395"):
// hay que decirle a Date explicitamente que es UTC o el navegador lo interpreta
// como hora local y tanto el calculo de antiguedad como la hora mostrada salen mal.
function parseUTC(receivedAt) {
    if (!receivedAt) return NaN;
    return Date.parse(receivedAt.replace(" ", "T") + "Z");
}

function msDesdeRecepcion(receivedAt) {
    const t = parseUTC(receivedAt);
    return Number.isFinite(t) ? Date.now() - t : Infinity;
}

function esViajeReciente(receivedAt) {
    return msDesdeRecepcion(receivedAt) < VIAJE_ANTIGUO_MS;
}

// Convierte el UTC de received_at a la hora real de Madrid (gestiona sola el
// cambio de horario verano/invierno, a diferencia del offset fijo del firmware).
const FORMATO_HORA_MADRID = new Intl.DateTimeFormat("es-ES", {
    hour: "2-digit", minute: "2-digit", second: "2-digit",
    hour12: false, timeZone: "Europe/Madrid",
});

function horaCorta(receivedAt) {
    const t = parseUTC(receivedAt);
    if (!Number.isFinite(t)) return "--:--:--";
    return FORMATO_HORA_MADRID.format(new Date(t));
}

function horaConCentesimas(receivedAt) {
    const t = parseUTC(receivedAt);
    if (!Number.isFinite(t)) return "--:--:--.--";
    const ms = (receivedAt.split(".")[1] || "00" ).padEnd(2, "0").slice(0, 2);
    return `${FORMATO_HORA_MADRID.format(new Date(t))}.${ms}`;
}

function degToCompass(deg) {
    const n = num(deg);
    if (n === null) return "—";
    const dirs = ["N", "NE", "E", "SE", "S", "SO", "O", "NO"];
    return dirs[Math.round(n / 45) % 8];
}

// -----------------------------------------------------------------------------
// Mapa (Leaflet)
// -----------------------------------------------------------------------------
// zoomControl abajo a la derecha: arriba-izquierda lo ocupa el overlay de
// velocidad/rumbo y arriba-derecha el boton de centrar mapa.
const map = L.map("map", { zoomControl: false }).setView([40.3317, -3.7684], 15); // UC3M Leganes por defecto
L.control.zoom({ position: "bottomright" }).addTo(map);
L.tileLayer("https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png", {
    attribution: "© OpenStreetMap contributors © CARTO",
    maxZoom: 19,
    subdomains: "abcd",
}).addTo(map);

const ruta = L.polyline([], { color: "#3454d1", weight: 4, opacity: 0.85 }).addTo(map);
const carIcon = L.divIcon({
    className: "",
    html: '<div style="width:22px;height:22px;display:flex;align-items:center;justify-content:center;"><div class="car-arrow"></div></div>',
    iconSize: [22, 22], iconAnchor: [11, 11],
});
let marcador = null;
let hayRuta = false;
let ultimaPosicion = null;

function rotateMarker(heading) {
    if (!marcador) return;
    const el = marcador.getElement();
    const inner = el && el.querySelector(".car-arrow");
    if (inner) inner.style.transform = `rotate(${heading || 0}deg)`;
}

async function refrescarMapa() {
    try {
        const pts = await getJSON("/api/track?limit=5000");
        if (!pts.length) return;

        // Si el ultimo punto es de un viaje anterior (mas de VIAJE_ANTIGUO_MS sin
        // tramas nuevas), no se pinta la ruta: evita mostrar el trayecto de la
        // ultima vez que se uso el vehiculo como si fuera el actual.
        if (!esViajeReciente(pts[pts.length - 1].received_at)) {
            ruta.setLatLngs([]);
            hayRuta = false;
            return;
        }

        const coords = pts.map(p => [p.latitud, p.longitud]);
        ruta.setLatLngs(coords);
        if (!hayRuta) {
            map.fitBounds(ruta.getBounds(), { padding: [30, 30] });
            hayRuta = true;
        }
    } catch (e) { /* aun sin track */ }
}

function actualizarPosicionMarcador(lat, lon, heading) {
    if (lat === null || lon === null) return;
    const pos = [lat, lon];
    ultimaPosicion = pos;
    if (!marcador) {
        marcador = L.marker(pos, { icon: carIcon }).addTo(map);
        map.panTo(pos, { animate: false });
    } else {
        marcador.setLatLng(pos);
    }
    rotateMarker(heading);
    map.panTo(pos, { animate: true, duration: 0.9 });
}

document.getElementById("btn-center-map").addEventListener("click", () => {
    if (ultimaPosicion) map.panTo(ultimaPosicion);
});

// -----------------------------------------------------------------------------
// Cuadro de mandos: gauges SVG (arco 250°, igual que el diseño de referencia)
// -----------------------------------------------------------------------------
function polarToCartesian(cx, cy, r, angleDeg) {
    const rad = (angleDeg - 90) * Math.PI / 180;
    return { x: cx + r * Math.cos(rad), y: cy + r * Math.sin(rad) };
}

function describeArc(cx, cy, r, startAngle, endAngle) {
    const start = polarToCartesian(cx, cy, r, startAngle);
    const end = polarToCartesian(cx, cy, r, endAngle);
    const largeArc = Math.abs(endAngle - startAngle) <= 180 ? "0" : "1";
    const sweep = endAngle >= startAngle ? "1" : "0";
    return `M ${start.x.toFixed(2)} ${start.y.toFixed(2)} A ${r} ${r} 0 ${largeArc} ${sweep} ${end.x.toFixed(2)} ${end.y.toFixed(2)}`;
}

const START_ANGLE = -125, END_ANGLE = 125;

function setGauge(prefix, value, max, colorFn) {
    const track = document.getElementById(`gauge-${prefix}-track`);
    const valuePath = document.getElementById(`gauge-${prefix}-value`);
    const needle = document.getElementById(`gauge-${prefix}-needle`);
    track.setAttribute("d", describeArc(100, 100, 80, START_ANGLE, END_ANGLE));

    const has = value !== null;
    const pct = clamp((value || 0) / max, 0, 1);
    const angle = START_ANGLE + (END_ANGLE - START_ANGLE) * pct;
    valuePath.setAttribute("d", has ? describeArc(100, 100, 80, START_ANGLE, angle) : "");
    needle.style.transform = `rotate(${(has ? angle : START_ANGLE).toFixed(1)}deg)`;
    valuePath.style.stroke = has ? colorFn(pct) : "#d7dae3";
    needle.style.stroke = has ? "#1a1d29" : "#c3c7d4";
}

// -----------------------------------------------------------------------------
// Graficas (Chart.js)
// -----------------------------------------------------------------------------
function crearGrafica(canvasId, series, opts) {
    return new Chart(document.getElementById(canvasId), {
        type: "line",
        data: {
            labels: [],
            datasets: series.map(s => ({
                label: s.label,
                data: [],
                borderColor: s.color,
                backgroundColor: s.color + "14",
                borderWidth: 2,
                pointRadius: 0,
                tension: 0.3,
                spanGaps: true,
                yAxisID: s.axis || "y",
            })),
        },
        options: Object.assign({
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            interaction: { intersect: false, mode: "index" },
            plugins: { legend: { display: false } },
            scales: {
                x: { ticks: { color: "#8a8fa3", maxTicksLimit: 5, font: { size: 10 } }, grid: { color: "#f0f1f6" } },
                y: { ticks: { color: "#3454d1", font: { size: 10 } }, grid: { color: "#f0f1f6" } },
            },
        }, opts || {}),
    });
}

const gRpmSpeed = crearGrafica("chart-rpm-speed", [
    { label: "RPM", color: "#3454d1", axis: "y" },
    { label: "Velocidad", color: "#f59e0b", axis: "y1" },
], {
    scales: {
        x: { ticks: { color: "#8a8fa3", maxTicksLimit: 5, font: { size: 10 } }, grid: { color: "#f0f1f6" } },
        y: { ticks: { color: "#3454d1", font: { size: 10 } }, grid: { color: "#f0f1f6" } },
        y1: { position: "right", ticks: { color: "#f59e0b", font: { size: 10 } }, grid: { drawOnChartArea: false } },
    },
});

const gTemp = crearGrafica("chart-temp", [
    { label: "Refrig.", color: "#dc2626" },
    { label: "Ambiente", color: "#3454d1" },
    { label: "Neumático", color: "#a855f7" },
]);

function empujar(chart, label, valores) {
    chart.data.labels.push(label);
    valores.forEach((v, i) => chart.data.datasets[i].data.push(v));
    if (chart.data.labels.length > MAX_PUNTOS_GRAFICA) {
        chart.data.labels.shift();
        chart.data.datasets.forEach(d => d.data.shift());
    }
}

function empujarFila(f) {
    const etq = horaCorta(f.received_at);
    empujar(gRpmSpeed, etq, [num(f.obd_rpm), num(f.velocidad_kmh)]);
    empujar(gTemp, etq, [num(f.obd_temp_refrigerante_c), num(f.temperatura_ambiente), num(f.temperatura_neumatico)]);
}

function refrescarGraficas() {
    gRpmSpeed.update();
    gTemp.update();
}

// -----------------------------------------------------------------------------
// Cabecera + cuadro de mandos + tarjetas resumen (ultima trama)
// -----------------------------------------------------------------------------
function actualizarCabecera(f) {
    const gprsDot = document.getElementById("gprs-dot");
    const gprsText = document.getElementById("gprs-text");
    const rssi = num(f.gprs_rssi_pct);
    const conectado = Number(f.gprs_conectado) === 1;
    gprsDot.className = "dot" + (conectado ? " live" : " down");
    gprsText.textContent = `GPRS ${rssi === null ? "—" : rssi}%`;

    document.getElementById("gps-text").textContent =
        `${fmtInt(f.n_satelites_en_uso)} sat · ${f.fix_quality_desc || "—"}`;
}

function actualizarMapaOverlay(f) {
    document.getElementById("map-speed").innerHTML = `${fmt(f.velocidad_kmh, 1)} <span class="unit">km/h</span>`;
    document.getElementById("map-heading").innerHTML =
        `${degToCompass(f.rumbo_grados)} <span class="unit">${fmt(f.rumbo_grados, 0)}°</span>`;
    document.getElementById("map-alt").textContent =
        `Alt. ${fmt(f.altitud_m, 0)} m · HDOP ${fmt(f.hdop, 2)} (${f.hdop_calidad || "—"})`;
}

function actualizarCuadroDeMandos(f) {
    const rpm = num(f.obd_rpm);
    const velObd = num(f.obd_velocidad_kmh);
    setGauge("speed", velObd, 140, pct => (pct > 0.85 ? "#f59e0b" : "#3454d1"));
    setGauge("rpm", rpm, 7000, pct => (pct > 0.85 ? "#dc2626" : pct > 0.7 ? "#f59e0b" : "#3454d1"));
    document.getElementById("gauge-speed-text").textContent = velObd === null ? "—" : velObd.toFixed(0);
    document.getElementById("gauge-rpm-text").textContent = rpm === null ? "—" : String(Math.round(rpm));

    const carga = num(f.obd_carga_motor_pct);
    const acelerador = num(f.obd_pos_acelerador_pct);
    document.getElementById("mini-load").textContent = carga === null ? "—" : `${fmt(carga, 0)}%`;
    document.getElementById("mini-accel").textContent = acelerador === null ? "—" : `${fmt(acelerador, 0)}%`;
    const coolant = num(f.obd_temp_refrigerante_c);
    const coolantEl = document.getElementById("mini-coolant");
    coolantEl.textContent = coolant === null ? "—" : `${coolant.toFixed(1)}°`;
    coolantEl.style.color = coolant === null ? "" : (coolant > 100 ? "#dc2626" : coolant > 95 ? "#f59e0b" : "#1a1d29");

    const anyObd = rpm !== null || velObd !== null || carga !== null || acelerador !== null || coolant !== null;
    document.getElementById("gauges-note").hidden = anyObd;
}

function actualizarTiles(f) {
    document.getElementById("tile-amb").textContent = `${fmt(f.temperatura_ambiente, 1)}°C`;
    document.getElementById("tile-tire").textContent = `${fmt(f.temperatura_neumatico, 1)}°C`;

    const roll = num(f.accel_roll_deg);
    const pitch = num(f.accel_pitch_deg);
    document.getElementById("tile-attitude").textContent = `Roll ${fmt(roll, 1)}° · Pitch ${fmt(pitch, 1)}°`;
    const horizon = document.getElementById("attitude-horizon");
    horizon.style.transform = `rotate(${roll || 0}deg) translateY(${clamp((pitch || 0) * 2, -16, 16)}px)`;

    const packets = fmtInt(f.gprs_paquetes_enviados);
    document.getElementById("tile-packets").textContent = `${packets} paquetes enviados`;
    const rssi = num(f.gprs_rssi_pct) || 0;
    const bar = document.getElementById("gprs-bar");
    bar.style.width = `${clamp(rssi, 0, 100)}%`;
    bar.style.background = Number(f.gprs_conectado) === 1 ? "#16a34a" : "#dc2626";
}

function actualizarPanel(f) {
    actualizarCabecera(f);
    actualizarMapaOverlay(f);
    actualizarCuadroDeMandos(f);
    actualizarTiles(f);
    if (Number(f.fix_quality) > 0 || f.status === "A") {
        actualizarPosicionMarcador(num(f.latitud), num(f.longitud), num(f.rumbo_grados));
    }
}

// -----------------------------------------------------------------------------
// Vista Histórico
// -----------------------------------------------------------------------------
function celda(v, cls) {
    const c = cls ? ` class="${cls}"` : "";
    return `<td${c}>${v === null || v === undefined || v === "" ? "—" : v}</td>`;
}

function filaHistorial(r) {
    const valido = r.status === "A" || Number(r.fix_quality) > 0;
    const gprsOk = Number(r.gprs_conectado) === 1;
    return "<tr>"
        + celda(horaConCentesimas(r.received_at))
        + celda(valido ? "Válido" : "Sin fix", valido ? "status-valido" : "status-invalido")
        + celda(fmt(r.latitud, 5), "num")
        + celda(r.latitud_hemisferio)
        + celda(fmt(r.longitud, 5), "num")
        + celda(r.longitud_hemisferio)
        + celda(fmt(r.velocidad_kmh, 1), "num")
        + celda(fmt(r.rumbo_grados, 0), "num")
        + celda(r.fix_quality, "ctr")
        + celda(r.fix_quality_desc)
        + celda(fmtInt(r.n_satelites_en_uso), "num")
        + celda(fmt(r.altitud_m, 0), "num")
        + celda(fmt(r.hdop, 2), "num")
        + celda(r.hdop_calidad)
        + celda(fmt(r.accel_ax_g, 3), "num")
        + celda(fmt(r.accel_ay_g, 3), "num")
        + celda(fmt(r.accel_az_g, 3), "num")
        + celda(fmt(r.accel_roll_deg, 1), "num")
        + celda(fmt(r.accel_pitch_deg, 1), "num")
        + celda(fmt(r.accel_temperature, 1), "num")
        + celda(fmt(r.temperatura_ambiente, 1), "num")
        + celda(fmt(r.temperatura_neumatico, 1), "num")
        + celda(fmtInt(r.obd_rpm), "num")
        + celda(fmt(r.obd_velocidad_kmh, 1), "num")
        + celda(fmt(r.obd_carga_motor_pct, 0), "num")
        + celda(fmt(r.obd_pos_acelerador_pct, 0), "num")
        + celda(fmt(r.obd_temp_refrigerante_c, 1), "num")
        + celda(fmtInt(r.obd_tiempo_arranque_s), "num")
        + celda(fmt(r.obd_tasa_consumo_lh, 2), "num")
        + celda(gprsOk ? "Sí" : "No", gprsOk ? "gprs-si" : "gprs-no")
        + celda(fmtInt(r.gprs_rssi_pct), "num")
        + celda(fmtInt(r.gprs_paquetes_enviados), "num")
        + "</tr>";
}

function pintarHistorial() {
    const body = document.getElementById("hist-body");
    const rows = historial.slice().reverse();
    document.getElementById("hist-count").textContent = `${historial.length} registros · más reciente primero`;
    body.innerHTML = rows.length
        ? rows.map(filaHistorial).join("")
        : `<tr class="empty-row"><td colspan="32">Aún no hay datos</td></tr>`;
}

// -----------------------------------------------------------------------------
// Navegación entre vistas (Panel / Histórico)
// -----------------------------------------------------------------------------
const btnDashboard = document.getElementById("btn-view-dashboard");
const btnHistorial = document.getElementById("btn-view-historial");
const viewDashboard = document.getElementById("view-dashboard");
const viewHistorial = document.getElementById("view-historial");

function irA(vista) {
    const dash = vista === "dashboard";
    btnDashboard.classList.toggle("active", dash);
    btnHistorial.classList.toggle("active", !dash);
    viewDashboard.hidden = !dash;
    viewHistorial.hidden = dash;
    if (!dash) pintarHistorial();
}
btnDashboard.addEventListener("click", () => irA("dashboard"));
btnHistorial.addEventListener("click", () => irA("historial"));

// -----------------------------------------------------------------------------
// Estado de conexión
// -----------------------------------------------------------------------------
function actualizarEstadoConexion() {
    if (!ultimaRecepcion || Date.now() - ultimaRecepcion >= STALE_MS) {
        document.getElementById("gprs-dot").className = "dot down";
    }
}

// -----------------------------------------------------------------------------
// Bucle de sondeo
// -----------------------------------------------------------------------------
function registrarFilas(filas) {
    for (const f of filas) {
        historial.push(f);
        if (f.id > ultimoId) ultimoId = f.id;
    }
    if (historial.length > MAX_FILAS_HISTORIAL) {
        historial.splice(0, historial.length - MAX_FILAS_HISTORIAL);
    }
    filas.forEach(empujarFila);
}

async function tick() {
    tickCount += 1;
    try {
        const nuevas = await getJSON(`/api/readings?since_id=${ultimoId}&limit=1000`);
        if (nuevas.length) {
            registrarFilas(nuevas);
            refrescarGraficas();
            const ultima = nuevas[nuevas.length - 1];
            actualizarPanel(ultima);
            if (!viewHistorial.hidden) pintarHistorial();
            ultimaRecepcion = Date.now();
        }
    } catch (e) { /* API o BD caidas momentaneamente: se reintenta al siguiente tick */ }

    if (tickCount % MAP_REFRESH_TICKS === 0) refrescarMapa();
    actualizarEstadoConexion();
}

async function inicio() {
    try {
        const filas = await getJSON(`/api/readings?limit=${MAX_FILAS_HISTORIAL}`);
        if (filas.length) {
            // Avanza el cursor de since_id aunque no se lleguen a pintar: si no,
            // tick() volveria a traer estas mismas filas antiguas en cada sondeo.
            for (const f of filas) if (f.id > ultimoId) ultimoId = f.id;

            // Si la ultima fila es de un viaje anterior (mas de VIAJE_ANTIGUO_MS
            // sin tramas nuevas), no se pinta el panel/graficas con ese historial:
            // se deja todo vacio hasta que llegue una trama realmente reciente.
            if (esViajeReciente(filas[filas.length - 1].received_at)) {
                registrarFilas(filas);
                refrescarGraficas();
                actualizarPanel(filas[filas.length - 1]);
                ultimaRecepcion = Date.now();
            }
        }
    } catch (e) { /* aun sin datos */ }

    await refrescarMapa();
    actualizarEstadoConexion();

    const tickReloj = () => {
        document.getElementById("clock").textContent = new Date().toLocaleTimeString("es-ES", { hour12: false });
    };
    tickReloj();
    setInterval(tickReloj, 1000);
    setInterval(tick, REFRESCO_MS);
}

inicio();
