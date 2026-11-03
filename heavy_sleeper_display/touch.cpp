/*
 * touch.cpp — Touch XPT2046 em barramento SPI PROPRIO (biblioteca
 *             XPT2046_Touchscreen, de Paul Stoffregen).
 *
 * POR QUE NAO USAR tft.getTouch():
 *   No board E32R28T o XPT2046 NAO compartilha o SPI da LCD. Ele tem
 *   CLK=25, MOSI=32, MISO=39, CS=33 e IRQ=36 — pinos totalmente diferentes
 *   dos 14/13/12/15 da tela. A TFT_eSPI assume barramento compartilhado, e
 *   por isso o getTouch() dela simplesmente nao funciona aqui.
 *
 * QUAL BARRAMENTO:
 *   A TFT_eSPI usa o VSPI (ver Processors/TFT_eSPI_ESP32.c — sem
 *   USE_HSPI_PORT no User_Setup.h, ela instancia SPIClass(VSPI)). O objeto
 *   global "SPI" do Arduino tambem e VSPI. Portanto o touch usa o HSPI,
 *   remapeado pela matriz de GPIO para 25/39/32/33. Os pinos "nativos" do
 *   HSPI (12-15) continuam livres para a tela.
 *
 * CALIBRACAO (3 pontos):
 *   Dois pontos em diagonal so corrigem escala e inversao — nao detectam
 *   TROCA de eixos. Com 3 pontos (canto sup-esq, sup-dir e inf-esq) da para
 *   descobrir qual eixo bruto acompanha o X da tela e qual acompanha o Y,
 *   alem dos limites de cada um. O resultado cabe nos mesmos 5 uint16_t que
 *   o storage.cpp ja grava na NVS, entao nada muda na camada de Preferences:
 *     cal[0]=xmin  cal[1]=xmax  cal[2]=ymin  cal[3]=ymax
 *     cal[4]=0xCA00 | swap   (assinatura + flag de troca de eixos)
 *   A assinatura serve para ignorar blobs antigos, gravados pela TFT_eSPI.
 */
#include <TFT_eSPI.h>
#include "touch.h"
#include "storage.h"
#include "ui.h"

/* ── SPI dedicado do touch ─────────────────────────────────────────── */
static SPIClass touchSPI(HSPI);

/*
 * SEM o pino de IRQ, de proposito (v0.4).
 * Errata do ESP32: com o Wi-Fi ligado, IO36 e IO39 recebem pulsos falsos
 * de ~80 ns. A biblioteca liga uma interrupcao FALLING no IRQ (IO36) e,
 * com o radio no ar, ela dispara sem parar -> o watchdog de interrupcao
 * reinicia a placa (rst:0x8 TG1WDT_SYS_RESET) logo que o Wi-Fi sobe.
 * Sem IRQ a biblioteca le o XPT2046 por SPI a cada touchUpdate() (20 ms),
 * o que da no mesmo para o despertador. O fio do IRQ pode continuar ligado.
 */
XPT2046_Touchscreen touch(PIN_TOUCH_CS);

/* ── Calibracao corrente ───────────────────────────────────────────── */
#define CAL_MAGIC     0xCA00   // marca os 8 bits altos de cal[4]
#define CAL_MAGIC_MSK 0xFF00
#define CAL_MIN_SPAN  500      // faixa bruta minima para a calibracao valer

static int  calXmin = TOUCH_RAW_MIN, calXmax = TOUCH_RAW_MAX;
static int  calYmin = TOUCH_RAW_MIN, calYmax = TOUCH_RAW_MAX;
static bool calSwap = false;   // true = raw Y acompanha o X da tela

/* Estado da maquina de eventos. */
static bool          available     = false;
static bool          isDown        = false;
static bool          longFired     = false;
static unsigned long downMs        = 0;
static unsigned long lastReleaseMs = 0;
static unsigned long lastPollMs    = 0;
static int           curX = 0, curY = 0;
static int           pressX = 0, pressY = 0;

static bool pendingTap  = false;
static bool pendingLong = false;

/* ═══════════════════════════════════════════════════════════════════
   Leitura bruta e mapeamento
   ═══════════════════════════════════════════════════════════════════ */

/* Converte a leitura bruta do XPT2046 em pixels, aplicando a calibracao. */
static void mapRaw(int rawX, int rawY, int &x, int &y) {
  int ax = calSwap ? rawY : rawX;   // eixo bruto que vira X da tela
  int ay = calSwap ? rawX : rawY;   // eixo bruto que vira Y da tela

  x = map(ax, calXmin, calXmax, 0, SCR_W - 1);
  y = map(ay, calYmin, calYmax, 0, SCR_H - 1);
  x = constrain(x, 0, SCR_W - 1);
  y = constrain(y, 0, SCR_H - 1);
}

/* True se ha dedo na tela; devolve a posicao ja em pixels. */
static bool rawTouch(int &x, int &y) {
  if (!touch.touched()) return false;
  TS_Point p = touch.getPoint();     // dentro de 3 ms reaproveita a amostra
  mapRaw(p.x, p.y, x, y);
  return true;
}

/* ═══════════════════════════════════════════════════════════════════
   Calibracao
   ═══════════════════════════════════════════════════════════════════ */

/* Guarda a calibracao corrente no formato uint16_t[5] da NVS. */
static void calPack(uint16_t cal[5]) {
  cal[0] = (uint16_t)constrain(calXmin, 0, 4095);
  cal[1] = (uint16_t)constrain(calXmax, 0, 4095);
  cal[2] = (uint16_t)constrain(calYmin, 0, 4095);
  cal[3] = (uint16_t)constrain(calYmax, 0, 4095);
  cal[4] = CAL_MAGIC | (calSwap ? 1 : 0);
}

/* Le um blob da NVS para a calibracao corrente. False se nao for nosso. */
static bool calUnpack(const uint16_t cal[5]) {
  if ((cal[4] & CAL_MAGIC_MSK) != CAL_MAGIC) return false;   // blob antigo
  if (abs((int)cal[1] - (int)cal[0]) < CAL_MIN_SPAN) return false;
  if (abs((int)cal[3] - (int)cal[2]) < CAL_MIN_SPAN) return false;

  calXmin = cal[0];
  calXmax = cal[1];
  calYmin = cal[2];
  calYmax = cal[3];
  calSwap = (cal[4] & 1) != 0;
  return true;
}

/* Volta a faixa generica do XPT2046 (usada se a calibracao falhar). */
static void calDefaults() {
  calXmin = TOUCH_RAW_MIN; calXmax = TOUCH_RAW_MAX;
  calYmin = TOUCH_RAW_MIN; calYmax = TOUCH_RAW_MAX;
  calSwap = false;
}

/* Mira em cruz no ponto de calibracao. */
static void drawCrosshair(int px, int py, uint16_t color) {
  tft.drawFastHLine(px - 12, py, 25, color);
  tft.drawFastVLine(px, py - 12, 25, color);
  tft.drawCircle(px, py, 7, color);
}

/*
 * Mostra a mira e espera um toque, devolvendo a media das leituras brutas.
 * Bloqueia — mas so roda no boot, antes de qualquer alarme. False se o
 * usuario nao tocar dentro de 30 s.
 */
static bool calReadPoint(int px, int py, int &rawX, int &rawY) {
  drawCrosshair(px, py, C_ACCENT);

  // Garante que o dedo do ponto anterior ja saiu da tela.
  while (touch.touched()) delay(20);
  delay(150);

  unsigned long start = millis();
  while (!touch.touched()) {
    if (millis() - start > 30000UL) {
      drawCrosshair(px, py, C_DANGER);
      return false;
    }
    delay(20);
  }

  long sumX = 0, sumY = 0;
  int  n = 0;
  while (touch.touched() && n < 64) {
    TS_Point p = touch.getPoint();
    sumX += p.x;
    sumY += p.y;
    n++;
    delay(15);
  }
  if (n < 3) {
    drawCrosshair(px, py, C_DANGER);
    return false;
  }

  rawX = (int)(sumX / n);
  rawY = (int)(sumY / n);
  drawCrosshair(px, py, C_OK);
  Serial.printf("[TOUCH] cal (%d,%d) -> raw %d,%d (%d amostras)\n",
                px, py, rawX, rawY, n);
  return true;
}

/*
 * Carrega a calibracao da NVS (force=false) ou executa a rotina de 3 pontos
 * na tela (force=true). Devolve true se ha calibracao valida no fim.
 */
bool touchCalibrate(bool force) {
  uint16_t cal[5];

  if (!force) {
    if (storageLoadTouchCal(cal) && calUnpack(cal)) {
      Serial.printf("[TOUCH] calibracao da NVS: x %d..%d  y %d..%d  swap=%d\n",
                    calXmin, calXmax, calYmin, calYmax, calSwap);
      return true;
    }
    return false;
  }

  /* Pontos de referencia: sup-esq, sup-dir e inf-esq. */
  const int P0X = 30,  P0Y = 40;
  const int P1X = 210, P1Y = 40;
  const int P2X = 30,  P2Y = 280;

  uiDrawCalibrationPrompt();
  delay(1800);
  tft.fillScreen(C_BG);

  int r0x, r0y, r1x, r1y, r2x, r2y;
  bool ok = calReadPoint(P0X, P0Y, r0x, r0y) &&
            calReadPoint(P1X, P1Y, r1x, r1y) &&
            calReadPoint(P2X, P2Y, r2x, r2y);

  if (!ok) {
    Serial.println(F("[TOUCH] calibracao incompleta -> usando padrao"));
    calDefaults();
    uiToast("Calib. padrao em uso", C_WARN);
    return false;
  }

  // Movimento horizontal na tela (P0->P1): o eixo bruto que mais variou
  // e o que acompanha o X da tela.
  calSwap = abs(r1y - r0y) > abs(r1x - r0x);

  int a0 = calSwap ? r0y : r0x;   // eixo bruto do X da tela
  int a1 = calSwap ? r1y : r1x;
  int b0 = calSwap ? r0x : r0y;   // eixo bruto do Y da tela
  int b2 = calSwap ? r2x : r2y;

  // Extrapola das duas medidas para as bordas reais da tela.
  float slopeX = (float)(a1 - a0) / (float)(P1X - P0X);
  float slopeY = (float)(b2 - b0) / (float)(P2Y - P0Y);

  int xmin = (int)lroundf(a0 - slopeX * P0X);
  int xmax = (int)lroundf(a0 + slopeX * (SCR_W - 1 - P0X));
  int ymin = (int)lroundf(b0 - slopeY * P0Y);
  int ymax = (int)lroundf(b0 + slopeY * (SCR_H - 1 - P0Y));

  if (abs(xmax - xmin) < CAL_MIN_SPAN || abs(ymax - ymin) < CAL_MIN_SPAN) {
    Serial.println(F("[TOUCH] calibracao inconsistente -> usando padrao"));
    calDefaults();
    uiToast("Calib. padrao em uso", C_WARN);
    return false;
  }

  calXmin = xmin; calXmax = xmax;
  calYmin = ymin; calYmax = ymax;

  calPack(cal);
  storageSaveTouchCal(cal);
  Serial.printf("[TOUCH] calibrado: x %d..%d  y %d..%d  swap=%d\n",
                calXmin, calXmax, calYmin, calYmax, calSwap);
  return true;
}

/* ═══════════════════════════════════════════════════════════════════
   Init
   ═══════════════════════════════════════════════════════════════════ */

/*
 * Sobe o SPI do touch nos pinos corretos e carrega (ou refaz) a calibracao.
 * O touchSPI.begin() com pinos explicitos tem que vir ANTES de touch.begin():
 * a biblioteca chama _pspi->begin() sem argumentos, e o core do ESP32 ignora
 * essa segunda chamada porque o barramento ja esta de pe.
 */
void touchBegin() {
  calDefaults();

  touchSPI.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  if (!touch.begin(touchSPI)) {
    available = false;
    Serial.println(F("[TOUCH] falha ao iniciar o XPT2046 — seguindo so como visor"));
    return;
  }
  touch.setRotation(0);
  available = true;

  if (!touchCalibrate(false)) {
    Serial.println(F("[TOUCH] sem calibracao valida -> iniciando rotina"));
    touchCalibrate(true);
  }
}

/* Alias com o nome usado no documento de hardware. */
void touchInit() { touchBegin(); }

/* True se o touch esta operacional. */
bool touchAvailable() { return available; }

/* ═══════════════════════════════════════════════════════════════════
   Maquina de eventos
   ═══════════════════════════════════════════════════════════════════ */

/*
 * Atualiza a maquina de eventos. Chame a cada volta do loop.
 * O "tap" e emitido na SOLTURA para nao disparar junto com o toque longo.
 */
void touchUpdate() {
  if (!available) return;

  // 20 ms de intervalo ja e de sobra e mantem o SPI do touch quieto.
  if (millis() - lastPollMs < 20) return;
  lastPollMs = millis();

  int x, y;
  bool down = rawTouch(x, y);

  if (down) {
    curX = x; curY = y;
    if (!isDown) {
      if (millis() - lastReleaseMs < TOUCH_DEBOUNCE_MS) return;  // debounce
      isDown    = true;
      longFired = false;
      downMs    = millis();
      pressX    = x;
      pressY    = y;
    } else if (!longFired && (millis() - downMs >= TOUCH_LONGPRESS_MS)) {
      longFired   = true;
      pendingLong = true;
    }
  } else if (isDown) {
    isDown        = false;
    lastReleaseMs = millis();
    if (!longFired) pendingTap = true;   // toque curto
  }
}

/* True (uma unica vez) se houve um toque curto; devolve onde. */
bool touchTap(int &x, int &y) {
  if (!pendingTap) return false;
  pendingTap = false;
  x = pressX;
  y = pressY;
  return true;
}

/* True (uma unica vez) se houve um toque longo; devolve onde. */
bool touchLongPress(int &x, int &y) {
  if (!pendingLong) return false;
  pendingLong = false;
  x = pressX;
  y = pressY;
  return true;
}

/* Posicao atual do dedo, se houver contato. */
bool getTouchPoint(int &x, int &y) {
  if (!available || !isDown) return false;
  x = curX;
  y = curY;
  return true;
}

/* Limpa eventos pendentes (usado ao trocar de tela). */
void touchFlush() {
  pendingTap  = false;
  pendingLong = false;
}

/* True se o ponto (tx,ty) cai dentro do retangulo do botao. */
bool isTouched(const Button& btn, int tx, int ty) {
  return isTouched(btn.x, btn.y, btn.w, btn.h, tx, ty);
}

/* Mesma coisa, com o retangulo solto. */
bool isTouched(int bx, int by, int bw, int bh, int tx, int ty) {
  return tx >= bx && tx < bx + bw &&
         ty >= by && ty < by + bh;
}
