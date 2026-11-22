/*
 ═══════════════════════════════════════════════════════════════════
  DESPERTADOR HEAVY SLEEPERS — Display ESP32
  Hardware : board E32R28T (ESP32-32E + TFT ST7789 2.8" 240x320 + XPT2046)
  Firmware : ver FW_VERSION em config.h

  Responsabilidades desta placa:
    - relogio (acertado por NTP), telas e navegacao por toque
    - lampada Wi-Fi WiZ: "nascer do sol" antes do alarme
    - portal web de configuracao (celular)
    - desafios cognitivos cronometrados para desligar o alarme
    - audio do alarme pelo DAC IO26 + amplificador do board (IO4)
    - envio dos comandos de vibracao ao Actuator ESP32 (UART2)

  ALARMES (v0.5): ate MAX_ALARMS, cada um com dias da semana (alarms.h).
  Tocando por mais de ringMaxMin minutos sem ninguem desligar, para sozinho.

  CICLO DO ALARME (v0.4):
    rampa ......... rampMin minutos antes: lampada sobe de 10% / 2200 K
                    ate o brilho maximo / 6500 K
    TOCANDO ....... som + vibracao + luz no maximo. Segure DESLIGAR 2 s.
    DESAFIO ....... silencio. chalTimeS segundos para resolver.
                    acertou -> fim (a luz continua acesa)
                    errou / tempo acabou -> volta a TOCAR, desafio +1 nivel
    A escalada do som (estagios 1-4) so conta o tempo TOCANDO.

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
#include "net.h"
#include "wiz.h"
#include "portal.h"
#include "night.h"
#include "app.h"

/* ── Estado de navegacao ────────────────────────────────────────── */
static Screen        curScreen    = SCREEN_CLOCK;
static unsigned long redrawAtMs   = 0;      // redesenho adiado (apos o piscar)
static unsigned long lastTouchMs  = 0;      // para voltar ao relogio sozinho
static bool          settingsDirty = false; // portal mudou algo

#define CONFIG_IDLE_MS 60000UL              // telas de ajuste fecham em 1 min

/* ── Estado do alarme ───────────────────────────────────────────── */
enum AlarmState { AL_IDLE = 0, AL_RINGING, AL_CHALLENGE };
static AlarmState    alState       = AL_IDLE;
static unsigned long ringAccumMs   = 0;     // tempo tocando ja acumulado
static unsigned long ringSegMs     = 0;     // inicio do trecho tocando atual
static unsigned long chalStartMs   = 0;
static unsigned long chalTotalMs   = 0;
static int           stage         = 0;     // 1..4
static int           tempBonus     = 0;     // +1 por erro
static int           stageBonus    = 0;     // +1 a partir do estagio 2
static int           snoozeBonus   = 0;     // +1 por soneca
static int           lastFiredKey  = -1;    // evita redisparo no mesmo minuto
static unsigned long snoozeUntil   = 0;     // 0 = sem soneca pendente
static unsigned long lastFlashMs   = 0;
static bool          flashState    = false;
static unsigned long lastStatusMs  = 0;
static unsigned long resolveAtMs   = 0;     // fim da tela CORRETO!/ERROU
static unsigned long vibTestUntil  = 0;     // fim do "Testar" da vibracao
static unsigned long soundTestUntil = 0;    // fim do "Testar" do volume
static int           lastChalType  = -1;    // ultimo tipo sorteado
static int           firedMin      = -1;    // minuto do dia em que disparou
static char          missedTxt[40] = "";    // "Alarme das 07:00 nao atendido"

/* Segurar DESLIGAR. O touch resistivo as vezes perde uma leitura com o dedo
   parado; uma falha curta (< HOLD_GRACE_MS) nao zera a contagem. */
#define HOLD_GRACE_MS 150
static bool          holding       = false;
static unsigned long holdStartMs   = 0;
static unsigned long holdLostMs    = 0;

/* ── Nascer do sol e modo noturno ───────────────────────────────── */
static bool          sunriseOn     = false;
static unsigned long lastRampMs    = 0;
static bool          nightActive   = false;
static unsigned long peekUntil     = 0;     // 0 = sem "espiada" no modo noturno

/* ═══════════════════════════════════════════════════════════════════
   Ganchos para os outros modulos (app.h)
   ═══════════════════════════════════════════════════════════════════ */
bool appAlarmBusy()       { return alState != AL_IDLE; }
void appSettingsChanged() { settingsDirty = true; }
bool appSunriseActive()   { return sunriseOn; }
const char* appMissedAlarm() { return missedTxt; }

/* Teste de intensidade pedido pelo portal. O loop desliga em VIB_TEST_MS. */
bool appVibTest(int percent) {
  if (alState != AL_IDLE) return false;
  commSendVibDuty(percent);
  commSendVib("ON");
  vibTestUntil = millis() + VIB_TEST_MS;
  return true;
}

/* Teste de volume pedido pelo portal. O loop corta em SOUND_TEST_MS. */
bool appSoundTest(int melody, int pct) {
  if (alState != AL_IDLE) return false;
  audioTest(melody, pct);
  soundTestUntil = millis() + SOUND_TEST_MS;
  return true;
}

/* Demo <-> producao. Para a rampa e a soneca em andamento, troca o alarme
   (storage) e o relogio: demo vai para 07:29:00; producao zera o relogio e
   deixa o NTP acertar no proximo segundo (sem internet: AJUSTAR HORA). */
bool appSetDemo(bool on) {
  if (alState != AL_IDLE) return false;
  if (on == settings.demoMode) return true;

  snoozeUntil  = 0;
  snoozeBonus  = 0;
  lastFiredKey = -1;
  if (sunriseOn) { sunriseOn = false; wizOff(); }

  storageSetDemo(on);
  if (on) rtc.begin(DEMO_START_H, DEMO_START_M, DEMO_START_S);
  else    rtc.begin(0, 0, 0);       // synced=false: o NTP reaplica ja
  nightActive = nightShouldBeActive();
  settingsDirty = true;
  return true;
}

/* ═══════════════════════════════════════════════════════════════════
   Navegacao
   ═══════════════════════════════════════════════════════════════════ */

static bool peeking() { return peekUntil && (long)(millis() - peekUntil) < 0; }

/* Troca de tela: ajusta o brilho, redesenha e limpa toques pendentes. */
static void switchScreen(Screen s) {
  curScreen = s;
  bool nightLook = nightActive && !peeking();
  uiSetNight(nightLook);
  switch (s) {
    case SCREEN_ALARM_RINGING:
    case SCREEN_ALARM_ACTIVE: uiSetBacklight(BL_ALARM);                         break;
    case SCREEN_CLOCK:        uiSetBacklight(nightLook ? BL_NIGHT : BL_STANDBY); break;
    default:                  uiSetBacklight(BL_CONFIG);                        break;
  }
  uiDrawScreen(s);
  touchFlush();
}

/* Agenda um redesenho da tela atual apos o feedback visual do botao. */
static void scheduleRedraw() {
  redrawAtMs = millis() + 120;
}

/* Reavalia o modo noturno (1x por segundo). */
static void refreshNight() {
  if (peekUntil && !peeking()) peekUntil = 0;       // espiada acabou
  nightActive = nightShouldBeActive();
  bool want = nightActive && !peeking();
  if (curScreen == SCREEN_CLOCK && want != uiIsNight()) switchScreen(SCREEN_CLOCK);
}

/* ═══════════════════════════════════════════════════════════════════
   Estimulos
   ═══════════════════════════════════════════════════════════════════ */

/* Nivel de dificuldade efetivo, somando todos os bonus temporarios. */
static int effectiveLevel() {
  return constrain(settings.diffLevel + tempBonus + stageBonus + snoozeBonus, 0, 3);
}

/* Luz, vibracao e audio do estagio. A lampada fica no maximo configurado:
   ela ja chegou la gradualmente pela rampa antes do alarme. */
static void applyStimuli(int st) {
  wizSetLight(settings.lightPct, WIZ_TEMP_MAX);
#ifdef USE_MOSFET_LIGHT
  static const int STAGE_LIGHT[5] = { 0, 30, 60, 100, 100 };
  commSendLight((STAGE_LIGHT[constrain(st, 0, 4)] * settings.lightPct) / 100);
#endif

  // Vibracao: estagio 1 = fraca pulsada; estagio 2 = media continua;
  // estagio 3+ = maxima continua — sem passar do nivel escolhido.
  vibTestUntil = 0;
  if (settings.vibMode == VIB_MODE_OFF) {
    commSendVib("OFF");
  } else {
    int top = settings.vibMode - VIB_MODE_WEAK;          // 0 fraca .. 2 maxima
    int lvl = (st <= 1) ? 0 : min(st - 1, top);
    commSendVibDuty(settings.vibDuty[lvl]);
    commSendVib((st <= 1 || top == 0) ? "PULSE" : "ON");
  }

  soundTestUntil = 0;
  audioSetVolume(settings.volStart, settings.volMax);   // volume do portal
  audioSetLevel(st);
}

/* Para som e vibracao. A luz fica como esta. */
static void stopSound() {
  commSendVib("OFF");
  stopAudio();
}

/* Apaga a luz do despertar. */
static void lightOff() {
  wizOff();
#ifdef USE_MOSFET_LIGHT
  commSendLight(0);
#endif
}

/* ═══════════════════════════════════════════════════════════════════
   Nascer do sol (rampa da lampada antes do alarme)
   ═══════════════════════════════════════════════════════════════════ */

static long rampSeconds() {
  return demoFastTimes() ? DEMO_RAMP_S : settings.rampMin * 60L;
}

/*
 * Chamada 1x por segundo. Calcula quanto falta para o alarme e, dentro da
 * janela da rampa, leva a lampada de WIZ_DIM_MIN%/WIZ_TEMP_MIN (luz quente
 * e fraca) ate lightPct%/WIZ_TEMP_MAX (branca e forte) no horario do alarme.
 */
static void updateSunrise() {
  if (alState != AL_IDLE) return;

  bool  want = false;
  float p    = 0;
  if (!snoozeUntil) {
    long left = 0;
    int  idx  = alarmNext(settings.alarms, settings.alarmCount, rtc.weekday,
                          rtc.secondsOfDay(), &left);
    long rs   = rampSeconds();
    if (idx >= 0 && left > 0 && left <= rs) {
      want = true;
      p    = 1.0f - (float)left / (float)rs;
    }
  }

  if (want) {
    unsigned long now = millis();
    if (!sunriseOn) {
      sunriseOn  = true;
      lastRampMs = now - WIZ_RAMP_STEP_MS;      // primeiro passo ja
      Serial.println(F("[SUNRISE] rampa iniciada"));
    }
    if (now - lastRampMs >= WIZ_RAMP_STEP_MS) {
      lastRampMs = now;
      int dim  = WIZ_DIM_MIN  + (int)((settings.lightPct - WIZ_DIM_MIN) * p + 0.5f);
      int temp = WIZ_TEMP_MIN + (int)((WIZ_TEMP_MAX - WIZ_TEMP_MIN) * p + 0.5f);
      temp = (temp / 100) * 100;                // passos de 100 K
      wizSetLight(dim, temp);
    }
  } else if (sunriseOn) {
    // Saiu da janela sem disparar (alarme desativado ou horario mudado).
    sunriseOn = false;
    lightOff();
    Serial.println(F("[SUNRISE] rampa cancelada"));
  }
}

/* ═══════════════════════════════════════════════════════════════════
   Ciclo do alarme
   ═══════════════════════════════════════════════════════════════════ */

/* Tempo total TOCANDO (nao conta o tempo resolvendo o desafio). */
static unsigned long ringElapsed() {
  return ringAccumMs + (alState == AL_RINGING ? millis() - ringSegMs : 0);
}

/* Dispara o alarme: zera os bonus e liga os estimulos. */
static void startAlarm() {
  alState      = AL_RINGING;
  ringAccumMs  = 0;
  ringSegMs    = millis();
  stage        = 1;
  tempBonus    = 0;
  stageBonus   = 0;
  resolveAtMs  = 0;
  snoozeUntil  = 0;
  holding      = false;
  sunriseOn    = false;
  peekUntil    = 0;
  missedTxt[0] = '\0';
  firedMin     = rtc.minuteOfDay();

  // Zera o contador de sonecas quando vira o dia.
  if (settings.lastAlarmDay != rtc.dayIndex) {
    settings.lastAlarmDay = rtc.dayIndex;
    settings.snoozeCount  = 0;
    snoozeBonus           = 0;
    storageSave();
  }

  challengeResetAttempts();
  commSendAlarm("START");
  applyStimuli(stage);
  switchScreen(SCREEN_ALARM_RINGING);
  Serial.println(F("[ALARM] disparado"));
}

/* Segurou DESLIGAR: silencio e comeca o desafio cronometrado. */
static void dismissRing() {
  ringAccumMs += millis() - ringSegMs;
  alState      = AL_CHALLENGE;
  holding      = false;
  resolveAtMs  = 0;
  stopSound();
  // Com varios tipos marcados, cada desafio sorteia um (sem repetir o
  // anterior); um so marcado = sempre ele.
  lastChalType = challengePickType(settings.chalMask, lastChalType);
  challengeStart(lastChalType, effectiveLevel());
  chalStartMs  = millis();
  chalTotalMs  = settings.chalTimeS * 1000UL;
  commSendAlarm("DISMISS");
  switchScreen(SCREEN_ALARM_ACTIVE);
  Serial.printf("[ALARM] desligado pelo botao: %d s para o desafio\n", settings.chalTimeS);
}

/* Errou ou acabou o tempo: volta a tocar no estagio em que estava. */
static void resumeRing() {
  alState   = AL_RINGING;
  ringSegMs = millis();
  holding   = false;
  applyStimuli(stage);
  commSendAlarm("RESUME");
  switchScreen(SCREEN_ALARM_RINGING);
  Serial.println(F("[ALARM] voltou a tocar"));
}

/* Desafio resolvido: fim. A lampada continua acesa (ajuda a levantar). */
static void stopAlarm() {
  alState = AL_IDLE;
  stopSound();
  commSendAlarm("STOP");
  tempBonus   = 0;
  stageBonus  = 0;
  snoozeBonus = 0;
  snoozeUntil = 0;
  switchScreen(SCREEN_CLOCK);
  Serial.println(F("[ALARM] encerrado pelo desafio"));
}

/* Tempo maximo tocando (0 = sem limite). No demo rapido, 2 min. */
static unsigned long ringMaxMs() {
  if (settings.ringMaxMin <= 0) return 0;
  return demoFastTimes() ? RING_MAX_DEMO_MS : settings.ringMaxMin * 60000UL;
}

/* Tocou o tempo maximo e ninguem desligou: desiste, como no celular.
   Apaga som, vibracao e luz e deixa o aviso na tela do relogio. */
static void timeoutAlarm() {
  unsigned long rang = ringElapsed();
  alState     = AL_IDLE;
  holding     = false;
  stopSound();
  lightOff();
  commSendAlarm("TIMEOUT");
  tempBonus   = 0;
  stageBonus  = 0;
  snoozeBonus = 0;
  snoozeUntil = 0;
  if (firedMin >= 0)
    snprintf(missedTxt, sizeof(missedTxt), "Alarme das %02d:%02d nao atendido",
             firedMin / 60, firedMin % 60);
  else
    snprintf(missedTxt, sizeof(missedTxt), "Alarme nao atendido");
  switchScreen(SCREEN_CLOCK);
  Serial.printf("[ALARM] %lu s tocando sem ninguem desligar: parou sozinho\n",
                rang / 1000UL);
}

/* Soneca: apaga tudo, endurece o proximo desafio e reagenda. */
static void snoozeAlarm() {
  alState = AL_IDLE;
  stopSound();
  lightOff();
  snoozeBonus++;
  settings.snoozeCount++;
  settings.lastAlarmDay = rtc.dayIndex;
  storageSave();
  snoozeUntil = millis() + SNOOZE_MS;
  commSendAlarm("SNOOZE");
  switchScreen(SCREEN_CLOCK);
  uiToast("Soneca: proximo nivel acima", C_WARN);
  Serial.printf("[ALARM] soneca #%d\n", settings.snoozeCount);
}

/* Escalona os estimulos conforme o tempo TOCANDO. */
static void updateEscalation() {
  unsigned long elapsed = ringElapsed();
  int want;

  if      (elapsed >= ESC_T3_MS) want = 4;
  else if (elapsed >= ESC_T2_MS) want = 3;
  else if (elapsed >= ESC_T1_MS) want = 2;
  else                           want = 1;

  if (want == stage) return;
  stage = want;

  if (stage >= 2) stageBonus = 1;
  if (stage == 4) commSendAlarm("ESCALATED");

  applyStimuli(stage);
  Serial.printf("[ALARM] estagio %d\n", stage);
}

/* Mede o dedo no botao DESLIGAR. */
static void updateDismissHold() {
  if (curScreen != SCREEN_ALARM_RINGING) return;
  unsigned long now = millis();
  int x, y;
  bool on = getTouchPoint(x, y) &&
            isTouched(DISMISS_X, DISMISS_Y, DISMISS_W, DISMISS_H, x, y);

  if (on) {
    holdLostMs = 0;
    if (!holding) { holding = true; holdStartMs = now; }
    float p = (float)(now - holdStartMs) / (float)DISMISS_HOLD_MS;
    uiRingHoldProgress(p);
    if (p >= 1.0f) dismissRing();
  } else if (holding) {
    if (!holdLostMs) holdLostMs = now;
    if (now - holdLostMs >= HOLD_GRACE_MS) {
      holding = false;
      uiRingHoldProgress(0);
    }
  }
}

/* Tempo do desafio e desfecho (acerto, erro ou tempo esgotado). */
static void updateChallenge() {
  challengeUpdate();
  const Challenge& c = challengeCurrent();
  unsigned long used = millis() - chalStartMs;

  if (c.phase != CHP_RIGHT && c.phase != CHP_WRONG && used >= chalTotalMs) {
    challengeTimeout();
  }

  // Desenha ANTES de tratar o desfecho: e aqui que CORRETO!/ERROU aparecem.
  unsigned long remain = used >= chalTotalMs ? 0 : chalTotalMs - used;
  uiChallengeUpdate(remain, chalTotalMs);

  if (c.phase == CHP_RIGHT) {
    if (resolveAtMs == 0) { resolveAtMs = millis() + 1500; return; }
    if ((long)(millis() - resolveAtMs) >= 0) { resolveAtMs = 0; stopAlarm(); }
    return;
  }

  if (c.phase == CHP_WRONG) {
    if (resolveAtMs == 0) { resolveAtMs = millis() + 1600; return; }
    if ((long)(millis() - resolveAtMs) >= 0) {
      resolveAtMs = 0;
      tempBonus++;                                   // errou -> +1 nivel
      resumeRing();
    }
  }
}

/* ═══════════════════════════════════════════════════════════════════
   Toque
   ═══════════════════════════════════════════════════════════════════ */

/* Proximo valor de uma lista circular. */
static int cycleNext(int cur, const int* opts, int n) {
  for (int i = 0; i < n; i++) if (opts[i] == cur) return opts[(i + 1) % n];
  return opts[0];
}

/* Aplica a acao de um botao da tela de configuracao do alarme. */
static void handleAlarmConfig(int a) {
  static const int RAMP[RAMP_MIN_COUNT]  = RAMP_MIN_OPTS;
  static const int CHAL[CHAL_TIME_COUNT] = CHAL_TIME_OPTS;
  static const int RMAX[RING_MAX_COUNT]  = RING_MAX_OPTS;
  switch (a) {
    case ACT_RINGMAX_CYCLE: settings.ringMaxMin = cycleNext(settings.ringMaxMin, RMAX, RING_MAX_COUNT); break;
    case ACT_LIGHT_UP: settings.lightPct = min(100, settings.lightPct + 10); break;
    case ACT_LIGHT_DN: settings.lightPct = max(WIZ_DIM_MIN, settings.lightPct - 10); break;
    case ACT_VIB_CYCLE: settings.vibMode = (settings.vibMode + 1) % 4;      break;
    case ACT_RAMP_CYCLE:   settings.rampMin   = cycleNext(settings.rampMin,   RAMP, RAMP_MIN_COUNT);  break;
    case ACT_CHTIME_CYCLE: settings.chalTimeS = cycleNext(settings.chalTimeS, CHAL, CHAL_TIME_COUNT); break;
    default:
      if (a >= ACT_DIFF_0 && a <= ACT_DIFF_3) settings.diffLevel = a - ACT_DIFF_0;
      if (a >= ACT_TYPE_0 && a <= ACT_TYPE_3) {
        // Marca/desmarca o tipo. O ultimo marcado nao pode sair.
        int bit = 1 << (a - ACT_TYPE_0);
        if (settings.chalMask == bit) {
          uiToast("Marque pelo menos 1 tipo", C_WARN);
          return;
        }
        settings.chalMask ^= bit;
      }
      break;
  }
  scheduleRedraw();
}

/* Lista de alarmes: editar, ligar/desligar, novo e opcoes. */
static void handleAlarmList(int a) {
  if (a >= ACT_ALM_EDIT_0 && a < ACT_ALM_EDIT_0 + MAX_ALARMS) {
    int i = a - ACT_ALM_EDIT_0;
    if (i >= settings.alarmCount) return;
    alarmDraft    = settings.alarms[i];
    alarmDraftIdx = i;
    switchScreen(SCREEN_ALARM_EDIT);
    return;
  }
  if (a >= ACT_ALM_EN_0 && a < ACT_ALM_EN_0 + MAX_ALARMS) {
    int i = a - ACT_ALM_EN_0;
    if (i >= settings.alarmCount) return;
    settings.alarms[i].enabled = !settings.alarms[i].enabled;
    storageSave();
    scheduleRedraw();
    return;
  }
  if (a == ACT_ALM_NEW) {
    if (settings.alarmCount >= MAX_ALARMS) {
      uiToast("Limite de 6 alarmes", C_WARN);
      return;
    }
    alarmDraft    = { 7, 0, DAYS_WEEK, 1 };
    alarmDraftIdx = -1;
    switchScreen(SCREEN_ALARM_EDIT);
  }
}

/* Edicao de um alarme (rascunho): hora, minuto, dias, ativo. */
static void handleAlarmEdit(int a) {
  Alarm& d = alarmDraft;
  switch (a) {
    case ACT_ALM_H_UP: d.hour   = (d.hour + 1) % 24;    break;
    case ACT_ALM_H_DN: d.hour   = (d.hour + 23) % 24;   break;
    case ACT_ALM_M_UP: d.minute = (d.minute + 1) % 60;  break;
    case ACT_ALM_M_DN: d.minute = (d.minute + 59) % 60; break;
    case ACT_ALM_TOGGLE:      d.enabled = !d.enabled;  break;
    case ACT_ALM_PRESET_WEEK: d.days = DAYS_WEEK;      break;
    case ACT_ALM_PRESET_ALL:  d.days = DAYS_ALL;       break;
    case ACT_ALM_PRESET_ONCE: d.days = DAYS_ONCE;      break;

    case ACT_ALM_SAVE_EDIT:
      if (alarmDraftIdx < 0) {
        if (settings.alarmCount >= MAX_ALARMS) { uiToast("Limite de 6 alarmes", C_WARN); return; }
        alarmDraftIdx = settings.alarmCount++;
      }
      settings.alarms[alarmDraftIdx] = d;
      storageSave();
      switchScreen(SCREEN_ALARM_LIST);
      uiToast(settings.demoMode ? "No demo os alarmes nao gravam" : "Alarme salvo",
              settings.demoMode ? C_WARN : C_OK);
      return;

    case ACT_ALM_DELETE:
      if (alarmDraftIdx >= 0 && alarmDraftIdx < settings.alarmCount) {
        for (int i = alarmDraftIdx; i < settings.alarmCount - 1; i++)
          settings.alarms[i] = settings.alarms[i + 1];
        settings.alarmCount--;
        storageSave();
        switchScreen(SCREEN_ALARM_LIST);
        uiToast("Alarme excluido", C_WARN);
      } else {
        switchScreen(SCREEN_ALARM_LIST);      // alarme novo: so descarta
      }
      return;

    default:
      if (a >= ACT_ALM_DAY_0 && a <= ACT_ALM_DAY_0 + 6) d.days ^= 1 << (a - ACT_ALM_DAY_0);
      break;
  }
  scheduleRedraw();
}

/* Aplica a acao de um botao da tela do modo noturno (passos de 30 min). */
static void handleNightConfig(int a) {
  switch (a) {
    case ACT_NIGHT_S_UP: settings.nightStart = (settings.nightStart + 30) % 1440;   break;
    case ACT_NIGHT_S_DN: settings.nightStart = (settings.nightStart + 1410) % 1440; break;
    case ACT_NIGHT_E_UP: settings.nightEnd   = (settings.nightEnd + 30) % 1440;     break;
    case ACT_NIGHT_E_DN: settings.nightEnd   = (settings.nightEnd + 1410) % 1440;   break;
    default:
      if (a >= ACT_NIGHT_MODE_0 && a <= ACT_NIGHT_MODE_2) settings.nightMode = a - ACT_NIGHT_MODE_0;
      break;
  }
  scheduleRedraw();
}

/* Aplica a acao de um botao da tela de rede. */
static void handleNetwork(int a) {
  switch (a) {
    case ACT_NET_PORTAL:
      if (!netApActive()) {
        netOpenPortal();
      } else if (netState() == NET_NO_CREDS) {
        uiToast("Sem rede salva: portal fica aberto", C_WARN);
        return;
      } else {
        netClosePortal();
      }
      scheduleRedraw();
      return;

    case ACT_NET_WIZ_FIND:
      if (!netConnected()) { uiToast("Conecte o Wi-Fi primeiro", C_WARN); return; }
      wizDiscover();
      uiToast("Procurando lampadas...", C_ACCENT);
      return;

    case ACT_NET_WIZ_TEST:
      if (!wizTargetIp()[0]) { uiToast("Nenhuma lampada encontrada", C_WARN); return; }
      wizTestBlink();
      uiToast("Piscando a lampada", C_ACCENT);
      return;
  }
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
    case ACT_T_WD_UP: rtc.setWeekday(rtc.weekday + 1); break;
    case ACT_T_WD_DN: rtc.setWeekday(rtc.weekday + 6); break;
  }
  scheduleRedraw();
}

/* Roteia um toque curto conforme a tela ativa. */
static void handleTap(int x, int y) {
  // Qualquer toque apaga o aviso de alarme nao atendido.
  if (missedTxt[0]) {
    missedTxt[0] = '\0';
    if (curScreen == SCREEN_CLOCK) { switchScreen(SCREEN_CLOCK); return; }
  }

  int a = uiHitTest(x, y);
  if (a == ACT_NONE) return;
  uiFlashAction(a);

  // Alternativas do desafio.
  if (a >= ACT_OPT_BASE) {
    if (curScreen == SCREEN_ALARM_ACTIVE) challengeTouchIndex(a - ACT_OPT_BASE);
    return;
  }

  switch (a) {
    case ACT_NIGHT_PEEK:
      peekUntil = millis() + NIGHT_PEEK_MS;
      switchScreen(SCREEN_CLOCK);
      return;

    case ACT_OPEN_ALARM_CFG: switchScreen(SCREEN_ALARM_LIST);   return;
    case ACT_OPEN_SET_TIME:  switchScreen(SCREEN_SET_TIME);     return;
    case ACT_GO_ALARM_CFG:   switchScreen(SCREEN_ALARM_LIST);   return;
    case ACT_GO_ALARM_LIST:  switchScreen(SCREEN_ALARM_LIST);   return;
    case ACT_GO_ALARM_OPTS:  switchScreen(SCREEN_ALARM_CONFIG); return;
    case ACT_GO_NIGHT_CFG:   switchScreen(SCREEN_NIGHT_CONFIG); return;
    case ACT_GO_NETWORK:     switchScreen(SCREEN_NETWORK);      return;
    case ACT_GO_SET_TIME:    switchScreen(SCREEN_SET_TIME);     return;

    case ACT_TEST_ALARM:
      lastFiredKey = -1;
      startAlarm();
      return;

    case ACT_SAVE:
      storageSave();
      uiToast(settings.demoMode ? "Salvo (alarme do demo nao grava)"
                                : "Configuracoes salvas", C_OK);
      return;

    case ACT_DEMO_TOGGLE: {
      bool on = !settings.demoMode;
      appSetDemo(on);
      switchScreen(SCREEN_SETTINGS);
      if (on)                   uiToast("DEMO: 07:29, alarme 07:30", C_WARN);
      else if (netTimeSynced()) uiToast("PRODUCAO: hora pela internet", C_OK);
      else                      uiToast("PRODUCAO: ajuste a hora", C_WARN);
      return;
    }

    case ACT_BACK:
    case ACT_T_CONFIRM:
      switchScreen(SCREEN_CLOCK);
      return;

    case ACT_SNOOZE:
      if (alState == AL_RINGING) snoozeAlarm();
      return;

    case ACT_DISMISS_HOLD:      // toque curto no DESLIGAR: precisa segurar
      return;

    case ACT_EARLY_TAP:
      if (curScreen == SCREEN_ALARM_ACTIVE) challengeEarlyTap();
      return;
  }

  if      (curScreen == SCREEN_ALARM_CONFIG) handleAlarmConfig(a);
  else if (curScreen == SCREEN_ALARM_LIST)   handleAlarmList(a);
  else if (curScreen == SCREEN_ALARM_EDIT)   handleAlarmEdit(a);
  else if (curScreen == SCREEN_NIGHT_CONFIG) handleNightConfig(a);
  else if (curScreen == SCREEN_NETWORK)      handleNetwork(a);
  else if (curScreen == SCREEN_SET_TIME)     handleSetTime(a);
}

/* Telas de ajuste esquecidas abertas voltam ao relogio (e ao modo noturno). */
static void checkIdleReturn() {
  bool cfg = curScreen == SCREEN_SETTINGS || curScreen == SCREEN_ALARM_CONFIG ||
             curScreen == SCREEN_ALARM_LIST || curScreen == SCREEN_ALARM_EDIT ||
             curScreen == SCREEN_NIGHT_CONFIG || curScreen == SCREEN_NETWORK ||
             curScreen == SCREEN_SET_TIME;
  if (cfg && millis() - lastTouchMs >= CONFIG_IDLE_MS) {
    Serial.println(F("[UI] sem toques: voltando ao relogio"));
    switchScreen(SCREEN_CLOCK);
  }
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
   *   2. storageBegin() vem antes do touch e da rede: e da NVS que saem a
   *      calibracao, o Wi-Fi e o IP da lampada.
   *   3. audioBegin() deixa o amplificador (IO4) mudo desde o boot.
   *   4. touchBegin() sobe o SPI proprio do XPT2046 e, se precisar, calibra
   *      na tela — por isso depende da UI e do storage ja estarem de pe.
   *   5. netBegin() antes do portalBegin(): o servidor HTTP precisa da
   *      pilha de rede iniciada.
   */
  uiBegin();             // tft.init() + rotacao + LEDC do backlight
  storageBegin();
  audioBegin();          // configura PIN_AMP_ENABLE e o DAC
  touchBegin();          // pode calibrar (unico ponto bloqueante, so no boot)
  commBegin();           // Serial2 nos pinos corretos + LED azul de status
  challengeInit();
  nightBegin();

  if (settings.demoMode) {
    rtc.begin(DEMO_START_H, DEMO_START_M, DEMO_START_S);
    Serial.println(F("[BOOT] modo DEMO: alarme dispara em ~1 minuto"));
  } else {
    rtc.begin(0, 0, 0);
    Serial.println(F("[BOOT] modo PRODUCAO"));
  }

  netBegin();            // Wi-Fi da casa ou portal "Despertador-XXXX"
  portalBegin();         // http://despertador.local / 192.168.4.1
  wizBegin();

  nightActive = nightShouldBeActive();
  lastTouchMs = millis();
  switchScreen(SCREEN_CLOCK);
  if (!touchAvailable()) uiToast("Touch indisponivel", C_DANGER);
  else if (settings.demoMode) uiToast("MODO DEMO: alarme as 07:30", C_WARN);
  else if (netState() == NET_NO_CREDS) uiToast("Wi-Fi: conecte em Despertador-...", C_WARN);
}

/* ═══════════════════════════════════════════════════════════════════
   Loop
   ═══════════════════════════════════════════════════════════════════ */
void loop() {
  commPoll();
  netUpdate();
  portalUpdate();
  wizUpdate();
  touchUpdate();
  uiFlashUpdate();

  /* Redesenho adiado, para o piscar do botao ficar visivel. */
  if (redrawAtMs && (long)(millis() - redrawAtMs) >= 0) {
    redrawAtMs = 0;
    uiDrawScreen(curScreen);
  }

  /* Configuracao mudou pelo portal: redesenha a tela parada. */
  if (settingsDirty) {
    settingsDirty = false;
    if (alState == AL_IDLE) switchScreen(curScreen);
  }

  /* Toques. */
  int tx, ty;
  if (getTouchPoint(tx, ty)) lastTouchMs = millis();
  if (touchTap(tx, ty)) {
    lastTouchMs = millis();
    if (peeking()) peekUntil = millis() + NIGHT_PEEK_MS;   // mexendo: estende
    handleTap(tx, ty);
  }
  if (touchLongPress(tx, ty)) {
    lastTouchMs = millis();
    if (curScreen == SCREEN_CLOCK) switchScreen(SCREEN_SETTINGS);
  }

  /* Relogio: tudo que roda 1x por segundo. */
  if (rtc.tick()) {
    if (curScreen == SCREEN_CLOCK) uiClockTick();

    // Algum alarme da lista bate com o minuto e o dia da semana?
    int key = rtc.dayIndex * 1440 + rtc.hour * 60 + rtc.minute;
    if (alState == AL_IDLE && key != lastFiredKey) {
      for (int i = 0; i < settings.alarmCount; i++) {
        if (!alarmDueNow(settings.alarms[i], rtc.weekday, rtc.hour, rtc.minute)) continue;
        lastFiredKey = key;
        if (settings.alarms[i].days == DAYS_ONCE) {   // "uma vez": se desativa
          settings.alarms[i].enabled = 0;
          storageSave();
        }
        Serial.printf("[ALARM] alarme %d (%02d:%02d %s)\n", i + 1,
                      settings.alarms[i].hour, settings.alarms[i].minute,
                      alarmDaysText(settings.alarms[i].days));
        startAlarm();
        break;
      }
    }

    updateSunrise();
    refreshNight();
    uiNetworkTick();
    checkIdleReturn();
  }

  /* Soneca vencida. */
  if (alState == AL_IDLE && snoozeUntil && (long)(millis() - snoozeUntil) >= 0) {
    snoozeUntil = 0;
    startAlarm();
  }

  /* Alarme tocando. */
  if (alState == AL_RINGING) {
    updateEscalation();
    if (ringMaxMs() && ringElapsed() >= ringMaxMs()) timeoutAlarm();
  }
  if (alState == AL_RINGING) {
    updateDismissHold();

    if (millis() - lastFlashMs >= 500) {      // piscar do cabecalho
      lastFlashMs = millis();
      flashState  = !flashState;
    }
    uiRingingUpdate(stage, ringElapsed(), effectiveLevel(), flashState);
  } else if (alState == AL_CHALLENGE) {
    updateChallenge();
  }

  /* Fim do teste de volume do portal. */
  if (soundTestUntil && (long)(millis() - soundTestUntil) >= 0) {
    soundTestUntil = 0;
    if (alState == AL_IDLE) stopAudio();
  }

  /* Fim do teste de vibracao do portal. */
  if (vibTestUntil && (long)(millis() - vibTestUntil) >= 0) {
    vibTestUntil = 0;
    if (alState == AL_IDLE) commSendVib("OFF");
  }

  /* Status periodico do Actuator. */
  if (millis() - lastStatusMs >= COMM_POLL_MS) {
    lastStatusMs = millis();
    commRequestStatus();
  }
}
