/*
 * ui.cpp — Desenho de todas as telas e tabela de botoes clicaveis.
 *
 * Cada tela, ao ser desenhada, registra seus botoes em curBtns[]. O hit-test
 * e o piscar de feedback trabalham em cima dessa tabela, entao layout e
 * area clicavel nunca saem de sincronia.
 */
#include "ui.h"
#include "touch.h"
#include "clock.h"
#include "storage.h"
#include "challenges.h"
#include "comm.h"
#include "audio.h"
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

/* Estado do piscar de feedback. */
static int           flashIdx   = -1;
static unsigned long flashUntil = 0;

/* Cache para redesenhar so o que mudou na tela do relogio. */
static int  lastMin = -1, lastHour = -1, lastSec = -1;
static bool lastOnline = false;

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

/* Faixa com o horario do alarme (ou aviso de disparo). */
void uiClockAlarmBar(bool ringing, bool flash) {
  uint16_t bg = (ringing && flash) ? C_DANGER : C_PANEL;
  tft.fillRect(0, 115, SCR_W, 30, bg);

  char buf[36];
  if (ringing) {
    snprintf(buf, sizeof(buf), "%s",
             flash ? "!!! ALARME DISPARANDO !!!" : ">> Resolva o desafio <<");
    tft.setTextColor(C_TEXT, bg);
  } else if (settings.almEnabled) {
    snprintf(buf, sizeof(buf), "Alarme %02d:%02d  [ATIVO]",
             settings.almHour, settings.almMin);
    tft.setTextColor(C_ACCENT, bg);
  } else {
    snprintf(buf, sizeof(buf), "ALARME DESATIVADO");
    tft.setTextColor(C_SUBTEXT, bg);
  }
  tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, SCR_W / 2, 130, 2);
  tft.setTextDatum(TL_DATUM);
  tft.drawFastHLine(0, 145, SCR_W, C_LINE);
}

/* Uma linha "rotulo ......... valor" do painel de informacoes. */
static void infoLine(int y, const char* label, const char* value, uint16_t vcolor) {
  tft.fillRect(0, y, SCR_W, 18, C_BG);
  textAt(8, y, label, C_SUBTEXT, C_BG, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(vcolor, C_BG);
  tft.drawString(value, SCR_W - 8, y, 2);
  tft.setTextDatum(TL_DATUM);
}

/* Painel com o resumo da configuracao e o estado do Actuator. */
static void drawClockInfo() {
  char buf[16];
  infoLine(150, "DESAFIO",  typeName(settings.diffType),  C_TEXT);
  infoLine(170, "NIVEL",    diffName(settings.diffLevel), C_TEXT);

  snprintf(buf, sizeof(buf), "%d%%", settings.lightPct);
  infoLine(190, "LUZ", buf, C_TEXT);

  static const char* VIBN[3] = { "OFF", "FRACA", "FORTE" };
  infoLine(210, "VIBRACAO", VIBN[constrain(settings.vibMode, 0, 2)], C_TEXT);

  bool online = commActuatorOnline();
  infoLine(230, "ATUADOR", online ? "ONLINE" : "OFFLINE",
           online ? C_OK : C_DANGER);
  lastOnline = online;
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

  char buf[48];
  snprintf(buf, sizeof(buf), "HeavySleep %s | toque longo = ajustes", FW_VERSION);
  textAt(8, 306, buf, C_LINE, C_PANEL, 1);
}

/* Tela principal completa. */
static void drawClockScreen() {
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
  uiClockAlarmBar(false, false);
  drawClockInfo();
  drawClockFooter();

  // Areas clicaveis invisiveis sobre o cabecalho e sobre a hora.
  addHitArea(0,  0, SCR_W, 36, ACT_OPEN_ALARM_CFG);
  addHitArea(0, 38, SCR_W, 76, ACT_OPEN_SET_TIME);

  lastHour = rtc.hour; lastMin = rtc.minute; lastSec = rtc.second;
}

/* Atualiza apenas o que muda a cada segundo na tela do relogio. */
void uiClockTick() {
  if (curScreen != SCREEN_CLOCK) return;

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

  bool online = commActuatorOnline();
  if (online != lastOnline) {
    infoLine(230, "ATUADOR", online ? "ONLINE" : "OFFLINE",
             online ? C_OK : C_DANGER);
    lastOnline = online;
  }
}

/* ───────────────────────── hub de ajustes ──────────────────────── */

/* Menu com os atalhos de configuracao. */
static void drawSettingsScreen() {
  tft.fillScreen(C_BG);
  drawTitleBar("AJUSTES");

  addButton(20,  60, 200, 50, "CONFIG. ALARME", ACT_GO_ALARM_CFG, C_BTN, C_ACCENT);
  addButton(20, 120, 200, 50, "AJUSTAR HORA",   ACT_GO_SET_TIME,  C_BTN, C_ACCENT);
  addButton(20, 180, 200, 50, "TESTAR ALARME",  ACT_TEST_ALARM,   C_BTN, C_WARN);
  addButton(20, 250, 200, 50, "VOLTAR",         ACT_BACK,         C_HEADER, C_LINE);

  textCentered(238, "dispara o alarme agora", C_SUBTEXT, C_BG, 1);
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

/* Tela de configuracao completa do alarme. */
static void drawAlarmConfigScreen() {
  tft.fillScreen(C_BG);
  drawTitleBar("CONFIG. ALARME");

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d", settings.almHour);
  spinRow(34, 32, "HORA", buf, ACT_ALM_H_DN, ACT_ALM_H_UP);

  snprintf(buf, sizeof(buf), "%02d", settings.almMin);
  spinRow(70, 32, "MIN", buf, ACT_ALM_M_DN, ACT_ALM_M_UP);

  textAt(6, 112, "ALARME", C_SUBTEXT, C_BG, 2);
  addButton(96, 106, 134, 28, settings.almEnabled ? "ATIVO" : "INATIVO",
            ACT_ALM_TOGGLE,
            settings.almEnabled ? C_BTN_SEL : C_BTN,
            settings.almEnabled ? C_OK : C_LINE);

  // Dificuldade — 4 opcoes exclusivas.
  textAt(6, 138, "DIFICULDADE", C_SUBTEXT, C_BG, 1);
  static const char* DL[4] = { "FACIL", "MEDIO", "DIFIC", "EXTR" };
  for (int i = 0; i < 4; i++) {
    bool sel = (settings.diffLevel == i);
    addButton(6 + i * 58, 152, 54, 28, DL[i], ACT_DIFF_0 + i,
              sel ? C_BTN_SEL : C_BTN, sel ? C_OK : C_LINE);
  }

  // Tipo de desafio — 4 opcoes exclusivas.
  textAt(6, 184, "DESAFIO", C_SUBTEXT, C_BG, 1);
  static const char* TL_[4] = { "MAT", "MEM", "SEQ", "REA" };
  for (int i = 0; i < 4; i++) {
    bool sel = (settings.diffType == i);
    addButton(6 + i * 58, 198, 54, 28, TL_[i], ACT_TYPE_0 + i,
              sel ? C_BTN_SEL : C_BTN, sel ? C_OK : C_LINE);
  }

  snprintf(buf, sizeof(buf), "%d%%", settings.lightPct);
  spinRow(230, 28, "LUZ", buf, ACT_LIGHT_DN, ACT_LIGHT_UP);

  static const char* VIBN[3] = { "OFF", "FRACA", "FORTE" };
  textAt(6, 268, "VIB", C_SUBTEXT, C_BG, 2);
  addButton(96, 262, 134, 28, VIBN[constrain(settings.vibMode, 0, 2)],
            ACT_VIB_CYCLE,
            settings.vibMode ? C_BTN_SEL : C_BTN,
            settings.vibMode ? C_OK : C_LINE);

  addButton(6,   292, 112, 26, "SALVAR", ACT_SAVE, C_BTN_SEL, C_OK);
  addButton(122, 292, 112, 26, "VOLTAR", ACT_BACK, C_HEADER, C_LINE);
}

/* ───────────────────── ajuste da hora atual ────────────────────── */

/* Tela de acerto do relogio. */
static void drawSetTimeScreen() {
  tft.fillScreen(C_BG);
  drawTitleBar("AJUSTAR HORA");

  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", rtc.hour, rtc.minute, rtc.second);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString(buf, SCR_W / 2, 46, 6);      // font 6 = 48 px (digitos e ":")
  tft.setTextDatum(TL_DATUM);

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

/* ─────────────────────── alarme disparado ──────────────────────── */

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

  tft.fillRect(0, 120, SCR_W, 148, C_BG);

  if (hidden) {
    textCentered(180, "AGUARDE...", C_WARN, C_BG, 4);
    // Area invisivel: tocar aqui antes da hora e queimar a largada.
    addHitArea(0, 120, SCR_W, 148, ACT_EARLY_TAP);
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

/* Tela de alarme tocando: enunciado, alternativas e soneca. */
static void drawAlarmActiveScreen() {
  const Challenge& c = challengeCurrent();
  tft.fillScreen(C_BG);

  tft.fillRect(0, 0, SCR_W, 32, C_DANGER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, C_DANGER);
  tft.drawString("!!! ALARME !!!", SCR_W / 2, 16, 4);
  tft.setTextDatum(TL_DATUM);

  tft.fillRect(0, 34, SCR_W, 24, C_PANEL);   // linha de status do desafio

  textCenteredFit(64, c.prompt, c.promptColor, C_BG);
  textCentered(100, c.hint, C_SUBTEXT, C_BG, 2);

  drawOptions();

  addButton(20, 272, 200, 44, "SONECA (+5 min)", ACT_SNOOZE, C_HEADER, C_WARN);
}

/* Atualiza a tela do alarme: status, alternativas e feedback de acerto/erro. */
void uiAlarmActiveUpdate(int stage, unsigned long elapsedMs, bool flash) {
  if (curScreen != SCREEN_ALARM_ACTIVE) return;
  const Challenge& c = challengeCurrent();

  // Cabecalho pisca no estagio mais agressivo.
  static bool lastFlash = false;
  if (stage >= 3 && flash != lastFlash) {
    uint16_t bg = flash ? C_WARN : C_DANGER;
    tft.fillRect(0, 0, SCR_W, 32, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT, bg);
    tft.drawString("!!! ACORDE !!!", SCR_W / 2, 16, 4);
    tft.setTextDatum(TL_DATUM);
    lastFlash = flash;
  }

  // Linha de status: tentativa, progresso, nivel e tempo decorrido.
  static char lastLine[42] = "";
  char line[42];
  unsigned long secs = elapsedMs / 1000UL;
  if (c.expectedLen > 1) {
    snprintf(line, sizeof(line), "T%d  %d/%d  %s  %02lu:%02lu",
             challengeAttempts(), c.entered, c.expectedLen,
             diffName(c.level), secs / 60, secs % 60);
  } else {
    snprintf(line, sizeof(line), "Tentativa %d  %s  %02lu:%02lu",
             challengeAttempts(), diffName(c.level), secs / 60, secs % 60);
  }
  if (strcmp(line, lastLine) != 0) {
    tft.fillRect(0, 34, SCR_W, 24, C_PANEL);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_ACCENT, C_PANEL);
    tft.drawString(line, SCR_W / 2, 45, 2);
    tft.setTextDatum(TL_DATUM);
    strncpy(lastLine, line, sizeof(lastLine) - 1);
    lastLine[sizeof(lastLine) - 1] = '\0';
  }

  if (!c.redraw) return;
  challengeClearRedraw();

  // Enunciado + alternativas (a fase pode ter mudado o texto).
  tft.fillRect(0, 60, SCR_W, 58, C_BG);
  textCenteredFit(64, c.prompt, c.promptColor, C_BG);
  textCentered(100, c.hint, C_SUBTEXT, C_BG, 2);

  curBtnCount = 0;                      // rearma a tabela de botoes
  flashIdx    = -1;                     // indices antigos deixam de valer
  if (c.phase == CHP_RIGHT) {
    tft.fillRect(0, 120, SCR_W, 148, C_BG);
    textCentered(170, "CORRETO!", C_OK, C_BG, 4);
  } else if (c.phase == CHP_WRONG) {
    tft.fillRect(0, 120, SCR_W, 148, C_BG);
    textCentered(160, "ERROU", C_DANGER, C_BG, 4);
    textCentered(196, "novo desafio, nivel acima", C_SUBTEXT, C_BG, 2);
  } else {
    drawOptions();
  }
  addButton(20, 272, 200, 44, "SONECA (+5 min)", ACT_SNOOZE, C_HEADER, C_WARN);
}

/* ──────────────────────── troca de tela ────────────────────────── */

/* Redesenha uma tela do zero e reconstroi a tabela de botoes. */
void uiDrawScreen(Screen s) {
  curScreen   = s;
  curBtnCount = 0;
  flashIdx    = -1;

  switch (s) {
    case SCREEN_CLOCK:        drawClockScreen();       break;
    case SCREEN_SETTINGS:     drawSettingsScreen();    break;
    case SCREEN_SET_TIME:     drawSetTimeScreen();     break;
    case SCREEN_ALARM_CONFIG: drawAlarmConfigScreen(); break;
    case SCREEN_ALARM_ACTIVE: drawAlarmActiveScreen(); break;
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
