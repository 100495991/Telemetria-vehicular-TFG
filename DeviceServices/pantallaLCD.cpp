#include "pantallaLCD.h"
#include <TouchScreen.h>
#include "gps.h"
#include "accelerometer.h"
#include "temperature_sensor.h"
#include "obd2.h"
#include "gprs.h"

// =============================================================================
// pantallaLCD.cpp
//
// Menú táctil de 5 páginas (HOME, GPS, DINAM, MOTOR, COMMS) para el TFT
// ILI9486/ILI9488 (MCUFRIEND_kbv, 28 pines, 8 bits paralelo).
//
// Cómo está organizado (para poder tocar una página sin romper las demás):
//   - Cada página tiene su bloque "PAGINA ..." con dos funciones:
//       dibujarEstructuraPaginaX()  -> se dibuja UNA VEZ al entrar en la página
//       actualizarPaginaX(forzar)   -> se llama en cada loop(), repinta solo
//                                      lo que cambió (o todo si forzar=true)
//   - Las variables "ant_*" que recuerdan el último valor pintado son
//     `static` DENTRO de cada actualizarPaginaX(), no variables de fichero.
//     Así ninguna página puede pisar el estado de otra sin querer.
//   - Todo campo numerico procedente de un modulo con bandera "valido" se
//     pinta como "--" mientras esa bandera este a false, en vez de arrastrar
//     el ultimo numero (o un 0 inicial) como si fuera un dato fresco.
// =============================================================================

MCUFRIEND_kbv tft;
Pagina        paginaActual = PAG_HOME;

// =============================================================================
// PALETA DE COLORES — DOS MODOS SELECCIONABLES EN COMPILACIÓN
//
// Poner MODO_PANTALLA_AZUL a:
//   0 -> paleta a todo color (la original: verde/rojo/ámbar/cian/marrón...)
//   1 -> paleta "solo azul": únicamente tonos de azul y negro. Muestra la
//        misma información; los estados que antes se distinguían por color
//        (ok/alerta/ámbar) ahora se distinguen por el brillo del tono azul.
//
// Todo el resto del código usa SIEMPRE los nombres COLOR_* de abajo, así que
// no hay que tocar ninguna función de dibujo para cambiar de modo.
// =============================================================================
// #define MODO_PANTALLA_AZUL 1   // <-- cambiar a 1 para el modo solo azul/negro (Migrado a config.h)

#if MODO_PANTALLA_AZUL
// ---- Paleta SOLO AZUL / NEGRO (RGB565, canal rojo siempre a 0) ----
// Escogidos sobre un degradado de luminancia para que todos los textos y
// números se lean con claridad sobre el fondo negro.
#define COLOR_FONDO      0x0000  // negro
#define COLOR_PANEL      0x0089  // azul muy oscuro (relleno de tarjetas)
#define COLOR_PISTA      0x0152  // azul oscuro (pistas de barras de progreso)
#define COLOR_BORDE      0x0218  // azul medio-oscuro (aros, guías)
#define COLOR_TEXTO_SEC  0x051C  // azul acero claro (etiquetas secundarias)
#define COLOR_TEXTO_PRIN 0x073F  // azul-cian muy claro (valores, máx. legible)
#define COLOR_ETIQUETA   0x061E  // azul claro (títulos de tarjeta)
#define COLOR_ACENTO     0x07FF  // cian brillante (acento)
#define COLOR_OK         0x05FF  // azul cielo claro (estado correcto)
#define COLOR_ALERTA     0x041F  // azul brillante (estado de alerta)
#define COLOR_AMBAR      0x02FF  // azul medio (aviso)
#define COLOR_AZUL       0x001F  // azul puro (barra acelerador)
#define COLOR_CIELO      0x03FF  // azul cielo (horizonte artificial)
#define COLOR_TIERRA     0x010E  // azul oscuro (tierra del horizonte)
#else
// ---- Paleta ORIGINAL a todo color (RGB565) ----
#define COLOR_FONDO      0x0000  // negro
#define COLOR_PANEL      0x18E3  // gris muy oscuro (relleno de tarjetas)
#define COLOR_PISTA      0x2945  // gris de las pistas de barras de progreso
#define COLOR_BORDE      0x4208  // gris medio (aros, guías)
#define COLOR_TEXTO_SEC  0x8C71  // gris claro (etiquetas secundarias)
#define COLOR_TEXTO_PRIN 0xFFFF  // blanco (valores)
#define COLOR_ETIQUETA   0xDDD1  // tono arena (títulos de tarjeta)
#define COLOR_ACENTO     0x3E9F  // cian
#define COLOR_OK         0x37E0  // verde
#define COLOR_ALERTA     0xF800  // rojo
#define COLOR_AMBAR      0xFD20  // ámbar
#define COLOR_AZUL       0x3D9F  // azul claro (barra acelerador)
#define COLOR_CIELO      0x3BFD  // azul cielo (horizonte artificial)
#define COLOR_TIERRA     0x7A86  // marrón tierra (horizonte artificial)
#endif

// ---- Geometría de pantalla (480x320 horizontal) ----
#define PANT_ANCHO 480
#define PANT_ALTO  320
#define CABECERA_ALTO 32
#define TABBAR_ALTO   48
#define CONTENIDO_Y0  CABECERA_ALTO
#define CONTENIDO_Y1  (PANT_ALTO - TABBAR_ALTO)

static const char* NOMBRES_PESTANA[5] = {"home", "gps", "dinam", "motor", "comms"};

// Tasa de refresco de la información del ACELERÓMETRO en pantalla (ms).
// Solo afecta a lo que depende del IMU: el horizonte y los valores
// pitch/roll/ax/ay/az de la página Dinámica y la bola de G de Home. El resto
// de datos (velocidad, rpm, temperatura, GPS...) siguen refrescándose a la
// velocidad normal del loop. Subir este número = refresco más lento/estable.
#define ACCEL_REFRESCO_MS 500

// =============================================================================
// TACTIL — CONFIGURACION Y CALIBRACION
//
// PASO 1 — Encontrar el patillaje correcto (TOUCH_PINOUT):
// Los clones de shield conectan el panel táctil a parejas de pines distintas.
// Si los pines no son los de tu shield, un eje queda "clavado" cerca del
// máximo (leemos un pin flotante) y el otro solo varía en una franja pequeña.
// Con TOUCH_DEBUG a 1, desliza el dedo por TODA la pantalla mirando la traza
// [TOUCH raw] del monitor serie y prueba TOUCH_PINOUT = 1, 2, 3, 4 hasta que
// AMBOS ejes barran un rango amplio (aprox. 100-900) al recorrer la pantalla.
//
// PASO 2 — Ajustar los rangos: con el patillaje bueno, apunta el valor raw en
// cada borde de la pantalla y ponlo en TOUCH_X/Y_MIN/MAX.
//
// PASO 3 — Orientación: el panel entrega coordenadas en su orientación nativa
// (vertical) y la pantalla va girada (setRotation(1)), de ahí TOUCH_SWAP_XY.
//   - Pestañas en orden inverso (tocas "home" y sale "comms") -> TOUCH_INVERT_X 1
//   - Se activa tocando la parte de ARRIBA de la pantalla     -> TOUCH_INVERT_Y 1
//   - Responde en diagonal                                    -> TOUCH_SWAP_XY 0
//
// Al terminar, poner TOUCH_DEBUG a 0.
// =============================================================================
#define TOUCH_DEBUG  1   // 1 = imprime cada lectura raw mientras tocas (calibración)
#define TOUCH_PINOUT 3   // probar en orden 3 -> 4 -> 2 -> 1 (ver nota abajo)

// Nota: el táctil comparte 4 líneas con el LCD. Con el cableado de este
// shield (LCD_WR=A1, LCD_RS=A2, LCD_D6=6, LCD_D7=7) el patillaje táctil
// clásico es el 3 (YP=A1, XM=A2, YM=7, XP=6). La prueba con el patillaje 1
// descartó A3 como pin del táctil (lectura clavada en el máximo).
#if TOUCH_PINOUT == 1        // A3/A2 + 9/8 (algunos shields Mega 3.5")
  #define TOUCH_YP A3
  #define TOUCH_XM A2
  #define TOUCH_YM 9
  #define TOUCH_XP 8
#elif TOUCH_PINOUT == 2      // pareja invertida de la 1
  #define TOUCH_YP A2
  #define TOUCH_XM A3
  #define TOUCH_YM 8
  #define TOUCH_XP 9
#elif TOUCH_PINOUT == 3      // A1/A2 + 7/6 (clásico MCUFRIEND con WR=A1, RS=A2)
  #define TOUCH_YP A1
  #define TOUCH_XM A2
  #define TOUCH_YM 7
  #define TOUCH_XP 6
#else                        // pareja invertida de la 3
  #define TOUCH_YP A2
  #define TOUCH_XM A1
  #define TOUCH_YM 6
  #define TOUCH_XP 7
#endif

#define TOUCH_X_MIN 140
#define TOUCH_X_MAX 910
#define TOUCH_Y_MIN 150
#define TOUCH_Y_MAX 950

#define TOUCH_SWAP_XY  1
#define TOUCH_INVERT_X 0   // con rotation(3) (pantalla girada 180) el giro ya invierte X -> se anula el invert de antes
#define TOUCH_INVERT_Y 1   // idem para Y: con rotation(1) no hacia falta, con rotation(3) si

// Ignorar el táctil durante el arranque: mientras el TFT se inicializa los
// pines compartidos LCD/táctil flotan y producen "toques" fantasma. Este era
// el fallo que hacía arrancar en la página COMMS en vez de HOME.
#define TOUCH_MS_IGNORAR_ARRANQUE 1500

static TouchScreen ts(TOUCH_XP, TOUCH_YP, TOUCH_XM, TOUCH_YM, 300);

// Los datos de OBD2 vienen de info_obd2 (obd2.h) y los de GPRS de infoGPRS
// (gprs.h); ambos los actualiza su propio modulo en cada vuelta del loop().

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
static void dibujarCabeceraEstatica();
static void actualizarCabecera();
static void actualizarTestigoVida();
static void dibujarBarraPestanas();
static void dibujarUnaPestana(int indice, bool activa);
static void dibujarIconoPestana(int indice, int cx, int cy, uint16_t color);
static void limpiarAreaContenido();
static void actualizarCampoFloat(int x, int y, int w, float valorNuevo, float &valorAnt,
                                  int decimales, const char* sufijo, uint16_t color,
                                  bool valido, bool &validoAnt);
static void dibujarTarjeta(int x, int y, int w, int h, const char* etiqueta);
static void valorCentradoEnTarjeta(int cx, int y, const char* valor, const char* sufijo,
                                    uint8_t tam, uint16_t color);
static void dibujarBarra(int x, int y, int w, int h, float pct, uint16_t color);

static void cambiarPagina(Pagina nueva);
static void dibujarEstructuraPagina(Pagina p);

static void dibujarEstructuraPaginaHome();
static void actualizarPaginaHome(bool forzar);
static void dibujarEstructuraPaginaGPS();
static void actualizarPaginaGPS(bool forzar);
static void dibujarEstructuraPaginaDinamica();
static void actualizarPaginaDinamica(bool forzar);
static void dibujarEstructuraPaginaMotor();
static void actualizarPaginaMotor(bool forzar);
static void dibujarEstructuraPaginaComms();
static void actualizarPaginaComms(bool forzar);

static void gestionarTouch();

// =============================================================================
// API PÚBLICA
// =============================================================================
// Diagnóstico temporal: solo se ve azul (nada de rojo/verde) -> probable
// ID de controlador mal detectado (tabla de init incorrecta para este
// clon de ILI9486). TFT_TEST_BARRAS_RGB pinta tres franjas puras R/G/B
// para comprobar visualmente qué canal llega bien. Poner a 0 una vez
// resuelto el problema.
// NOTA: se probó forzar COLMOD (0x3A=0x55) vía WriteCmdData() y dejó la
// pantalla en negro total -> con este ID/driver ese método de escritura
// de bajo nivel no es compatible, no reintentar sin antes confirmar el
// ID real con el sketch de diagnóstico de MCUFRIEND_kbv.
#define TFT_TEST_BARRAS_RGB  0

void setupPantalla() {
    uint16_t ID = tft.readID();
    Serial.print(F("[TFT] Driver detectado: 0x")); Serial.println(ID, HEX);
    if (ID == 0xD3D3) ID = 0x9486; // fallback si la lectura falla
    tft.begin(ID);

    tft.setRotation(3);            // horizontal, 480x320, girada 180 respecto a rotation(1).
    tft.fillScreen(COLOR_FONDO);

#if TFT_TEST_BARRAS_RGB
    // Tres franjas puras: si solo aparece la de abajo (azul), confirma
    // que el byte alto (rojo + parte de verde) no está llegando al panel.
    tft.fillRect(0,   0, PANT_ANCHO, PANT_ALTO / 3, 0xF800); // rojo puro
    tft.fillRect(0, PANT_ALTO / 3, PANT_ANCHO, PANT_ALTO / 3, 0x07E0); // verde puro
    tft.fillRect(0, 2 * PANT_ALTO / 3, PANT_ANCHO, PANT_ALTO / 3, 0x001F); // azul puro
    delay(4000);
#endif

    dibujarCabeceraEstatica();
    dibujarBarraPestanas();
    dibujarEstructuraPagina(paginaActual); // dibuja la primera página (Home)
    actualizarPaginaHome(true); // primer pintado forzado: sin esto, "--" no aparece hasta cambiar de pestaña
}

void actualizarPantalla() {
    gestionarTouch();
    switch (paginaActual) {
        case PAG_HOME:  actualizarPaginaHome(false);     break;
        case PAG_GPS:   actualizarPaginaGPS(false);      break;
        case PAG_DIN:   actualizarPaginaDinamica(false); break;
        case PAG_MOTOR: actualizarPaginaMotor(false);    break;
        case PAG_COMMS: actualizarPaginaComms(false);    break;
    }
    actualizarCabecera();
}

// =============================================================================
// CABECERA
// =============================================================================
static void dibujarCabeceraEstatica() {
    tft.drawFastHLine(0, CABECERA_ALTO - 1, PANT_ANCHO, COLOR_BORDE);
}

static void actualizarCabecera() {
    actualizarTestigoVida();

    // Repinta como mucho una vez por segundo (evita parpadeo/coste innecesario).
    // OJO: antes este throttle miraba info_gps.segundo en vez de millis(), pero
    // info_gps.segundo solo se actualiza cuando actualizarGPS() se ejecuta, y
    // eso en el .ino esta condicionado a gps.location.isUpdated() -- que solo
    // se pone a true si hay fix. Sin fix (o antes del primer fix) segundo se
    // quedaba congelado, el throttle no volvia a disparar nunca, y TODA la
    // cabecera (hora, satelites y el estado GPRS) se quedaba pintada con lo
    // ultimo que hubiera, aunque el GPRS ya estuviera conectado.
    static unsigned long ultimoRepintado = 0;
    if (millis() - ultimoRepintado < 1000) return;
    ultimoRepintado = millis();

    tft.fillRect(20, 3, PANT_ANCHO - 40, CABECERA_ALTO - 6, COLOR_FONDO);

    // Fecha y hora grandes, centradas
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d  %02d:%02d:%02d",
             info_gps.dia, info_gps.mes, info_gps.anyo,
             info_gps.hora, info_gps.minuto, info_gps.segundo);
    tft.setTextColor(COLOR_TEXTO_PRIN);
    tft.setTextSize(2);
    tft.setCursor(100, 9);
    tft.print(buf);
    tft.setTextSize(1);
    tft.setCursor(346, 16);
    tft.setTextColor(COLOR_TEXTO_SEC);
    tft.print("Espana");

    // Satélites y GPRS a la derecha
    tft.setCursor(392, 13);
    tft.setTextColor(info_gps.n_satelites_en_uso >= 5 ? COLOR_OK : COLOR_ALERTA);
    tft.print(info_gps.n_satelites_en_uso); tft.print(" sat");

    tft.setCursor(444, 13);
    tft.setTextColor(infoGPRS.estado ? COLOR_OK : COLOR_ALERTA);
    tft.print(infoGPRS.estado ? "GPRS" : "----");
}

// ---- Testigo de vida: punto parpadeante en la esquina ----
static void actualizarTestigoVida() {
    static bool estado = false;
    static unsigned long ultimo = 0;
    if (millis() - ultimo < 500) return;
    ultimo = millis();
    estado = !estado;
    tft.fillCircle(10, 15, 4, estado ? COLOR_OK : COLOR_FONDO);
}

// =============================================================================
// BARRA DE PESTAÑAS (grande, con iconos)
// =============================================================================
static void dibujarBarraPestanas() {
    int y0 = PANT_ALTO - TABBAR_ALTO;
    tft.drawFastHLine(0, y0, PANT_ANCHO, COLOR_BORDE);
    for (int i = 0; i < 5; i++) {
        dibujarUnaPestana(i, (i == (int)paginaActual));
    }
}

static void dibujarUnaPestana(int indice, bool activa) {
    int anchoPestana = PANT_ANCHO / 5;
    int x0 = indice * anchoPestana;
    int y0 = PANT_ALTO - TABBAR_ALTO;

    tft.fillRect(x0, y0 + 1, anchoPestana, TABBAR_ALTO - 1,
                 activa ? COLOR_PANEL : COLOR_FONDO);

    uint16_t c = activa ? COLOR_TEXTO_PRIN : COLOR_TEXTO_SEC;
    dibujarIconoPestana(indice, x0 + anchoPestana / 2, y0 + 16, c);

    tft.setTextColor(c);
    tft.setTextSize(1);
    int textoX = x0 + (anchoPestana - (int)strlen(NOMBRES_PESTANA[indice]) * 6) / 2;
    tft.setCursor(textoX, y0 + 34);
    tft.print(NOMBRES_PESTANA[indice]);
}

// Iconos dibujados con primitivas GFX (sin bitmaps, no gastan flash/RAM)
static void dibujarIconoPestana(int indice, int cx, int cy, uint16_t color) {
    switch (indice) {
        case 0: // home: cuadrícula 2x2
            tft.drawRect(cx - 8, cy - 8, 7, 7, color);
            tft.drawRect(cx + 1, cy - 8, 7, 7, color);
            tft.drawRect(cx - 8, cy + 1, 7, 7, color);
            tft.drawRect(cx + 1, cy + 1, 7, 7, color);
            break;
        case 1: // gps: pin de localización
            tft.drawCircle(cx, cy - 3, 5, color);
            tft.drawLine(cx - 4, cy + 1, cx, cy + 9, color);
            tft.drawLine(cx + 4, cy + 1, cx, cy + 9, color);
            break;
        case 2: // dinam: triángulo (delta)
            tft.drawTriangle(cx, cy - 8, cx - 9, cy + 7, cx + 9, cy + 7, color);
            break;
        case 3: // motor: velocímetro
            tft.drawCircle(cx, cy, 8, color);
            tft.drawLine(cx, cy, cx + 5, cy - 5, color);
            break;
        case 4: // comms: arcos wifi
            tft.fillCircle(cx, cy + 6, 2, color);
            tft.drawCircleHelper(cx, cy + 6, 5, 0x3, color);
            tft.drawCircleHelper(cx, cy + 6, 9, 0x3, color);
            tft.drawCircleHelper(cx, cy + 6, 13, 0x3, color);
            break;
    }
}

// =============================================================================
// UTILIDADES COMUNES
// =============================================================================
static void limpiarAreaContenido() {
    tft.fillRect(0, CONTENIDO_Y0, PANT_ANCHO, CONTENIDO_Y1 - CONTENIDO_Y0, COLOR_FONDO);
}

// Dibuja un valor numérico solo si ha cambiado (evita parpadeo). Fondo negro.
// Si valido es false se pinta "--" en vez del numero (sin sufijo). Se
// redibuja si el valor cambio O si la validez cambio (para poder pasar de
// numero a "--" y viceversa aunque el numero de debajo no se haya movido).
static void actualizarCampoFloat(int x, int y, int w, float valorNuevo, float &valorAnt,
                                  int decimales, const char* sufijo, uint16_t color,
                                  bool valido, bool &validoAnt) {
    bool cambioValidez = (valido != validoAnt);
    if (!cambioValidez && (!valido || abs(valorNuevo - valorAnt) < 0.001)) return;

    tft.fillRect(x, y, w, 16, COLOR_FONDO);
    tft.setTextSize(2);
    tft.setCursor(x, y);
    if (valido) {
        tft.setTextColor(color);
        tft.print(valorNuevo, decimales);
        if (sufijo) { tft.setTextSize(1); tft.print(sufijo); }
    } else {
        tft.setTextColor(COLOR_TEXTO_SEC);
        tft.print("--");
    }
    valorAnt   = valorNuevo;
    validoAnt  = valido;
}

// Tarjeta rellena con esquinas redondeadas y título centrado (estilo Home)
static void dibujarTarjeta(int x, int y, int w, int h, const char* etiqueta) {
    tft.fillRoundRect(x, y, w, h, 8, COLOR_PANEL);
    tft.setTextColor(COLOR_ETIQUETA);
    tft.setTextSize(1);
    int tx = x + (w - (int)strlen(etiqueta) * 6) / 2;
    tft.setCursor(tx, y + 14);
    tft.print(etiqueta);
}

// Valor grande centrado dentro de una tarjeta (limpia sobre el gris del panel)
static void valorCentradoEnTarjeta(int cx, int y, const char* valor, const char* sufijo,
                                    uint8_t tam, uint16_t color) {
    int wValor = (int)strlen(valor) * 6 * tam;
    int wSufijo = sufijo ? (int)strlen(sufijo) * 6 + 2 : 0;
    tft.fillRect(cx - 95, y, 190, 8 * tam, COLOR_PANEL);
    tft.setTextColor(color);
    tft.setTextSize(tam);
    tft.setCursor(cx - (wValor + wSufijo) / 2, y);
    tft.print(valor);
    if (sufijo) {
        tft.setTextSize(1);
        tft.setCursor(tft.getCursorX() + 2, y + 8 * tam - 8);
        tft.print(sufijo);
    }
}

// Barra de progreso redondeada sobre pista gris (estilo Motor)
static void dibujarBarra(int x, int y, int w, int h, float pct, uint16_t color) {
    pct = constrain(pct, 0, 100);
    int lleno = (int)((float)w * pct / 100.0);
    tft.fillRoundRect(x, y, w, h, h / 2, COLOR_PISTA);
    if (lleno > h) tft.fillRoundRect(x, y, lleno, h, h / 2, color);
}

// =============================================================================
// NAVEGACIÓN ENTRE PÁGINAS
// =============================================================================
static void cambiarPagina(Pagina nueva) {
    if (nueva == paginaActual) return;
    paginaActual = nueva;
    dibujarBarraPestanas();
    dibujarEstructuraPagina(nueva);
    switch (paginaActual) {
        case PAG_HOME:  actualizarPaginaHome(true);     break;
        case PAG_GPS:   actualizarPaginaGPS(true);      break;
        case PAG_DIN:   actualizarPaginaDinamica(true); break;
        case PAG_MOTOR: actualizarPaginaMotor(true);    break;
        case PAG_COMMS: actualizarPaginaComms(true);    break;
    }
}

static void dibujarEstructuraPagina(Pagina p) {
    limpiarAreaContenido();
    switch (p) {
        case PAG_HOME:  dibujarEstructuraPaginaHome();     break;
        case PAG_GPS:   dibujarEstructuraPaginaGPS();      break;
        case PAG_DIN:   dibujarEstructuraPaginaDinamica(); break;
        case PAG_MOTOR: dibujarEstructuraPaginaMotor();    break;
        case PAG_COMMS: dibujarEstructuraPaginaComms();    break;
    }
}

// =============================================================================
// PAGINA HOME — 4 tarjetas rellenas con valores grandes centrados
// =============================================================================
#define HOME_TARJ_W 228
#define HOME_TARJ_H 112
#define HOME_X1 8
#define HOME_X2 244
#define HOME_Y1 (CONTENIDO_Y0 + 4)
#define HOME_Y2 (HOME_Y1 + HOME_TARJ_H + 8)
#define HOME_CX1 (HOME_X1 + HOME_TARJ_W / 2)
#define HOME_CX2 (HOME_X2 + HOME_TARJ_W / 2)
#define HOME_G_R 40  // radio del círculo guía de la bola de G

static void dibujarGuiaBolaG() {
    int cx = HOME_CX1, cy = HOME_Y2 + HOME_TARJ_H / 2 + 8;
    tft.drawCircle(cx, cy, HOME_G_R, COLOR_BORDE);
    tft.drawFastHLine(cx - HOME_G_R, cy, HOME_G_R * 2, COLOR_BORDE);
    tft.drawFastVLine(cx, cy - HOME_G_R, HOME_G_R * 2, COLOR_BORDE);
}

static void dibujarEstructuraPaginaHome() {
    dibujarTarjeta(HOME_X1, HOME_Y1, HOME_TARJ_W, HOME_TARJ_H, "velocidad");
    dibujarTarjeta(HOME_X2, HOME_Y1, HOME_TARJ_W, HOME_TARJ_H, "rpm motor");
    dibujarTarjeta(HOME_X1, HOME_Y2, HOME_TARJ_W, HOME_TARJ_H, "g lateral/long");
    dibujarTarjeta(HOME_X2, HOME_Y2, HOME_TARJ_W, HOME_TARJ_H, "temp neumatico");
    dibujarGuiaBolaG();
}

static void actualizarPaginaHome(bool forzar) {
    static float ant_vel = -1, ant_rpm = -1, ant_tireT = -1;
    static bool  antV_vel = false, antV_rpm = false, antV_tireT = false;
    static int   gx_ant = -1, gy_ant = -1;

    char buf[12];

    bool vVel = info_gps.valido, vRpm = info_obd2.valido, vTemp = info_temp.valido;
    if (forzar) {
        ant_vel = ant_rpm = ant_tireT = -1;
        // !v fuerza el primer pintado ("--" o numero) aunque v ya arranque en
        // false, ver el comentario equivalente en actualizarPaginaGPS().
        antV_vel = !vVel; antV_rpm = !vRpm; antV_tireT = !vTemp;
        gx_ant = gy_ant = -1;
    }

    if ((vVel != antV_vel) || (vVel && abs(info_gps.velocidad_kmh - ant_vel) >= 0.5)) {
        if (vVel) {
            snprintf(buf, sizeof(buf), "%d", (int)info_gps.velocidad_kmh);
            valorCentradoEnTarjeta(HOME_CX1, HOME_Y1 + 48, buf, " km/h", 4, COLOR_TEXTO_PRIN);
        } else {
            valorCentradoEnTarjeta(HOME_CX1, HOME_Y1 + 48, "--", NULL, 4, COLOR_TEXTO_SEC);
        }
        ant_vel = info_gps.velocidad_kmh;
        antV_vel = vVel;
    }

    if ((vRpm != antV_rpm) || (vRpm && abs(info_obd2.rpm - ant_rpm) >= 10)) {
        if (vRpm) {
            snprintf(buf, sizeof(buf), "%d", (int)info_obd2.rpm);
            valorCentradoEnTarjeta(HOME_CX2, HOME_Y1 + 48, buf, NULL, 4, COLOR_TEXTO_PRIN);
        } else {
            valorCentradoEnTarjeta(HOME_CX2, HOME_Y1 + 48, "--", NULL, 4, COLOR_TEXTO_SEC);
        }
        ant_rpm = info_obd2.rpm;
        antV_rpm = vRpm;
    }

    if ((vTemp != antV_tireT) || (vTemp && abs(info_temp.temperatura_objeto - ant_tireT) >= 0.5)) {
        if (vTemp) {
            uint16_t colorTemp = info_temp.temperatura_objeto > 60 ? COLOR_ALERTA
                               : info_temp.temperatura_objeto > 45 ? COLOR_AMBAR : COLOR_ACENTO;
            snprintf(buf, sizeof(buf), "%d", (int)info_temp.temperatura_objeto);
            valorCentradoEnTarjeta(HOME_CX2, HOME_Y2 + 48, buf, " C", 4, colorTemp);
        } else {
            valorCentradoEnTarjeta(HOME_CX2, HOME_Y2 + 48, "--", NULL, 4, COLOR_TEXTO_SEC);
        }
        ant_tireT = info_temp.temperatura_objeto;
        antV_tireT = vTemp;
    }

    // Bola de G (depende del acelerómetro): se refresca a ACCEL_REFRESCO_MS,
    // igual que la página Dinámica, para no saturar la pantalla con el IMU.
    static unsigned long ultimoRefrescoG = 0;
    if (!forzar && millis() - ultimoRefrescoG < ACCEL_REFRESCO_MS) return;
    ultimoRefrescoG = millis();

    // Sin lectura valida del IMU, la bola se queda congelada donde estaba en
    // vez de irse a una posicion calculada con datos viejos/basura.
    if (!accel.valido) return;

    // Bola de G: borrar posición anterior, repintar guía y dibujar la nueva
    int cx = HOME_CX1, cy = HOME_Y2 + HOME_TARJ_H / 2 + 8;
    int gx = cx + constrain((int)(accel.ax * 35), -(HOME_G_R - 6), HOME_G_R - 6);
    int gy = cy + constrain((int)(accel.ay * 35), -(HOME_G_R - 6), HOME_G_R - 6);
    if (gx != gx_ant || gy != gy_ant) {
        if (gx_ant != -1) tft.fillCircle(gx_ant, gy_ant, 4, COLOR_PANEL);
        dibujarGuiaBolaG(); // restaura la guía que el borrado pueda haber tapado
        tft.fillCircle(gx, gy, 4, COLOR_AMBAR);
        gx_ant = gx; gy_ant = gy;
    }
}

// =============================================================================
// PAGINA GPS
// =============================================================================
static void dibujarEstructuraPaginaGPS() {
    int y0 = CONTENIDO_Y0 + 16;
    const char* etiquetas[] = {"LATITUD", "LONGITUD", "ALTITUD", "VELOCIDAD GPS", "HDOP"};
    for (int i = 0; i < 5; i++) {
        tft.setTextColor(COLOR_TEXTO_SEC);
        tft.setTextSize(1);
        tft.setCursor(24, y0 + i * 32 + 4);
        tft.print(etiquetas[i]);
    }
}

static void actualizarPaginaGPS(bool forzar) {
    static float ant_lat = -1, ant_lon = -1, ant_alt = -1, ant_vel = -1, ant_hdop = -1;
    static bool  antV_lat = false, antV_lon = false, antV_alt = false, antV_vel = false, antV_hdop = false;

    int y0 = CONTENIDO_Y0 + 16;
    bool v = info_gps.valido;
    if (forzar) {
        // !v (no v) fuerza el primer pintado sea cual sea el estado de v:
        // si no se hace asi, cuando v empieza en false y antV_* tambien
        // arranca en false, el "no ha cambiado nada" hace que no se pinte
        // ni el numero ni el "--".
        ant_lat = ant_lon = ant_alt = ant_vel = ant_hdop = -1;
        antV_lat = antV_lon = antV_alt = antV_vel = antV_hdop = !v;
    }
    actualizarCampoFloat(220, y0,       220, info_gps.latitud,       ant_lat,  6, NULL,    COLOR_TEXTO_PRIN, v, antV_lat);
    actualizarCampoFloat(220, y0 + 32,  220, info_gps.longitud,      ant_lon,  6, NULL,    COLOR_TEXTO_PRIN, v, antV_lon);
    actualizarCampoFloat(220, y0 + 64,  220, info_gps.altitud_m,     ant_alt,  1, " m",    COLOR_TEXTO_PRIN, v, antV_alt);
    actualizarCampoFloat(220, y0 + 96,  220, info_gps.velocidad_kmh, ant_vel,  0, " km/h", COLOR_TEXTO_PRIN, v, antV_vel);
    uint16_t colorHdop = info_gps.hdop < 2 ? COLOR_OK : info_gps.hdop < 5 ? COLOR_AMBAR : COLOR_ALERTA;
    actualizarCampoFloat(220, y0 + 128, 220, info_gps.hdop,          ant_hdop, 1, NULL,    colorHdop, v, antV_hdop);
}

// =============================================================================
// PAGINA DINAMICA — horizonte artificial relleno (cielo/tierra) + valores IMU
// =============================================================================
#define HORIZ_CX 110
#define HORIZ_CY 150
#define HORIZ_R  64

// Dibuja el horizonte relleno: disco azul (cielo), tierra marrón por columnas
// y línea de horizonte blanca inclinada según roll y desplazada según pitch.
static void dibujarHorizonte(float rollDeg, float pitchDeg) {
    float pendiente = tan(rollDeg * PI / 180.0);
    int   despPitch = (int)(pitchDeg * 3);

    tft.fillCircle(HORIZ_CX, HORIZ_CY, HORIZ_R, COLOR_CIELO);

    for (int dx = -HORIZ_R; dx <= HORIZ_R; dx++) {
        int h  = (int)(sqrt((float)(HORIZ_R * HORIZ_R - dx * dx)) + 0.5);
        int yl = despPitch + (int)(pendiente * dx);
        if (yl < -h) yl = -h;
        if (yl >  h) yl =  h;
        if (yl < h) tft.drawFastVLine(HORIZ_CX + dx, HORIZ_CY + yl, h - yl + 1, COLOR_TIERRA);
        if (yl > -h && yl < h) {
            tft.drawPixel(HORIZ_CX + dx, HORIZ_CY + yl - 1, COLOR_TEXTO_PRIN);
            tft.drawPixel(HORIZ_CX + dx, HORIZ_CY + yl,     COLOR_TEXTO_PRIN);
        }
    }
    tft.drawCircle(HORIZ_CX, HORIZ_CY, HORIZ_R, COLOR_BORDE);
}

static void dibujarEstructuraPaginaDinamica() {
    const char* etiquetas[] = {"pitch", "roll", "ax", "ay", "az"};
    for (int i = 0; i < 5; i++) {
        tft.setTextColor(COLOR_TEXTO_SEC);
        tft.setTextSize(2);
        tft.setCursor(270, CONTENIDO_Y0 + 32 + i * 30);
        tft.print(etiquetas[i]);
    }
}

static void actualizarPaginaDinamica(bool forzar) {
    static float rollAnt = -1000, pitchAnt = -1000;
    static float ant_pitch = -1000, ant_roll = -1000, ant_ax = -1000, ant_ay = -1000, ant_az = -1000;
    static bool  antV_pitch = false, antV_roll = false, antV_ax = false, antV_ay = false, antV_az = false;

    // Limita la frecuencia de refresco del IMU: si no es un repintado forzado
    // (entrada en la página) y no ha pasado el intervalo, no toca nada.
    static unsigned long ultimoRefresco = 0;
    if (!forzar && millis() - ultimoRefresco < ACCEL_REFRESCO_MS) return;
    ultimoRefresco = millis();

    // Sin lectura valida del IMU se congela el horizonte tal como estaba (no
    // tiene sentido "vaciarlo") y los campos numericos pasan a "--".
    bool v = accel.valido;
    if (forzar) {
        rollAnt = pitchAnt = -1000;
        ant_pitch = ant_roll = ant_ax = ant_ay = ant_az = -1000;
        // !v fuerza el primer pintado ("--" o numero) aunque v ya arranque
        // en false, ver el comentario equivalente en actualizarPaginaGPS().
        antV_pitch = antV_roll = antV_ax = antV_ay = antV_az = !v;
    }
    if (v && (abs(accel.roll - rollAnt) > 0.5 || abs(accel.pitch - pitchAnt) > 0.5)) {
        dibujarHorizonte(accel.roll, accel.pitch);
        rollAnt = accel.roll; pitchAnt = accel.pitch;
    }

    int x0 = 352, yy = CONTENIDO_Y0 + 32;
    actualizarCampoFloat(x0, yy,       118, accel.pitch, ant_pitch, 1, NULL, COLOR_TEXTO_PRIN, v, antV_pitch);
    actualizarCampoFloat(x0, yy + 30,  118, accel.roll,  ant_roll,  1, NULL, COLOR_TEXTO_PRIN, v, antV_roll);
    actualizarCampoFloat(x0, yy + 60,  118, accel.ax,    ant_ax,    2, " g", COLOR_TEXTO_PRIN, v, antV_ax);
    actualizarCampoFloat(x0, yy + 90,  118, accel.ay,    ant_ay,    2, " g", COLOR_TEXTO_PRIN, v, antV_ay);
    actualizarCampoFloat(x0, yy + 120, 118, accel.az,    ant_az,    2, " g", COLOR_TEXTO_PRIN, v, antV_az);
}

// =============================================================================
// PAGINA MOTOR (OBD2) — reloj de RPM + barras de progreso redondeadas
// Datos reales de info_obd2 (obd2.h). Sin info_obd2.valido (ELM327 no
// detectado o sin conexion con el vehiculo), la aguja/barras se congelan tal
// como estaban y los numeros pasan a "--".
// =============================================================================
#define RPM_CX 64
#define RPM_CY (CONTENIDO_Y0 + 60)
#define RPM_R  36

static void dibujarEstructuraPaginaMotor() {
    tft.fillCircle(RPM_CX, RPM_CY, RPM_R, COLOR_PANEL);
    tft.drawCircle(RPM_CX, RPM_CY, RPM_R, COLOR_BORDE);

    tft.setTextColor(COLOR_TEXTO_SEC);
    tft.setTextSize(1);
    tft.setCursor(116, RPM_CY - 24);
    tft.print("rpm");
}

static void actualizarPaginaMotor(bool forzar) {
    static float ant_rpm = -1, ant_coolant = -1, ant_throttle = -1, ant_load = -1;
    static bool  antV_rpm = false, antV_coolant = false, antV_throttle = false, antV_load = false;

    bool v = info_obd2.valido;
    if (forzar) {
        ant_rpm = ant_coolant = ant_throttle = ant_load = -1;
        // !v fuerza el primer pintado ("--" o numero) aunque v ya arranque en
        // false, ver el comentario equivalente en actualizarPaginaGPS().
        antV_rpm = antV_coolant = antV_throttle = antV_load = !v;
    }

    // Reloj de RPM (0-7000 rpm en un arco de 270 grados) + valor grande al lado.
    // La aguja solo se mueve con lectura valida (se congela si no la hay).
    if (v && abs(info_obd2.rpm - ant_rpm) > 20) {
        tft.fillCircle(RPM_CX, RPM_CY, RPM_R - 2, COLOR_PANEL); // borra la aguja anterior
        float ang = map((long)info_obd2.rpm, 0, 7000, -135, 135) * PI / 180.0;
        tft.drawLine(RPM_CX, RPM_CY,
                     RPM_CX + (int)(sin(ang) * (RPM_R - 8)),
                     RPM_CY - (int)(cos(ang) * (RPM_R - 8)), COLOR_AMBAR);
    }
    if ((v != antV_rpm) || (v && abs(info_obd2.rpm - ant_rpm) > 20)) {
        char buf[8];
        tft.fillRect(116, RPM_CY - 12, 130, 24, COLOR_FONDO);
        tft.setTextSize(3);
        tft.setCursor(116, RPM_CY - 12);
        if (v) {
            snprintf(buf, sizeof(buf), "%d", (int)info_obd2.rpm);
            tft.setTextColor(COLOR_TEXTO_PRIN);
        } else {
            strcpy(buf, "--");
            tft.setTextColor(COLOR_TEXTO_SEC);
        }
        tft.print(buf);
        ant_rpm  = info_obd2.rpm;
        antV_rpm = v;
    }

    // --- Barras: etiqueta con valor + pista redondeada ---
    struct BarraDef { const char* nombre; float valor; const char* unidad; float pct; uint16_t color; float* ant; bool* antV; };
    BarraDef barras[3] = {
        {"refrigerante", info_obd2.temp_refrigerante, " C", (float)(info_obd2.temp_refrigerante / 1.2), // escala 0-120 C
         info_obd2.temp_refrigerante > 100 ? COLOR_ALERTA : COLOR_OK, &ant_coolant, &antV_coolant},
        {"acelerador",   info_obd2.pos_acelerador, " %", info_obd2.pos_acelerador, COLOR_AZUL,  &ant_throttle, &antV_throttle},
        {"carga motor",  info_obd2.carga_motor,    " %", info_obd2.carga_motor,    COLOR_AMBAR, &ant_load,     &antV_load},
    };
    for (int i = 0; i < 3; i++) {
        bool cambioValidez = (v != *barras[i].antV);
        if (!cambioValidez && (!v || abs(barras[i].valor - *barras[i].ant) <= 0.5)) continue;
        int yEtiq = CONTENIDO_Y0 + 108 + i * 44;

        tft.fillRect(16, yEtiq, 240, 10, COLOR_FONDO);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXTO_SEC);
        tft.setCursor(16, yEtiq);
        tft.print(barras[i].nombre);
        tft.setCursor(tft.getCursorX() + 4, yEtiq);
        if (v) {
            tft.setTextColor(COLOR_TEXTO_PRIN);
            tft.print(barras[i].valor, 0); tft.print(barras[i].unidad);
            dibujarBarra(16, yEtiq + 14, PANT_ANCHO - 32, 8, barras[i].pct, barras[i].color);
        } else {
            tft.setTextColor(COLOR_TEXTO_SEC);
            tft.print("--");
            dibujarBarra(16, yEtiq + 14, PANT_ANCHO - 32, 8, 0, barras[i].color);
        }
        *barras[i].ant  = barras[i].valor;
        *barras[i].antV = v;
    }
}

// =============================================================================
// PAGINA COMUNICACIONES (GPRS)
// Los valores se leen de infoGPRS (gprs.h), que mantiene actualizada
// gestionarGPRS() desde el loop() principal. Aquí solo se repinta el campo
// que ha cambiado respecto al refresco anterior.
// =============================================================================
static void dibujarEstructuraPaginaComms() {
    int y0 = CONTENIDO_Y0 + 24;
    const char* etiquetas[] = {"ESTADO", "RSSI", "PAQUETES ENVIADOS"};
    for (int i = 0; i < 3; i++) {
        tft.setTextColor(COLOR_TEXTO_SEC);
        tft.setTextSize(1);
        tft.setCursor(24, y0 + i * 40 + 4);
        tft.print(etiquetas[i]);
    }
}

static void actualizarPaginaComms(bool forzar) {
    static bool          estadoAnt    = true;  // distinto del valor inicial para forzar 1er pintado
    static int           ant_rssi     = -1;
    static unsigned long ant_paquetes = 0xFFFFFFFF;
    if (forzar) { estadoAnt = !infoGPRS.estado; ant_rssi = -1; ant_paquetes = 0xFFFFFFFF; }

    int pct = rssiPorcentaje();
    int y0  = CONTENIDO_Y0 + 24;

    if (infoGPRS.estado != estadoAnt) {
        tft.fillRect(220, y0, 240, 16, COLOR_FONDO);
        tft.setTextColor(infoGPRS.estado ? COLOR_OK : COLOR_ALERTA);
        tft.setTextSize(2);
        tft.setCursor(220, y0);
        tft.print(infoGPRS.estado ? "CONECTADO" : "SIN COBERTURA");
        estadoAnt = infoGPRS.estado;
    }
    if (pct != ant_rssi) {
        tft.fillRect(220, y0 + 40, 240, 16, COLOR_FONDO);
        tft.setTextSize(2);
        tft.setCursor(220, y0 + 40);
        // Sin cobertura el valor es 0, no un hueco: la señal nula es un dato.
        tft.setTextColor(pct == 0 ? COLOR_ALERTA : COLOR_TEXTO_PRIN);
        tft.print(pct); tft.print(" %");
        ant_rssi = pct;
    }
    if (infoGPRS.paquetesEnviados != ant_paquetes) {
        tft.fillRect(220, y0 + 80, 240, 16, COLOR_FONDO);
        tft.setTextColor(COLOR_TEXTO_PRIN);
        tft.setTextSize(2);
        tft.setCursor(220, y0 + 80);
        tft.print(infoGPRS.paquetesEnviados);
        ant_paquetes = infoGPRS.paquetesEnviados;
    }
}

// =============================================================================
// TACTIL
// =============================================================================
static void gestionarTouch() {
    // Los pines flotan durante el arranque y generan toques fantasma que
    // cambiaban de página nada más encender (aterrizaba en COMMS).
    if (millis() < TOUCH_MS_IGNORAR_ARRANQUE) return;

    TSPoint p = ts.getPoint();
    // TouchScreen deja los pines XM/YP como entrada; hay que devolverlos
    // a su uso normal para que la pantalla siga funcionando bien.
    pinMode(TOUCH_XM, OUTPUT);
    pinMode(TOUCH_YP, OUTPUT);

    bool tocando = (p.z > 200 && p.z < 1000);

    // Anti-rebote: exigir 2 lecturas válidas consecutivas (~100 ms con el
    // delay(50) del loop) antes de aceptar el toque. Filtra picos de ruido.
    static uint8_t muestrasToque = 0;
    if (!tocando) { muestrasToque = 0; return; }
    if (muestrasToque < 250) muestrasToque++;
    if (muestrasToque != 2) return;   // solo actúa una vez por pulsación

    // El panel entrega coordenadas en orientación nativa (vertical); con la
    // pantalla en horizontal (rotation 1) hay que intercambiar los ejes.
    int rawX = TOUCH_SWAP_XY ? p.y : p.x;
    int rawY = TOUCH_SWAP_XY ? p.x : p.y;
    int xPantalla = map(rawX, TOUCH_X_MIN, TOUCH_X_MAX, 0, PANT_ANCHO);
    int yPantalla = map(rawY, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, PANT_ALTO);
    if (TOUCH_INVERT_X) xPantalla = PANT_ANCHO - xPantalla;
    if (TOUCH_INVERT_Y) yPantalla = PANT_ALTO - yPantalla;
    xPantalla = constrain(xPantalla, 0, PANT_ANCHO - 1);
    yPantalla = constrain(yPantalla, 0, PANT_ALTO - 1);
    

    if (yPantalla >= PANT_ALTO - TABBAR_ALTO) {
        int indicePestana = xPantalla / (PANT_ANCHO / 5);
        indicePestana = constrain(indicePestana, 0, 4);
        cambiarPagina((Pagina)indicePestana);
    }
}
