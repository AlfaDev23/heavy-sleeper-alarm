/*
 ═══════════════════════════════════════════════════════════════════
  DESPERTADOR HEAVY SLEEPERS — Display ESP32
  Hardware : board E32R28T (ESP32-32E + TFT ST7789 2.8" 240x320 + XPT2046)
  Firmware : ver FW_VERSION em config.h

  Responsabilidades desta placa:
    - relogio, telas e navegacao por toque (XPT2046 em SPI proprio)
    - desafios cognitivos para desligar o alarme
    - audio do alarme pelo DAC IO26 + amplificador do board (IO4)
    - envio dos comandos de luz e vibracao ao Actuator ESP32 (UART2)

  REGRA DE OURO: nenhum delay() no fluxo principal. Tudo e millis() e
  maquina de estados; o unico ponto que bloqueia e a task de audio, que
  vive isolada no Core 0.
 ═══════════════════════════════════════════════════════════════════
*/

#include "config.h"
#include "clock.h"
#include "storage.h"
#include "ui.h"
#include "touch.h"
#include "challenges.h"
#include "audio.h"
#include "comm.h"

/* ── Estado de navegacao ────────────────────────────────────────── */
static Screen        curScreen    = SCREEN_CLOCK;
static unsigned long redrawAtMs   = 0;      // redesenho adiado (apos o piscar)

/* ── Estado do alarme ───────────────────────────────────────────── */
static bool          ringing      = false;
static unsigned long alarmStartMs = 0;
static int           stage        = 0;      // 1..4
static int           tempBonus    = 0;      // +1 por erro
static int           stageBonus   = 0;      // +1 a partir do estagio 2
static int           snoozeBonus  = 0;      // +1 por soneca
static int           lastFiredKey = -1;     // evita redisparo no mesmo minuto
static unsigned long snoozeUntil  = 0;      // 0 = sem soneca pendente
static unsigned long lastFlashMs  = 0;
static bool          flashState   = false;
static unsigned long lastStatusMs = 0;
static unsigned long resolveAtMs  = 0;      // fim da tela CORRETO!/ERROU

/* ═══════════════════════════════════════════════════════════════════
   Navegacao
   ═══════════════════════════════════════════════════════════════════ */

/* Troca de tela: ajusta o brilho, redesenha e limpa toques pendentes. */
static void switchScreen(Screen s) {
  curScreen = s;
  switch (s) {
    case SCREEN_ALARM_ACTIVE: uiSetBacklight(BL_ALARM);   break;
    case SCREEN_CLOCK:        uiSetBacklight(BL_STANDBY); break;
    default:                  uiSetBacklight(BL_CONFIG);  break;
  }
  uiDrawScreen(s);
  touchFlush();
}

/* Agenda um redesenho da tela atual apos o feedback visual do botao. */
static void scheduleRedraw() {
  redrawAtMs = millis() + 120;
}

/* ═══════════════════════════════════════════════════════════════════
   Estimulos
   ═══════════════════════════════════════════════════════════════════ */

/* Nivel de dificuldade efetivo, somando todos os bonus temporarios. */
static int effectiveLevel() {
  return constrain(settings.diffLevel + tempBonus + stageBonus + snoozeBonus, 0, 3);
}

/*
 * Aplica luz, vibracao e audio do estagio.
 * A luz usa a porcentagem do estagio (30/60/100) escalada pelo maximo que
 * o usuario configurou, para o despertar continuar gradual sem estourar
 * o limite escolhido.
 */
static void applyStimuli(int st) {
  static const int STAGE_LIGHT[5] = { 0, 30, 60, 100, 100 };
  int base = STAGE_LIGHT[constrain(st, 0, 4)];
  commSendLight((base * settings.lightPct) / 100);

  if (settings.vibMode == VIB_MODE_OFF) {
    commSendVib("OFF");
  } else if (st <= 1) {
    commSendVib("PULSE");
  } else {
    commSendVib(settings.vibMode == VIB_MODE_STRONG ? "ON" : "PULSE");
  }

  audioSetLevel(st);
}

/* Desliga todos os estimulos. */
static void stopStimuli() {
  commSendLight(0);
  commSendVib("OFF");
  stopAudio();
}

/* ═══════════════════════════════════════════════════════════════════
   Ciclo do alarme
   ═══════════════════════════════════════════════════════════════════ */

/* Dispara o alarme: zera bonus, arma o primeiro desafio e liga estimulos. */
static void startAlarm() {
  ringing      = true;
  alarmStartMs = millis();
  stage        = 1;
  tempBonus    = 0;
  stageBonus   = 0;
  resolveAtMs  = 0;
  snoozeUntil  = 0;

  // Zera o contador de sonecas quando vira o dia.
  if (settings.lastAlarmDay != rtc.dayIndex) {
    settings.lastAlarmDay = rtc.dayIndex;
    settings.snoozeCount  = 0;
    snoozeBonus           = 0;
    storageSave();
  }

  challengeResetAttempts();
  challengeStart(settings.diffType, effectiveLevel());
  commSendAlarm("START");
  applyStimuli(stage);
  switchScreen(SCREEN_ALARM_ACTIVE);
  Serial.println(F("[ALARM] disparado"));
}

/* Encerra o alarme (desafio resolvido) e volta ao relogio. */
static void stopAlarm() {
  ringing = false;
  stopStimuli();
  commSendAlarm("STOP");
  tempBonus   = 0;
  stageBonus  = 0;
  snoozeBonus = 0;
  snoozeUntil = 0;
  switchScreen(SCREEN_CLOCK);
  Serial.println(F("[ALARM] desligado pelo desafio"));
}

/* Soneca: para os estimulos, endurece o proximo desafio e reagenda. */
static void snoozeAlarm() {
  ringing = false;
  stopStimuli();
  snoozeBonus++;
  settings.snoozeCount++;
  settings.lastAlarmDay = rtc.dayIndex;
  storageSave();
  snoozeUntil = millis() + SNOOZE_MS;
  switchScreen(SCREEN_CLOCK);
  uiToast("Soneca: proximo nivel acima", C_WARN);
  Serial.printf("[ALARM] soneca #%d\n", settings.snoozeCount);
}

/* Escalona os estimulos conforme o tempo desde o disparo. */
static void updateEscalation() {
  unsigned long elapsed = millis() - alarmStartMs;
  int want = stage;

  if      (elapsed >= ESC_T3_MS) want = 4;
  else if (elapsed >= ESC_T2_MS) want = 3;
  else if (elapsed >= ESC_T1_MS) want = 2;
  else                           want = 1;

  if (want == stage) return;
  stage = want;

  if (stage >= 2) stageBonus = 1;
  if (stage == 4) commSendAlarm("ESCALATED");

  applyStimuli(stage);

  // Sobe a dificuldade tambem no desafio que esta na tela.
  if (challengeCurrent().phase == CHP_INPUT ||
      challengeCurrent().phase == CHP_SHOW) {
    challengeStart(settings.diffType, effectiveLevel());
  }
  Serial.printf("[ALARM] estagio %d\n", stage);
}

/* Trata o desfecho do desafio depois do tempo de exibicao do resultado. */
static void updateChallengeOutcome() {
  const Challenge& c = challengeCurrent();

  if (c.phase == CHP_RIGHT) {
    if (resolveAtMs == 0) { resolveAtMs = millis() + 1200; return; }
    if ((long)(millis() - resolveAtMs) >= 0) { resolveAtMs = 0; stopAlarm(); }
    return;
  }

  if (c.phase == CHP_WRONG) {
    if (resolveAtMs == 0) { resolveAtMs = millis() + 1400; return; }
    if ((long)(millis() - resolveAtMs) >= 0) {
      resolveAtMs = 0;
      tempBonus++;                                   // errou -> +1 nivel
      challengeStart(settings.diffType, effectiveLevel());
    }
  }
}

/* ═══════════════════════════════════════════════════════════════════
   Toque
   ═══════════════════════════════════════════════════════════════════ */

/* Aplica a acao de um botao da tela de configuracao do alarme. */
static void handleAlarmConfig(int a) {
  switch (a) {
    case ACT_ALM_H_UP: settings.almHour = (settings.almHour + 1) % 24;      break;
    case ACT_ALM_H_DN: settings.almHour = (settings.almHour + 23) % 24;     break;
    case ACT_ALM_M_UP: settings.almMin  = (settings.almMin + 1) % 60;       break;
    case ACT_ALM_M_DN: settings.almMin  = (settings.almMin + 59) % 60;      break;
    case ACT_ALM_TOGGLE: settings.almEnabled = !settings.almEnabled;        break;
    case ACT_LIGHT_UP: settings.lightPct = min(100, settings.lightPct + 10); break;
    case ACT_LIGHT_DN: settings.lightPct = max(0,   settings.lightPct - 10); break;
    case ACT_VIB_CYCLE: settings.vibMode = (settings.vibMode + 1) % 3;      break;
    default:
      if (a >= ACT_DIFF_0 && a <= ACT_DIFF_3) settings.diffLevel = a - ACT_DIFF_0;
      if (a >= ACT_TYPE_0 && a <= ACT_TYPE_3) settings.diffType  = a - ACT_TYPE_0;
      break;
  }
  scheduleRedraw();
}

/* Aplica a acao de um botao da tela de ajuste da hora. */
static void handleSetTime(int a) {
  switch (a) {
    case ACT_T_H_UP: rtc.setTime((rtc.hour + 1) % 24, rtc.minute, rtc.second);  break;
    case ACT_T_H_DN: rtc.setTime((rtc.hour + 23) % 24, rtc.minute, rtc.second); break;
    case ACT_T_M_UP: rtc.setTime(rtc.hour, (rtc.minute + 1) % 60, rtc.second);  break;
    case ACT_T_M_DN: rtc.setTime(rtc.hour, (rtc.minute + 59) % 60, rtc.second); break;
    case ACT_T_S_UP: rtc.setTime(rtc.hour, rtc.minute, (rtc.second + 1) % 60);  break;
    case ACT_T_S_DN: rtc.setTime(rtc.hour, rtc.minute, (rtc.second + 59) % 60); break;
  }
  scheduleRedraw();
}

/* Roteia um toque curto conforme a tela ativa. */
static void handleTap(int x, int y) {
  int a = uiHitTest(x, y);
  if (a == ACT_NONE) return;
  uiFlashAction(a);

  // Alternativas do desafio.
  if (a >= ACT_OPT_BASE) {
    if (curScreen == SCREEN_ALARM_ACTIVE) challengeTouchIndex(a - ACT_OPT_BASE);
    return;
  }

  switch (a) {
    case ACT_OPEN_ALARM_CFG: switchScreen(SCREEN_ALARM_CONFIG); return;
    case ACT_OPEN_SET_TIME:  switchScreen(SCREEN_SET_TIME);     return;
    case ACT_GO_ALARM_CFG:   switchScreen(SCREEN_ALARM_CONFIG); return;
    case ACT_GO_SET_TIME:    switchScreen(SCREEN_SET_TIME);     return;

    case ACT_TEST_ALARM:
      lastFiredKey = -1;
      startAlarm();
      return;

    case ACT_SAVE:
      storageSave();
      uiToast("Configuracoes salvas", C_OK);
      return;

    case ACT_BACK:
    case ACT_T_CONFIRM:
      switchScreen(SCREEN_CLOCK);
      return;

    case ACT_SNOOZE:
      if (ringing) snoozeAlarm();
      return;

    case ACT_EARLY_TAP:
      if (curScreen == SCREEN_ALARM_ACTIVE) challengeEarlyTap();
      return;
  }

  if (curScreen == SCREEN_ALARM_CONFIG) handleAlarmConfig(a);
  else if (curScreen == SCREEN_SET_TIME) handleSetTime(a);
}

/* ═══════════════════════════════════════════════════════════════════
   Setup
   ═══════════════════════════════════════════════════════════════════ */
void setup() {
  Serial.begin(115200);
  Serial.printf("\n[BOOT] HeavySleep Display %s\n", FW_VERSION);

  /*
   * A ORDEM AQUI IMPORTA:
   *   1. uiBegin() faz tft.init() e SO DEPOIS anexa o LEDC ao backlight —
   *      a TFT_eSPI reconfigura IO21 como saida digital durante o init().
   *   2. storageBegin() vem antes do touch: e da NVS que sai a calibracao.
   *   3. audioBegin() deixa o amplificador (IO4) mudo desde o boot.
   *   4. touchBegin() sobe o SPI proprio do XPT2046 e, se precisar, calibra
   *      na tela — por isso depende da UI e do storage ja estarem de pe.
   */
  uiBegin();             // tft.init() + rotacao + LEDC do backlight
  storageBegin();
  audioBegin();          // configura PIN_AMP_ENABLE e o DAC
  touchBegin();          // pode calibrar (unico ponto bloqueante, so no boot)
  commBegin();           // Serial2 nos pinos corretos + LED azul de status
  challengeInit();

#ifdef DEMO_MODE
  rtc.begin(DEMO_START_H, DEMO_START_M, DEMO_START_S);
  Serial.println(F("[BOOT] DEMO_MODE: alarme dispara em ~1 minuto"));
#else
  rtc.begin(0, 0, 0);
#endif

  switchScreen(SCREEN_CLOCK);
  if (!touchAvailable()) uiToast("Touch indisponivel", C_DANGER);
}

/* ═══════════════════════════════════════════════════════════════════
   Loop
   ═══════════════════════════════════════════════════════════════════ */
void loop() {
  commPoll();
  touchUpdate();
  uiFlashUpdate();

  /* Redesenho adiado, para o piscar do botao ficar visivel. */
  if (redrawAtMs && (long)(millis() - redrawAtMs) >= 0) {
    redrawAtMs = 0;
    uiDrawScreen(curScreen);
  }

  /* Toques. */
  int tx, ty;
  if (touchTap(tx, ty)) handleTap(tx, ty);
  if (touchLongPress(tx, ty)) {
    if (curScreen == SCREEN_CLOCK) switchScreen(SCREEN_SETTINGS);
  }

  /* Relogio. */
  if (rtc.tick()) {
    if (curScreen == SCREEN_CLOCK) uiClockTick();

    int key = rtc.dayIndex * 1440 + rtc.hour * 60 + rtc.minute;
    if (!ringing && settings.almEnabled &&
        rtc.isAlarmTime(settings.almHour, settings.almMin) &&
        key != lastFiredKey) {
      lastFiredKey = key;
      startAlarm();
    }
  }

  /* Soneca vencida. */
  if (!ringing && snoozeUntil && (long)(millis() - snoozeUntil) >= 0) {
    snoozeUntil = 0;
    startAlarm();
  }

  /* Alarme tocando. */
  if (ringing) {
    updateEscalation();
    challengeUpdate();
    updateChallengeOutcome();

    if (millis() - lastFlashMs >= 500) {      // piscar da tela/cabecalho
      lastFlashMs = millis();
      flashState  = !flashState;
    }
    uiAlarmActiveUpdate(stage, millis() - alarmStartMs, flashState);
  }

  /* Status periodico do Actuator. */
  if (millis() - lastStatusMs >= COMM_POLL_MS) {
    lastStatusMs = millis();
    commRequestStatus();
  }
}
