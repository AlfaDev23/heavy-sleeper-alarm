/*
 * ui.cpp — Desenho de todas as telas e tabela de botoes clicaveis.
 *
 * Cada tela, ao ser desenhada, registra seus botoes em curBtns[]. O hit-test
 * e o piscar de feedback trabalham em cima dessa tabela, entao layout e
 * area clicavel nunca saem de sincronia.
 *
 * Rotulos de botao sao guardados por PONTEIRO (o piscar redesenha o botao
 * depois). Por isso todo rotulo montado com snprintf usa um buffer static.
 */
#include "ui.h"
#include "touch.h"
#include "clock.h"
#include "storage.h"
#include "challenges.h"
#include "comm.h"
#include "audio.h"
#include "net.h"
#include "wiz.h"
#include "night.h"
#include "app.h"
#include "pwm.h"

TFT_eSPI tft = TFT_eSPI();

/* ── Tabela de botoes da tela corrente ──────────────────────────── */
struct UiButton {
  Button   b;
  uint16_t bg, border, fg;
  uint8_t  font;
  int      action;
};

#define MAX_BTNS 24
static UiButton curBtns[MAX_BTNS];
static int      curBtnCount = 0;
static Screen   curScreen   = SCREEN_CLOCK;
static bool     nightOn     = false;

/* Estado do piscar de feedback. */
static int           flashIdx   = -1;
static unsigned long flashUntil = 0;

/* Cache para redesenhar so o que mudou na tela do relogio. */
static int  lastMin = -1, lastHour = -1, lastSec = -1;
static bool lastOnline = false;
static const char* lastNetName = NULL;
static const char* lastWizName = NULL;
static bool lastSunrise = false;

/* Caches das telas de alarme (zerados sempre que a tela e redesenhada). */
static char ringLastLine[42] = "";
static int  ringLastMin      = -1;
static int  holdLastPx       = -1;
static char chalLastLine[42] = "";

/* ───────────────────────── primitivas ──────────────────────────── */

/* Desenha um botao com rotulo centralizado. */
static void drawButton(const UiButton& u) {
  tft.fillRoundRect(u.b.x, u.b.y, u.b.w, u.b.h, 6, u.bg);
  tft.drawRoundRect(u.b.x, u.b.y, u.b.w, u.b.h, 6, u.border);
  if (u.b.label && u.b.label[0]) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(u.fg, u.bg);
    tft.drawString(u.b.label, u.b.x + u.b.w / 2, u.b.y + u.b.h / 2, u.font);
    tft.setTextDatum(TL_DATUM);
  }
}

/* Registra e desenha um botao na tabela da tela atual. */
static void addButton(int x, int y, int w, int h, const char* label,
                      int action, uint16_t bg = C_BTN,
                      uint16_t border = C_LINE, uint16_t fg = C_TEXT,
                      uint8_t font = 2) {
  if (curBtnCount >= MAX_BTNS) return;
  UiButton& u = curBtns[curBtnCount++];
  u.b.x = x; u.b.y = y; u.b.w = w; u.b.h = h; u.b.label = label;
  u.bg = bg; u.border = border; u.fg = fg; u.font = font; u.action = action;
  drawButton(u);
}

/* Botao de opcao exclusiva (selecionado = verde). */
static void addChoice(int x, int y, int w, int h, const char* label,
                      int action, bool sel) {
  addButton(x, y, w, h, label, action,
            sel ? C_BTN_SEL : C_BTN, sel ? C_OK : C_LINE);
}

/* Escreve um texto alinhado a esquerda. */
static void textAt(int x, int y, const char* s, uint16_t color,
                   uint16_t bg, uint8_t font = 2) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, bg);
  tft.drawString(s, x, y, font);
}

/* Escreve um texto centralizado em toda a largura. */
static void textCentered(int y, const char* s, uint16_t color,
                         uint16_t bg, uint8_t font = 2) {
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(color, bg);
  tft.drawString(s, SCR_W / 2, y, font);
  tft.setTextDatum(TL_DATUM);
}

/* Centraliza usando a fonte 4, caindo para a 2 se o texto nao couber. */
static void textCenteredFit(int y, const char* s, uint16_t color, uint16_t bg) {
  uint8_t font = (tft.textWidth(s, 4) <= SCR_W - 8) ? 4 : 2;
  textCentered(y, s, color, bg, font);
}

/* Registra uma area clicavel sem desenhar nada (label nulo = nao pisca). */
static void addHitArea(int x, int y, int w, int h, int action) {
  if (curBtnCount >= MAX_BTNS) return;
  UiButton& u = curBtns[curBtnCount++];
  u.b.x = x; u.b.y = y; u.b.w = w; u.b.h = h; u.b.label = NULL;
  u.bg = C_BG; u.border = C_BG; u.fg = C_TEXT; u.font = 2; u.action = action;
}

/* Barra de nivel com 8 segmentos. */
static void drawLevelBar(int x, int y, int filled, uint16_t activeColor) {
  for (int i = 0; i < 8; i++) {
    tft.fillRect(x + i * 11, y, 9, 8, (i < filled) ? activeColor : C_LINE);
  }
}

/* Cabecalho padrao das telas de configuracao. */
static void drawTitleBar(const char* title) {
  tft.fillRect(0, 0, SCR_W, 30, C_HEADER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_HEADER);
  tft.drawString(title, SCR_W / 2, 15, 2);
  tft.setTextDatum(TL_DATUM);
  tft.drawFastHLine(0, 30, SCR_W, C_LINE);
}

/* Uma linha "rotulo ......... valor". */
static void infoLine(int y, const char* label, const char* value, uint16_t vcolor) {
  tft.fillRect(0, y, SCR_W, 18, C_BG);
  textAt(8, y, label, C_SUBTEXT, C_BG, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(vcolor, C_BG);
  tft.drawString(value, SCR_W - 8, y, 2);
  tft.setTextDatum(TL_DATUM);
}

/* Barra horizontal de progresso (0..1). */
static void drawProgress(int x, int y, int w, int h, float p, uint16_t color) {
  p = constrain(p, 0.0f, 1.0f);
  int fw = (int)(w * p);
  tft.fillRect(x, y, fw, h, color);
  tft.fillRect(x + fw, y, w - fw, h, C_LINE);
}

/* ─────────────────────────── init ──────────────────────────────── */

/* Inicia a tela e o PWM do backlight. */
void uiBegin() {
  tft.init();
  tft.setRotation(0);                 // retrato 240x320
  tft.fillScreen(C_BG);

  // O TFT_eSPI ja configurou PIN_BL como saida digital em init();
  // so depois disso anexamos o LEDC para poder variar o brilho.
  pwmAttach(PIN_BL, LEDC_CH_BL, 5000, 8);
  uiSetBacklight(BL_STANDBY);
}

/* Ajusta o brilho do backlight (0-255). */
void uiSetBacklight(uint8_t duty) {
  pwmWrite(PIN_BL, LEDC_CH_BL, duty);
}

void uiSetNight(bool night) { nightOn = night; }
bool uiIsNight()            { return nightOn; }

/* ─────────────────────── tela do relogio ───────────────────────── */

/* Desenha hora grande e segundos. */
static void drawClockTime() {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", rtc.hour, rtc.minute);
  tft.fillRect(0, 38, SCR_W, 76, C_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(buf, SCR_W / 2, 42, 7);       // font 7 = 7 segmentos 48 px

  char sbuf[6];
  snprintf(sbuf, sizeof(sbuf), ":%02d", rtc.second);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_SUBTEXT, C_BG);
  tft.drawString(sbuf, SCR_W - 6, 95, 2);
  tft.setTextDatum(TL_DATUM);
}

/* Faixa com o horario do alarme e de quando a luz comeca a acender. */
static void drawClockAlarmBar() {
  bool sunrise = appSunriseActive();
  uint16_t bg = sunrise ? 0x4A00 : C_PANEL;       // ambar escuro no amanhecer
  tft.fillRect(0, 115, SCR_W, 30, bg);

  char buf[40];
  if (sunrise) {
    snprintf(buf, sizeof(buf), "Amanhecendo... alarme %02d:%02d",
             settings.almHour, settings.almMin);
    tft.setTextColor(TFT_YELLOW, bg);
  } else if (settings.almEnabled) {
    int luz = (settings.almHour * 60 + settings.almMin - settings.rampMin + 1440) % 1440;
    snprintf(buf, sizeof(buf), "Alarme %02d:%02d  luz %02d:%02d",
             settings.almHour, settings.almMin, luz / 60, luz % 60);
    tft.setTextColor(C_ACCENT, bg);
  } else {
    snprintf(buf, sizeof(buf), "ALARME DESATIVADO");
    tft.setTextColor(C_SUBTEXT, bg);
  }
  tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, SCR_W / 2, 130, 2);
  tft.setTextDatum(TL_DATUM);
  tft.drawFastHLine(0, 145, SCR_W, C_LINE);
  lastSunrise = sunrise;
}

/* Texto da linha WI-FI. Um literal so, porque o tick compara ponteiros. */
static const char* const PORTAL_TXT = "PORTAL ABERTO";
static const char* netLineText() {
  return netApActive() && !netConnected() ? PORTAL_TXT : netStateName();
}

static void drawNetLine() {
  const char* n = netLineText();
  infoLine(190, "WI-FI", n, netConnected() ? C_OK : C_WARN);
  lastNetName = n;
}

static void drawWizLine() {
  const char* w = wizStatusName();
  infoLine(210, "LAMPADA", w, wizOnline() ? C_OK : C_WARN);
  lastWizName = w;
}

static void drawActuatorLine() {
  bool online = commActuatorOnline();
  infoLine(230, "ATUADOR", online ? "ONLINE" : "OFFLINE",
           online ? C_OK : C_DANGER);
  lastOnline = online;
}

/* Painel com o resumo da configuracao e o estado das conexoes. */
static void drawClockInfo() {
  static char d[28];
  snprintf(d, sizeof(d), "%s - %s", typeName(settings.diffType),
           diffName(settings.diffLevel));
  infoLine(150, "DESAFIO", d, C_TEXT);

  char t[16];
  snprintf(t, sizeof(t), "%d s", settings.chalTimeS);
  infoLine(170, "RESOLVER EM", t, C_TEXT);

  drawNetLine();
  drawWizLine();
  drawActuatorLine();
}

/* Rodape com as barras de estimulo e a versao. */
static void drawClockFooter() {
  tft.fillRect(0, 250, SCR_W, SCR_H - 250, C_PANEL);
  tft.drawFastHLine(0, 249, SCR_W, C_LINE);

  textAt(8, 256, "SONS", C_SUBTEXT, C_PANEL, 1);
  drawLevelBar(46, 255, audioGetLevel() * 2, C_ACCENT);

  textAt(8, 272, "LUZ",  C_SUBTEXT, C_PANEL, 1);
  drawLevelBar(46, 271, (settings.lightPct * 8) / 100, TFT_YELLOW);

  textAt(8, 288, "VIB",  C_SUBTEXT, C_PANEL, 1);
  drawLevelBar(46, 287, settings.vibMode * 4, C_WARN);

  char buf[56];
  snprintf(buf, sizeof(buf), "rampa %d min", settings.rampMin);
  textAt(150, 272, buf, C_SUBTEXT, C_PANEL, 1);

  snprintf(buf, sizeof(buf), "v%s | toque longo = ajustes", FW_VERSION);
  textAt(8, 306, buf, C_LINE, C_PANEL, 1);
}

/* Tela principal completa (tema normal). */
static void drawDayClock() {
  tft.fillScreen(C_BG);

  // Cabecalho — tocar aqui abre a configuracao do alarme.
  tft.fillRect(0, 0, SCR_W, 36, C_HEADER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_HEADER);
  tft.drawString("ALARME", SCR_W / 2, 17, 4);
  tft.setTextDatum(TL_DATUM);
  textAt(6, 11, "*", C_TEXT, C_HEADER, 2);         // icone de brilho
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_TEXT, C_HEADER);
  tft.drawString("))", SCR_W - 6, 11, 2);          // icone de volume
  tft.setTextDatum(TL_DATUM);
  tft.drawFastHLine(0, 36, SCR_W, C_LINE);

  drawClockTime();
  tft.drawFastHLine(0, 114, SCR_W, C_LINE);
  drawClockAlarmBar();
  drawClockInfo();
  drawClockFooter();

  // Areas clicaveis invisiveis sobre o cabecalho e sobre a hora.
  addHitArea(0,  0, SCR_W, 36, ACT_OPEN_ALARM_CFG);
  addHitArea(0, 38, SCR_W, 76, ACT_OPEN_SET_TIME);
}

/* ── tema noturno: so a hora, grande e vermelha ───────────────────
   Horas e minutos empilhados em fonte 7 (7 segmentos) com escala 2:
   cada par de digitos tem ~128 x 96 px e da para ler da cama.        */
static void drawNightTime() {
  char hh[4], mm[4];
  snprintf(hh, sizeof(hh), "%02d", rtc.hour);
  snprintf(mm, sizeof(mm), "%02d", rtc.minute);
  tft.fillRect(0, 24, SCR_W, 228, C_BG);
  tft.setTextSize(2);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_NIGHT, C_BG);
  tft.drawString(hh, SCR_W / 2, 30, 7);
  tft.drawString(mm, SCR_W / 2, 146, 7);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
}

static void drawNightAlarmLine() {
  char buf[32];
  if (settings.almEnabled)
    snprintf(buf, sizeof(buf), "alarme %02d:%02d", settings.almHour, settings.almMin);
  else
    snprintf(buf, sizeof(buf), "alarme desligado");
  tft.fillRect(0, 268, SCR_W, 30, C_BG);
  textCentered(274, buf, C_NIGHT_DIM, C_BG, 2);
}

static void drawNightClock() {
  tft.fillScreen(C_BG);
  drawNightTime();
  drawNightAlarmLine();
  addHitArea(0, 0, SCR_W, SCR_H, ACT_NIGHT_PEEK);   // qualquer toque espia
}

static void drawClockScreen() {
  if (nightOn) drawNightClock();
  else         drawDayClock();
  lastHour = rtc.hour; lastMin = rtc.minute; lastSec = rtc.second;
}

/* Atualiza apenas o que muda a cada segundo na tela do relogio. */
void uiClockTick() {
  if (curScreen != SCREEN_CLOCK) return;

  if (nightOn) {
    if (rtc.hour != lastHour || rtc.minute != lastMin) {
      drawNightTime();
      lastHour = rtc.hour; lastMin = rtc.minute;
    }
    return;
  }

  if (rtc.hour != lastHour || rtc.minute != lastMin) {
    drawClockTime();
    lastHour = rtc.hour; lastMin = rtc.minute; lastSec = rtc.second;
  } else if (rtc.second != lastSec) {
    char sbuf[6];
    snprintf(sbuf, sizeof(sbuf), ":%02d", rtc.second);
    tft.fillRect(SCR_W - 60, 94, 54, 18, C_BG);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_SUBTEXT, C_BG);
    tft.drawString(sbuf, SCR_W - 6, 95, 2);
    tft.setTextDatum(TL_DATUM);
    lastSec = rtc.second;
  }

  if (appSunriseActive() != lastSunrise) drawClockAlarmBar();
  if (netLineText() != lastNetName) drawNetLine();
  if (wizStatusName() != lastWizName) drawWizLine();
  if (commActuatorOnline() != lastOnline) drawActuatorLine();
}

/* ───────────────────────── hub de ajustes ──────────────────────── */

static void drawSettingsScreen() {
  tft.fillScreen(C_BG);
  drawTitleBar("AJUSTES");

  addButton(20,  40, 200, 40, "CONFIG. ALARME", ACT_GO_ALARM_CFG, C_BTN, C_ACCENT);
  addButton(20,  86, 200, 40, "MODO NOTURNO",   ACT_GO_NIGHT_CFG, C_BTN, C_ACCENT);
  addButton(20, 132, 200, 40, "REDE E LAMPADA", ACT_GO_NETWORK,   C_BTN, C_ACCENT);
  addButton(20, 178, 200, 40, "AJUSTAR HORA",   ACT_GO_SET_TIME,  C_BTN, C_ACCENT);
  addButton(20, 224, 200, 40, "TESTAR ALARME",  ACT_TEST_ALARM,   C_BTN, C_WARN);
  addButton(20, 274, 200, 40, "VOLTAR",         ACT_BACK,         C_HEADER, C_LINE);
}

/* ──────────────────── configuracao do alarme ───────────────────── */

/* Linha "rotulo [-] valor [+]". */
static void spinRow(int y, int h, const char* label, const char* value,
                    int actDown, int actUp) {
  textAt(6, y + (h - 16) / 2, label, C_SUBTEXT, C_BG, 2);
  addButton(96,  y, 34, h, "-", actDown, C_BTN, C_LINE, C_TEXT, 4);
  tft.fillRect(132, y, 62, h, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(value, 163, y + h / 2, 4);
  tft.setTextDatum(TL_DATUM);
  addButton(196, y, 34, h, "+", actUp, C_BTN, C_LINE, C_TEXT, 4);
}

/* Valor grande centralizado em uma caixa. */
static void valueBox(int cx, int y, int w, int h, const char* v) {
  tft.fillRect(cx - w / 2, y, w, h, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(v, cx, y + h / 2, 4);
  tft.setTextDatum(TL_DATUM);
}

static void drawAlarmConfigScreen() {
  tft.fillScreen(C_BG);
  drawTitleBar("CONFIG. ALARME");

  // Hora e minuto na mesma linha:  [-] 07 [+]  :  [-] 00 [+]
  char buf[8];
  addButton(6,   34, 30, 34, "-", ACT_ALM_H_DN, C_BTN, C_LINE, C_TEXT, 4);
  snprintf(buf, sizeof(buf), "%02d", settings.almHour);
  valueBox(57, 34, 38, 34, buf);
  addButton(78,  34, 30, 34, "+", ACT_ALM_H_UP, C_BTN, C_LINE, C_TEXT, 4);
  valueBox(120, 34, 20, 34, ":");
  addButton(132, 34, 30, 34, "-", ACT_ALM_M_DN, C_BTN, C_LINE, C_TEXT, 4);
  snprintf(buf, sizeof(buf), "%02d", settings.almMin);
  valueBox(183, 34, 38, 34, buf);
  addButton(204, 34, 30, 34, "+", ACT_ALM_M_UP, C_BTN, C_LINE, C_TEXT, 4);

  textAt(6, 78, "ALARME", C_SUBTEXT, C_BG, 2);
  addChoice(96, 72, 138, 28, settings.almEnabled ? "ATIVO" : "INATIVO",
            ACT_ALM_TOGGLE, settings.almEnabled);

  textAt(6, 104, "DIFICULDADE", C_SUBTEXT, C_BG, 1);
  static const char* DL[4] = { "FACIL", "MEDIO", "DIFIC", "EXTR" };
  for (int i = 0; i < 4; i++)
    addChoice(6 + i * 58, 116, 54, 28, DL[i], ACT_DIFF_0 + i, settings.diffLevel == i);

  textAt(6, 148, "DESAFIO", C_SUBTEXT, C_BG, 1);
  static const char* TL_[4] = { "MAT", "MEM", "SEQ", "REA" };
  for (int i = 0; i < 4; i++)
    addChoice(6 + i * 58, 160, 54, 28, TL_[i], ACT_TYPE_0 + i, settings.diffType == i);

  snprintf(buf, sizeof(buf), "%d%%", settings.lightPct);
  spinRow(192, 28, "LUZ MAX", buf, ACT_LIGHT_DN, ACT_LIGHT_UP);

  static const char* VIBN[3] = { "VIB: OFF", "VIB: FRACA", "VIB: FORTE" };
  addChoice(6, 224, 112, 28, VIBN[constrain(settings.vibMode, 0, 2)],
            ACT_VIB_CYCLE, settings.vibMode != VIB_MODE_OFF);

  static char rampLbl[20];
  snprintf(rampLbl, sizeof(rampLbl), "RAMPA %d min", settings.rampMin);
  addButton(122, 224, 112, 28, rampLbl, ACT_RAMP_CYCLE, C_BTN, TFT_YELLOW);

  static char chLbl[32];
  snprintf(chLbl, sizeof(chLbl), "TEMPO P/ RESOLVER: %d s", settings.chalTimeS);
  addButton(6, 256, 228, 28, chLbl, ACT_CHTIME_CYCLE, C_BTN, C_ACCENT);

  addButton(6,   290, 112, 28, "SALVAR", ACT_SAVE, C_BTN_SEL, C_OK);
  addButton(122, 290, 112, 28, "VOLTAR", ACT_BACK, C_HEADER, C_LINE);
}

/* ──────────────────────── modo noturno ─────────────────────────── */

/* Linha "rotulo [-] HH:MM [+]" (passos de 30 min). */
static void timeRow(int y, int h, const char* label, int minuteOfDay,
                    int actDown, int actUp) {
  textAt(6, y + (h - 16) / 2, label, C_SUBTEXT, C_BG, 2);
  addButton(90, y, 32, h, "-", actDown, C_BTN, C_LINE, C_TEXT, 4);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", minuteOfDay / 60, minuteOfDay % 60);
  valueBox(161, y, 74, h, buf);
  addButton(200, y, 34, h, "+", actUp, C_BTN, C_LINE, C_TEXT, 4);
}

static void drawNightConfigScreen() {
  tft.fillScreen(C_BG);
  drawTitleBar("MODO NOTURNO");

  textAt(6, 40, "MODO", C_SUBTEXT, C_BG, 1);
  static const char* NM[3] = { "AUTO", "DIA", "NOITE" };
  for (int i = 0; i < 3; i++)
    addChoice(6 + i * 77, 52, 74, 36, NM[i], ACT_NIGHT_MODE_0 + i,
              settings.nightMode == i);

  timeRow(102, 36, "COMECA",  settings.nightStart, ACT_NIGHT_S_DN, ACT_NIGHT_S_UP);
  timeRow(146, 36, "TERMINA", settings.nightEnd,   ACT_NIGHT_E_DN, ACT_NIGHT_E_UP);

  bool active = nightShouldBeActive();
  char buf[40];
  snprintf(buf, sizeof(buf), "Agora: %s", active ? "MODO NOTURNO" : "MODO DIA");
  textCentered(196, buf, active ? C_NIGHT : C_ACCENT, C_BG, 2);
#ifdef PIN_LDR
  textCentered(216, "AUTO segue o sensor de luz (IO35)", C_SUBTEXT, C_BG, 1);
#else
  textCentered(216, "AUTO segue o horario acima", C_SUBTEXT, C_BG, 1);
#endif
  textCentered(232, "A noite: so a hora, em vermelho.", C_SUBTEXT, C_BG, 1);
  textCentered(246, "Um toque mostra tudo por 10 s.", C_SUBTEXT, C_BG, 1);

  addButton(6,   276, 112, 36, "SALVAR", ACT_SAVE, C_BTN_SEL, C_OK);
  addButton(122, 276, 112, 36, "VOLTAR", ACT_BACK, C_HEADER, C_LINE);
}

/* ─────────────────────── rede e lampada ────────────────────────── */

static char netSig[96] = "";      // "assinatura" do que esta na tela

/* Monta um texto que muda sempre que algo exibido muda. */
static void networkSignature(char* out, size_t n) {
  snprintf(out, n, "%d|%d|%d|%s|%s|%d|%d|%s", netState(), netApActive(),
           netApClients(), netIp().toString().c_str(), wizTargetIp(),
           wizOnline(), wizDiscovering(), rtc.synced ? "s" : "m");
}

static void drawNetworkScreen() {
  tft.fillScreen(C_BG);
  drawTitleBar("REDE E LAMPADA");

  char buf[40];
  infoLine(36, "WI-FI", netStateName(), netConnected() ? C_OK : C_WARN);
  char ssid[34];
  if (!netcfg.ssid[0])               snprintf(ssid, sizeof(ssid), "(nenhuma)");
  else if (strlen(netcfg.ssid) > 20) snprintf(ssid, sizeof(ssid), "%.18s..", netcfg.ssid);
  else                               snprintf(ssid, sizeof(ssid), "%s", netcfg.ssid);
  infoLine(54, "REDE", ssid, C_TEXT);
  infoLine(72, "IP", netConnected() ? netIp().toString().c_str() : "--", C_TEXT);

  if (netApActive()) {
    infoLine(94,  "PORTAL", netApSsid(), C_ACCENT);
    infoLine(112, "SENHA",  AP_PASSWORD, C_ACCENT);
    infoLine(130, "ABRIR",  netApIp().toString().c_str(), C_ACCENT);
  } else if (netConnected()) {
    snprintf(buf, sizeof(buf), "%s.local", NET_HOSTNAME);
    infoLine(94, "PORTAL", buf, C_ACCENT);
    textCentered(116, "abra no celular, na mesma rede", C_SUBTEXT, C_BG, 1);
  }

  infoLine(152, "LAMPADA", wizStatusName(), wizOnline() ? C_OK : C_WARN);
  if (wizTargetIp()[0]) {
    snprintf(buf, sizeof(buf), "%s %s", wizTargetIp(),
             wizTargetIsFixed() ? "(fixo)" : "(auto)");
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  infoLine(170, "IP LAMP.", buf, C_TEXT);
  infoLine(188, "HORA", rtc.synced ? "PELA INTERNET" : "MANUAL",
           rtc.synced ? C_OK : C_WARN);

  addButton(6,   220, 112, 30, netApActive() ? "FECHAR PORTAL" : "ABRIR PORTAL",
            ACT_NET_PORTAL, C_BTN, C_ACCENT);
  addButton(122, 220, 112, 30, "PROCURAR LAMP.", ACT_NET_WIZ_FIND, C_BTN, C_ACCENT);
  addButton(6,   254, 228, 30, "TESTAR LAMPADA", ACT_NET_WIZ_TEST, C_BTN, TFT_YELLOW);
  addButton(6,   288, 228, 28, "VOLTAR",         ACT_BACK, C_HEADER, C_LINE);

  networkSignature(netSig, sizeof(netSig));
}

void uiNetworkTick() {
  if (curScreen != SCREEN_NETWORK) return;
  char sig[96];
  networkSignature(sig, sizeof(sig));
  if (strcmp(sig, netSig) != 0) uiDrawScreen(SCREEN_NETWORK);
}

/* ───────────────────── ajuste da hora atual ────────────────────── */

static void drawSetTimeScreen() {
  tft.fillScreen(C_BG);
  drawTitleBar("AJUSTAR HORA");

  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", rtc.hour, rtc.minute, rtc.second);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString(buf, SCR_W / 2, 40, 6);      // font 6 = 48 px (digitos e ":")
  tft.setTextDatum(TL_DATUM);
  if (rtc.synced)
    textCentered(94, "sincronizada pela internet", C_WARN, C_BG, 1);

  const int H = 44;
  textAt(8, 110 + (H - 16) / 2, "HORA", C_SUBTEXT, C_BG, 2);
  addButton(110, 110, 52, H, "-", ACT_T_H_DN, C_BTN, C_LINE, C_TEXT, 4);
  addButton(176, 110, 52, H, "+", ACT_T_H_UP, C_BTN, C_LINE, C_TEXT, 4);

  textAt(8, 160 + (H - 16) / 2, "MIN", C_SUBTEXT, C_BG, 2);
  addButton(110, 160, 52, H, "-", ACT_T_M_DN, C_BTN, C_LINE, C_TEXT, 4);
  addButton(176, 160, 52, H, "+", ACT_T_M_UP, C_BTN, C_LINE, C_TEXT, 4);

  textAt(8, 210 + (H - 16) / 2, "SEG", C_SUBTEXT, C_BG, 2);
  addButton(110, 210, 52, H, "-", ACT_T_S_DN, C_BTN, C_LINE, C_TEXT, 4);
  addButton(176, 210, 52, H, "+", ACT_T_S_UP, C_BTN, C_LINE, C_TEXT, 4);

  addButton(20, 266, 200, 44, "CONFIRMAR", ACT_T_CONFIRM, C_BTN_SEL, C_OK);
}

/* ──────────────────── alarme tocando (fase 1) ──────────────────── */

static void drawRingHeader(bool alt) {
  uint16_t bg = alt ? C_WARN : C_DANGER;
  tft.fillRect(0, 0, SCR_W, 32, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, bg);
  tft.drawString(alt ? "!!! ACORDE !!!" : "!!! ALARME !!!", SCR_W / 2, 16, 4);
  tft.setTextDatum(TL_DATUM);
}

static void drawDismissButton() {
  tft.fillRoundRect(DISMISS_X, DISMISS_Y, DISMISS_W, DISMISS_H, 10, C_BTN_SEL);
  tft.drawRoundRect(DISMISS_X, DISMISS_Y, DISMISS_W, DISMISS_H, 10, C_OK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_BTN_SEL);
  tft.drawString("DESLIGAR", SCR_W / 2, DISMISS_Y + 34, 4);
  tft.setTextColor(C_SUBTEXT, C_BTN_SEL);
  tft.drawString("segure 2 segundos", SCR_W / 2, DISMISS_Y + 62, 2);
  tft.setTextDatum(TL_DATUM);
  drawProgress(DISMISS_X + 14, DISMISS_Y + DISMISS_H - 22, DISMISS_W - 28, 10, 0, C_OK);
  holdLastPx = 0;
}

static void drawRingingScreen() {
  tft.fillScreen(C_BG);
  drawRingHeader(false);
  tft.fillRect(0, 34, SCR_W, 24, C_PANEL);

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", rtc.hour, rtc.minute);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(buf, SCR_W / 2, 66, 7);
  tft.setTextDatum(TL_DATUM);

  textCentered(120, "Desligue e resolva o desafio", C_SUBTEXT, C_BG, 2);
  ringLastLine[0] = '\0';
  ringLastMin     = rtc.minute;

  drawDismissButton();
  addHitArea(DISMISS_X, DISMISS_Y, DISMISS_W, DISMISS_H, ACT_DISMISS_HOLD);

  static char snoozeLbl[24];
  snprintf(snoozeLbl, sizeof(snoozeLbl), "SONECA (+%lu min)", SNOOZE_MS / 60000UL);
  if (SNOOZE_MS < 60000UL) snprintf(snoozeLbl, sizeof(snoozeLbl), "SONECA (+%lu s)", SNOOZE_MS / 1000UL);
  addButton(20, 260, 200, 48, snoozeLbl, ACT_SNOOZE, C_HEADER, C_WARN, C_TEXT, 2);
}

void uiRingingUpdate(int stage, unsigned long ringMs, int nextLevel, bool flash) {
  if (curScreen != SCREEN_ALARM_RINGING) return;

  static bool lastFlash = false;
  if (stage >= 3 && flash != lastFlash) {
    drawRingHeader(flash);
    lastFlash = flash;
  }

  char line[42];
  unsigned long secs = ringMs / 1000UL;
  snprintf(line, sizeof(line), "Estagio %d   %02lu:%02lu   nivel %s",
           stage, secs / 60, secs % 60, diffName(nextLevel));
  if (strcmp(line, ringLastLine) != 0) {
    tft.fillRect(0, 34, SCR_W, 24, C_PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ACCENT, C_PANEL);
    tft.drawString(line, SCR_W / 2, 45, 2);
    tft.setTextDatum(TL_DATUM);
    strncpy(ringLastLine, line, sizeof(ringLastLine) - 1);
    ringLastLine[sizeof(ringLastLine) - 1] = '\0';
  }

  if (rtc.minute != ringLastMin) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", rtc.hour, rtc.minute);
    tft.fillRect(0, 62, SCR_W, 54, C_BG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawString(buf, SCR_W / 2, 66, 7);
    tft.setTextDatum(TL_DATUM);
    ringLastMin = rtc.minute;
  }
}

void uiRingHoldProgress(float p) {
  if (curScreen != SCREEN_ALARM_RINGING) return;
  int px = (int)(constrain(p, 0.0f, 1.0f) * 100);
  if (px == holdLastPx) return;
  holdLastPx = px;
  drawProgress(DISMISS_X + 14, DISMISS_Y + DISMISS_H - 22, DISMISS_W - 28, 10,
               p, p >= 1.0f ? C_ACCENT : C_OK);
}

/* ───────────────────── desafio (fase 2) ────────────────────────── */

/* Calcula a grade das alternativas conforme a quantidade. */
static void optionGeometry(int count, int& cols, int& w, int& h, int& gx, int& gy) {
  if (count <= 2)      { cols = 2; w = 110; h = 68; gx = 8; gy = 150; }
  else if (count <= 4) { cols = 2; w = 110; h = 68; gx = 8; gy = 124; }
  else if (count <= 6) { cols = 3; w = 74;  h = 68; gx = 6; gy = 124; }
  else                 { cols = 3; w = 74;  h = 44; gx = 6; gy = 124; }
}

/* Desenha as alternativas do desafio e as registra como botoes. */
static void drawOptions() {
  const Challenge& c = challengeCurrent();
  int cols, w, h, gx, gy;
  optionGeometry(c.optCount, cols, w, h, gx, gy);

  const int stepX = w + 4;
  const int stepY = h + 6;

  // Na fase de espera da REACAO os botoes ficam ocultos.
  bool hidden = (c.type == CHAL_REACTION && c.phase == CHP_SHOW);

  tft.fillRect(0, 120, SCR_W, 150, C_BG);

  if (hidden) {
    textCentered(180, "AGUARDE...", C_WARN, C_BG, 4);
    // Area invisivel: tocar aqui antes da hora e queimar a largada.
    addHitArea(0, 120, SCR_W, 150, ACT_EARLY_TAP);
    return;
  }

  for (int i = 0; i < c.optCount; i++) {
    int col = i % cols;
    int row = i / cols;
    int x   = gx + col * stepX;
    int y   = gy + row * stepY;

    uint16_t bg     = c.colored ? c.optColor[i] : C_BTN;
    uint16_t border = C_LINE;
    uint16_t fg     = C_TEXT;

    // Durante a exibicao da MEMORIA so a cor da vez fica acesa.
    if (c.type == CHAL_MEMORY && c.phase == CHP_SHOW) {
      if (i == c.highlight) { border = C_TEXT; }
      else                  { bg = C_PANEL; }
    }
    if (c.colored && bg == 0xFFE0) fg = C_BG;   // texto escuro sobre amarelo

    addButton(x, y, w, h, c.optLabel[i], ACT_OPT_BASE + i, bg, border, fg, 4);
  }
}

static unsigned long chalLastSec = 0xFFFFFFFF;

/* Contador e barra de tempo no rodape. */
static void drawCountdown(unsigned long remainMs, unsigned long totalMs) {
  unsigned long s = (remainMs + 999UL) / 1000UL;     // arredonda para cima
  float p = totalMs ? (float)remainMs / (float)totalMs : 0;
  uint16_t col = p > 0.5f ? C_OK : (p > 0.2f ? C_WARN : C_DANGER);

  char buf[24];
  snprintf(buf, sizeof(buf), "TEMPO: %lu:%02lu", s / 60, s % 60);
  tft.fillRect(0, 274, SCR_W, 46, C_PANEL);
  textCentered(278, buf, col, C_PANEL, 2);
  drawProgress(20, 300, 200, 12, p, col);
  chalLastSec = s;
}

static void drawChallengeHeader() {
  tft.fillRect(0, 0, SCR_W, 32, C_HEADER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_ACCENT, C_HEADER);
  tft.drawString("DESAFIO", SCR_W / 2, 16, 4);
  tft.setTextDatum(TL_DATUM);
}

static void drawChallengeScreen() {
  const Challenge& c = challengeCurrent();
  tft.fillScreen(C_BG);
  drawChallengeHeader();
  tft.fillRect(0, 34, SCR_W, 24, C_PANEL);

  textCenteredFit(64, c.prompt, c.promptColor, C_BG);
  textCentered(100, c.hint, C_SUBTEXT, C_BG, 2);

  drawOptions();
  chalLastSec     = 0xFFFFFFFF; // forca o contador no proximo update
  chalLastLine[0] = '\0';
}

void uiChallengeUpdate(unsigned long remainMs, unsigned long totalMs) {
  if (curScreen != SCREEN_ALARM_ACTIVE) return;
  const Challenge& c = challengeCurrent();

  // Linha de status: tentativa, progresso e nivel.
  char line[42];
  if (c.expectedLen > 1) {
    snprintf(line, sizeof(line), "Tentativa %d   %d/%d   %s",
             challengeAttempts(), c.entered, c.expectedLen, diffName(c.level));
  } else {
    snprintf(line, sizeof(line), "Tentativa %d   %s",
             challengeAttempts(), diffName(c.level));
  }
  if (strcmp(line, chalLastLine) != 0) {
    tft.fillRect(0, 34, SCR_W, 24, C_PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ACCENT, C_PANEL);
    tft.drawString(line, SCR_W / 2, 45, 2);
    tft.setTextDatum(TL_DATUM);
    strncpy(chalLastLine, line, sizeof(chalLastLine) - 1);
    chalLastLine[sizeof(chalLastLine) - 1] = '\0';
  }

  // O contador para de andar na tela de resultado.
  bool resolved = (c.phase == CHP_RIGHT || c.phase == CHP_WRONG);
  if (!resolved && (remainMs + 999UL) / 1000UL != chalLastSec)
    drawCountdown(remainMs, totalMs);

  if (!c.redraw) return;
  challengeClearRedraw();

  // Enunciado + alternativas (a fase pode ter mudado o texto).
  tft.fillRect(0, 60, SCR_W, 58, C_BG);
  textCenteredFit(64, c.prompt, c.promptColor, C_BG);
  textCentered(100, c.hint, C_SUBTEXT, C_BG, 2);

  curBtnCount = 0;                      // rearma a tabela de botoes
  flashIdx    = -1;                     // indices antigos deixam de valer
  if (c.phase == CHP_RIGHT) {
    tft.fillRect(0, 120, SCR_W, 150, C_BG);
    textCentered(170, "CORRETO!", C_OK, C_BG, 4);
    textCentered(206, "alarme desligado. bom dia!", C_SUBTEXT, C_BG, 2);
  } else if (c.phase == CHP_WRONG) {
    tft.fillRect(0, 120, SCR_W, 150, C_BG);
    textCentered(160, c.timedOut ? "TEMPO ESGOTADO" : "ERROU", C_DANGER, C_BG, 4);
    textCentered(196, "o alarme vai tocar de novo", C_SUBTEXT, C_BG, 2);
  } else {
    drawOptions();
  }
}

/* ──────────────────────── troca de tela ────────────────────────── */

void uiDrawScreen(Screen s) {
  curScreen   = s;
  curBtnCount = 0;
  flashIdx    = -1;

  switch (s) {
    case SCREEN_CLOCK:         drawClockScreen();       break;
    case SCREEN_SETTINGS:      drawSettingsScreen();    break;
    case SCREEN_SET_TIME:      drawSetTimeScreen();     break;
    case SCREEN_ALARM_CONFIG:  drawAlarmConfigScreen(); break;
    case SCREEN_NIGHT_CONFIG:  drawNightConfigScreen(); break;
    case SCREEN_NETWORK:       drawNetworkScreen();     break;
    case SCREEN_ALARM_RINGING: drawRingingScreen();     break;
    case SCREEN_ALARM_ACTIVE:  drawChallengeScreen();   break;
  }
}

/* Devolve a acao do botao sob o ponto, ou ACT_NONE. */
int uiHitTest(int x, int y) {
  for (int i = 0; i < curBtnCount; i++) {
    if (isTouched(curBtns[i].b, x, y)) return curBtns[i].action;
  }
  return ACT_NONE;
}

/* Pisca o botao correspondente a acao por 100 ms. */
void uiFlashAction(int action) {
  for (int i = 0; i < curBtnCount; i++) {
    if (curBtns[i].action != action) continue;
    if (!curBtns[i].b.label) return;          // area invisivel: nao pisca
    UiButton hi = curBtns[i];
    hi.bg     = C_ACCENT;
    hi.fg     = C_BG;
    hi.border = C_TEXT;
    drawButton(hi);
    flashIdx   = i;
    flashUntil = millis() + 100;
    return;
  }
}

/* Restaura o botao quando os 100 ms terminam (chamada no loop). */
void uiFlashUpdate() {
  if (flashIdx < 0) return;
  if ((long)(millis() - flashUntil) < 0) return;
  if (flashIdx < curBtnCount) drawButton(curBtns[flashIdx]);
  flashIdx = -1;
}

/* ──────────────────────────── avisos ───────────────────────────── */

/* Mensagem rapida na base da tela. */
void uiToast(const char* msg, uint16_t color) {
  tft.fillRect(0, SCR_H - 22, SCR_W, 22, C_PANEL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, C_PANEL);
  tft.drawString(msg, SCR_W / 2, SCR_H - 11, 2);
  tft.setTextDatum(TL_DATUM);
}

/* Instrucoes exibidas antes da calibracao do touch. */
void uiDrawCalibrationPrompt() {
  tft.fillScreen(C_BG);
  textCentered(90,  "CALIBRACAO DO TOUCH", C_ACCENT, C_BG, 4);
  textCentered(140, "Toque nas setas que", C_TEXT, C_BG, 2);
  textCentered(160, "aparecerem nos cantos.", C_TEXT, C_BG, 2);
  textCentered(200, "Isso e feito uma unica vez.", C_SUBTEXT, C_BG, 2);
}
