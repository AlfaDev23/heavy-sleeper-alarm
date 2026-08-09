/*
 ═══════════════════════════════════════════════════════════════════
  DESPERTADOR HEAVY SLEEPERS — Demo de Apresentação
  Hardware  : ESP32-32E + TFT ST7789 2.8" (240 × 320 px)
  Biblioteca: TFT_eSPI (User_Setup.h já configurado)

  Layout (retrato, 240×320 px):
    [  0– 35] Cabeçalho "ALARME"
    [ 38–113] Hora:Min (Font 7 — 7 segmentos, 48 px) + segundos
    [115–144] Barra de alarme / flash vermelho ao disparar
    [146–181] Cabeçalho do desafio matemático
    [187–241] 4 botões de resposta
    [247–319] Barras de status (sons, luz, vibração)

  ► O tempo começa em 07:29 → alarme dispara em ~1 min (ótimo para demo!)
  ► Para ajustar, mude tHr / tMn no início do arquivo.
 ═══════════════════════════════════════════════════════════════════
*/

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

/* ── Paleta de cores (RGB 565) ─────────────────────────────────── */
#define C_BG         TFT_BLACK    // 0x0000 — fundo principal
#define C_HEADER     0x2945       //         cinza-escuro do cabeçalho
#define C_ALARM_BG   0x000C       //         azul muito escuro
#define C_CHAL_BG    0x0008       //         azul escuro (desafio)
#define C_BTN        0x10A3       //         botão inativo
#define C_BTN_OK     0x02A6       //         botão correto (verde-escuro)
#define C_ACCENT     TFT_CYAN     // 0x07FF  ciano para destaques
#define C_GRAY       0x8C51       //         cinza médio (segundos)
#define C_LINE       0x4208       //         linhas separadoras
#define C_STATUS_BG  0x0820       //         fundo do rodapé

/* ── Tempo simulado ─────────────────────────────────────────────── */
int tHr = 7, tMn = 29, tSc = 0;   // Começa em 07:29:00
unsigned long prevMs = 0;

/* ── Configuração do alarme ────────────────────────────────────── */
const int  ALM_H = 7, ALM_M = 30;
bool alarmEnabled = true;
bool alarmRinging = false;
bool flashState   = false;

/* ── Desafio matemático (12 + 7 = ?) ───────────────────────────── */
// Para mudar o desafio: edite a string em drawChallenge()
// e atualize os valores abaixo.
const int opts[]  = {17, 19, 20, 21};
const int correct = 19;  // 12 + 7 = 19

/* ═══════════════════════════════════════════════════════════════════
   Helper: barra de nível com 8 segmentos coloridos
   ═══════════════════════════════════════════════════════════════════ */
void drawLevelBar(int x, int y, int filled, uint16_t activeColor) {
  for (int i = 0; i < 8; i++) {
    tft.fillRect(x + i * 11, y, 9, 8,
                 (i < filled) ? activeColor : C_LINE);
  }
}

/* ═══════════════════════════════════════════════════════════════════
   Seção 1 — Cabeçalho "ALARME"
   ═══════════════════════════════════════════════════════════════════ */
void drawHeader() {
  tft.fillRect(0, 0, 240, 36, C_HEADER);

  // Título centralizado — Font 4 (26 px)
  tft.setTextFont(4);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, C_HEADER);
  int tw = tft.textWidth("ALARME");
  tft.setCursor((240 - tw) / 2, 5);
  tft.print("ALARME");

  // Ícone de brilho — esquerda
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, C_HEADER);
  tft.setCursor(6, 11);
  tft.print("*");

  // Ícone de volume — direita
  tft.setCursor(210, 11);
  tft.print("))");

  tft.drawFastHLine(0, 36, 240, C_LINE);
}

/* ═══════════════════════════════════════════════════════════════════
   Seção 2 — Hora e segundos
   Chame com fullClear=true apenas na primeira vez.
   Nas atualizações de 1 em 1 s, use fullClear=false (mais rápido).
   ═══════════════════════════════════════════════════════════════════ */
void drawTime(bool fullClear) {
  if (fullClear) {
    tft.fillRect(0, 38, 240, 76, C_BG);
  }

  // ── Hora:Minuto — Font 7 (7 segmentos, 48 px) ─────────────────
  char buf[6];
  sprintf(buf, "%02d:%02d", tHr, tMn);

  tft.setTextFont(7);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, C_BG);
  int tw = tft.textWidth(buf);
  tft.setCursor((240 - tw) / 2, 42);
  tft.print(buf);

  // ── Segundos — Font 2 (16 px), canto inferior direito ─────────
  char sbuf[4];
  sprintf(sbuf, ":%02d", tSc);
  tft.fillRect(183, 94, 54, 16, C_BG);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(C_GRAY, C_BG);
  tft.setCursor(186, 95);
  tft.print(sbuf);

  tft.drawFastHLine(0, 114, 240, C_LINE);
}

/* ═══════════════════════════════════════════════════════════════════
   Seção 3 — Barra de alarme
   ringing=true  → alterna vermelho/azul (flash)
   ringing=false → mostra horário do alarme em ciano
   ═══════════════════════════════════════════════════════════════════ */
void drawAlarmBar(bool ringing, bool flash) {
  uint16_t bg = (ringing && flash) ? TFT_RED : C_ALARM_BG;
  tft.fillRect(0, 115, 240, 30, bg);

  tft.setTextFont(2);
  tft.setTextSize(1);

  if (ringing) {
    const char* msg = flash
      ? "  !!! ALARME DISPARANDO !!!  "
      : "   >> Resolva o desafio! <<  ";
    tft.setTextColor(TFT_WHITE, bg);
    int tw = tft.textWidth(msg);
    tft.setCursor((240 - tw) / 2, 122);
    tft.print(msg);
  } else {
    char buf[32];
    sprintf(buf, "Alarme: %02d:%02d  [ATIVO]", ALM_H, ALM_M);
    tft.setTextColor(C_ACCENT, bg);
    int tw = tft.textWidth(buf);
    tft.setCursor((240 - tw) / 2, 122);
    tft.print(buf);
  }

  tft.drawFastHLine(0, 145, 240, C_LINE);
}

/* ═══════════════════════════════════════════════════════════════════
   Seção 4 — Desafio + botões de resposta
   ═══════════════════════════════════════════════════════════════════ */
void drawChallenge() {
  // Fundo e texto do desafio
  tft.fillRect(0, 146, 240, 36, C_CHAL_BG);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(C_ACCENT, C_CHAL_BG);
  const char* chal = "DESAFIO: 12 + 7 = ?";   // ← mude aqui
  int tw = tft.textWidth(chal);
  tft.setCursor((240 - tw) / 2, 157);
  tft.print(chal);
  tft.drawFastHLine(0, 182, 240, C_LINE);

  // 4 botões de resposta em linha
  const int W  = 54;   // largura do botão
  const int H  = 55;   // altura do botão
  const int Y  = 187;  // posição vertical
  const int MX = 7;    // margem esquerda
  const int G  = 3;    // gap entre botões

  for (int i = 0; i < 4; i++) {
    int      x    = MX + i * (W + G);
    bool     isOk = (opts[i] == correct);
    uint16_t bg   = isOk ? C_BTN_OK : C_BTN;
    uint16_t bord = isOk ? C_ACCENT  : C_LINE;

    tft.fillRoundRect(x, Y, W, H, 8, bg);
    tft.drawRoundRect(x, Y, W, H, 8, bord);

    char label[3];
    sprintf(label, "%d", opts[i]);
    tft.setTextFont(4);   // 26 px para os números
    tft.setTextColor(TFT_WHITE, bg);
    int lw = tft.textWidth(label);
    tft.setCursor(x + (W - lw) / 2, Y + (H - 26) / 2);
    tft.print(label);
  }

  tft.drawFastHLine(0, 246, 240, C_LINE);
}

/* ═══════════════════════════════════════════════════════════════════
   Seção 5 — Barras de status no rodapé
   ═══════════════════════════════════════════════════════════════════ */
void drawStatusArea() {
  tft.fillRect(0, 247, 240, 73, C_STATUS_BG);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(C_GRAY, C_STATUS_BG);

  tft.setCursor(8, 255); tft.print("SONS");
  drawLevelBar(46, 253, 6, TFT_CYAN);

  tft.setCursor(8, 271); tft.print("LUZ ");
  drawLevelBar(46, 269, 5, TFT_YELLOW);

  tft.setCursor(8, 287); tft.print("VIB ");
  drawLevelBar(46, 285, 4, TFT_ORANGE);

  // Rodapé de versão
  tft.setTextColor(C_LINE, C_STATUS_BG);
  tft.setCursor(8, 306);
  tft.print("HeavySleep v0.1  |  Demo Mode");
}

/* ═══════════════════════════════════════════════════════════════════
   Setup
   ═══════════════════════════════════════════════════════════════════ */
void setup() {
  Serial.begin(115200);

  // Liga o backlight (pino 21, igual ao User_Setup.h)
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  tft.init();
  tft.setRotation(0);   // Retrato (240×320)
  tft.fillScreen(C_BG);

  // Desenha a interface completa
  drawHeader();
  drawTime(true);
  drawAlarmBar(false, false);
  drawChallenge();
  drawStatusArea();

  prevMs = millis();
}

/* ═══════════════════════════════════════════════════════════════════
   Loop — atualiza o relógio a cada segundo
   ═══════════════════════════════════════════════════════════════════ */
void loop() {
  if (millis() - prevMs >= 1000) {
    prevMs += 1000;   // +1000 evita drift acumulado

    // Avança 1 segundo
    if (++tSc >= 60) {
      tSc = 0;
      if (++tMn >= 60) {
        tMn = 0;
        if (++tHr >= 24) tHr = 0;
      }
    }

    // Verifica se deve tocar o alarme
    bool shouldRing = alarmEnabled && (tHr == ALM_H) && (tMn == ALM_M);

    if (shouldRing) {
      flashState   = !flashState;   // alterna flash vermelho / azul
      alarmRinging = true;
      drawAlarmBar(true, flashState);
    } else if (alarmRinging) {
      // Minuto passou → para de tocar
      alarmRinging = false;
      flashState   = false;
      drawAlarmBar(false, false);
    }

    // Redesenha apenas a área de tempo (eficiente)
    drawTime(false);
  }
}
