/*
 * storage.cpp — Le e grava as configuracoes na NVS Flash via Preferences.
 */
#include <Preferences.h>
#include "storage.h"

static Preferences prefs;
static const char* NS = "heavysleep";   // namespace da NVS

Settings  settings;
NetConfig netcfg;

/* Valores validos para as opcoes de ciclo (mesma ordem da tela). */
static const int CHAL_OPTS[CHAL_TIME_COUNT] = CHAL_TIME_OPTS;
static const int RAMP_OPTS[RAMP_MIN_COUNT]  = RAMP_MIN_OPTS;
static const int RING_OPTS[RING_MAX_COUNT]  = RING_MAX_OPTS;

static bool inList(int v, const int* list, int n) {
  for (int i = 0; i < n; i++) if (list[i] == v) return true;
  return false;
}

/* Aplica os valores padrao de fabrica na struct em memoria. */
/* Lista de fabrica: um alarme 07:00 todos os dias (como a v0.4). */
static void defaultAlarms() {
  memset(settings.alarms, 0, sizeof(settings.alarms));
  settings.alarmCount = 1;
  settings.alarms[0]  = { 7, 0, DAYS_ALL, 1 };
}

void storageResetDefaults() {
  defaultAlarms();
  settings.ringMaxMin   = RING_MAX_DEFAULT;
  settings.diffLevel    = DIFF_EASY;
  settings.chalMask     = 1 << CHAL_MATH;
  settings.lightPct     = 100;
  settings.vibMode      = VIB_MODE_WEAK;
  static const int VD[3] = VIB_DUTY_DEFAULTS;
  for (int i = 0; i < 3; i++) settings.vibDuty[i] = VD[i];
  settings.snoozeCount  = 0;
  settings.lastAlarmDay = -1;
  settings.rampMin      = 10;
  settings.chalTimeS    = 60;
  settings.nightMode    = NIGHT_AUTO;
  settings.nightStart   = 22 * 60;
  settings.nightEnd     = 6 * 60;
  settings.demoMode     = DEMO_DEFAULT;
}

bool demoFastTimes() { return DEMO_FAST_TIMES && settings.demoMode; }

/* Alarme da apresentacao: so na RAM, nunca vai para a NVS. */
static void applyDemoAlarm() {
  memset(settings.alarms, 0, sizeof(settings.alarms));
  settings.alarmCount = 1;
  settings.alarms[0]  = { DEMO_ALM_H, DEMO_ALM_M, DAYS_ALL, 1 };
}

/* Le a lista de alarmes da NVS (no boot e ao sair do demo). */
static void loadAlarms() {
  if (prefs.isKey("alarm_n")) {
    int   n = prefs.getInt("alarm_n", 0);
    Alarm tmp[MAX_ALARMS];
    memset(tmp, 0, sizeof(tmp));
    bool ok = n >= 0 && n <= MAX_ALARMS &&
              (n == 0 || prefs.getBytesLength("alarms") == sizeof(Alarm) * n);
    if (ok && n > 0) prefs.getBytes("alarms", tmp, sizeof(Alarm) * n);
    for (int i = 0; ok && i < n; i++) if (!alarmValid(tmp[i])) ok = false;
    if (ok) {
      settings.alarmCount = n;
      memcpy(settings.alarms, tmp, sizeof(tmp));
      return;
    }
    Serial.println(F("[STORAGE] lista de alarmes corrompida -> padrao"));
  } else if (prefs.isKey("alm_hour")) {
    // v0.5: o alarme unico das versoes antigas (chaves alm_*) vira o
    // primeiro da lista, tocando todos os dias como antes.
    memset(settings.alarms, 0, sizeof(settings.alarms));
    settings.alarmCount = 1;
    settings.alarms[0].hour    = constrain(prefs.getInt("alm_hour", 7), 0, 23);
    settings.alarms[0].minute  = constrain(prefs.getInt("alm_min", 0), 0, 59);
    settings.alarms[0].days    = DAYS_ALL;
    settings.alarms[0].enabled = prefs.getBool("alm_enabled", true) ? 1 : 0;
    return;
  }
  defaultAlarms();
}

/* Grava a lista de alarmes (fora do demo). */
static void saveAlarms() {
  prefs.putInt("alarm_n", settings.alarmCount);
  if (settings.alarmCount > 0)
    prefs.putBytes("alarms", settings.alarms, sizeof(Alarm) * settings.alarmCount);
  else
    prefs.remove("alarms");
}

/* Rede: vazia, lampada em modo automatico e fuso de Brasilia. */
static void netDefaults() {
  memset(&netcfg, 0, sizeof(netcfg));
  strncpy(netcfg.tz, TZ_DEFAULT, sizeof(netcfg.tz) - 1);
}

/* Garante que valores lidos da NVS estejam dentro da faixa valida. */
static void sanitize() {
  bool bad = false;
  if (!inList(settings.ringMaxMin, RING_OPTS, RING_MAX_COUNT)) bad = true;
  if (settings.diffLevel < 0 || settings.diffLevel > 3)  bad = true;
  if (settings.chalMask  < 1 || settings.chalMask  > CHAL_MASK_ALL) bad = true;
  if (settings.lightPct  < WIZ_DIM_MIN || settings.lightPct > 100) bad = true;
  if (settings.vibMode   < 0 || settings.vibMode   > 3)  bad = true;
  for (int i = 0; i < 3; i++)
    if (settings.vibDuty[i] < 0 || settings.vibDuty[i] > 100) bad = true;
  if (settings.snoozeCount < 0) bad = true;
  if (!inList(settings.rampMin,   RAMP_OPTS, RAMP_MIN_COUNT))  bad = true;
  if (!inList(settings.chalTimeS, CHAL_OPTS, CHAL_TIME_COUNT)) bad = true;
  if (settings.nightMode  < 0 || settings.nightMode  > 2)        bad = true;
  if (settings.nightStart < 0 || settings.nightStart >= 24 * 60) bad = true;
  if (settings.nightEnd   < 0 || settings.nightEnd   >= 24 * 60) bad = true;

  if (bad) {
    Serial.println(F("[STORAGE] Preferences corrompida -> restaurando padroes"));
    Alarm keep[MAX_ALARMS];                     // a lista tem validacao propria
    int   keepN = settings.alarmCount;
    memcpy(keep, settings.alarms, sizeof(keep));
    storageResetDefaults();
    settings.alarmCount = keepN;
    memcpy(settings.alarms, keep, sizeof(keep));
    storageSave();
  }
}

/* Abre a NVS e carrega todas as chaves (usando padroes se ausentes). */
void storageBegin() {
  storageResetDefaults();
  netDefaults();

  if (!prefs.begin(NS, false)) {
    Serial.println(F("[STORAGE] Falha ao abrir NVS -> usando padroes em RAM"));
    return;
  }

  loadAlarms();
  settings.ringMaxMin   = prefs.getInt ("ring_max",      settings.ringMaxMin);
  settings.diffLevel    = prefs.getInt ("diff_level",    settings.diffLevel);
  // v0.4.1: varios tipos de desafio (chave nova chal_mask). Quem vem da
  // chave antiga diff_type (um tipo so, 0-3) vira a mascara desse tipo.
  if (prefs.isKey("chal_mask")) {
    settings.chalMask = prefs.getInt("chal_mask", settings.chalMask);
  } else if (prefs.isKey("diff_type")) {
    int old = prefs.getInt("diff_type", 0);
    if (old >= 0 && old <= 3) settings.chalMask = 1 << old;
  }
  settings.lightPct     = prefs.getInt ("light_pct",     settings.lightPct);
  // v0.4.1: a vibracao ganhou o nivel MEDIA (chave nova vib_lvl). Quem vem
  // da chave antiga vib_mode (0 off, 1 fraca, 2 forte) e convertido.
  if (prefs.isKey("vib_lvl")) {
    settings.vibMode = prefs.getInt("vib_lvl", settings.vibMode);
  } else if (prefs.isKey("vib_mode")) {
    int old = prefs.getInt("vib_mode", 1);
    settings.vibMode = (old == 2) ? VIB_MODE_MAX : old;
  }
  settings.vibDuty[0]   = prefs.getInt ("vib_d0",        settings.vibDuty[0]);
  settings.vibDuty[1]   = prefs.getInt ("vib_d1",        settings.vibDuty[1]);
  settings.vibDuty[2]   = prefs.getInt ("vib_d2",        settings.vibDuty[2]);
  settings.snoozeCount  = prefs.getInt ("snooze_count",  settings.snoozeCount);
  settings.lastAlarmDay = prefs.getInt ("last_alarm_day", settings.lastAlarmDay);
  settings.rampMin      = prefs.getInt ("ramp_min",      settings.rampMin);
  settings.chalTimeS    = prefs.getInt ("chal_time_s",   settings.chalTimeS);
  settings.nightMode    = prefs.getInt ("night_mode",    settings.nightMode);
  settings.nightStart   = prefs.getInt ("night_start",   settings.nightStart);
  settings.nightEnd     = prefs.getInt ("night_end",     settings.nightEnd);
  settings.demoMode     = prefs.getBool("demo_mode",     settings.demoMode);

  // Chaves de rede. isKey() antes de ler evita o log de erro da NVS
  // ("NOT_FOUND") no primeiro boot, quando elas ainda nao existem.
  if (prefs.isKey("wifi_ssid")) prefs.getString("wifi_ssid", netcfg.ssid,  sizeof(netcfg.ssid));
  if (prefs.isKey("wifi_pass")) prefs.getString("wifi_pass", netcfg.pass,  sizeof(netcfg.pass));
  if (prefs.isKey("wiz_ip"))    prefs.getString("wiz_ip",    netcfg.wizIp, sizeof(netcfg.wizIp));
  if (prefs.isKey("tz"))        prefs.getString("tz",        netcfg.tz,    sizeof(netcfg.tz));
  if (!netcfg.tz[0]) strncpy(netcfg.tz, TZ_DEFAULT, sizeof(netcfg.tz) - 1);

  sanitize();

  // Em modo demo o alarme e forcado para o horario da apresentacao.
  if (settings.demoMode) applyDemoAlarm();

  Serial.printf("[STORAGE] alarmes=%d max=%dmin nivel=%d tipos=0x%X luz=%d%% vib=%d "
                "rampa=%dmin desafio=%ds noite=%d modo=%s wifi='%s' wiz='%s'\n",
                settings.alarmCount, settings.ringMaxMin,
                settings.diffLevel, settings.chalMask,
                settings.lightPct, settings.vibMode,
                settings.rampMin, settings.chalTimeS, settings.nightMode,
                settings.demoMode ? "DEMO" : "PRODUCAO", netcfg.ssid, netcfg.wizIp[0] ? netcfg.wizIp : "auto");
}

/* Grava a struct inteira de volta na NVS. */
void storageSave() {
  if (!settings.demoMode) saveAlarms();   // no demo a lista e a da apresentacao
  prefs.putInt ("ring_max",       settings.ringMaxMin);
  prefs.putInt ("diff_level",     settings.diffLevel);
  prefs.putInt ("chal_mask",      settings.chalMask);
  prefs.putInt ("light_pct",      settings.lightPct);
  prefs.putInt ("vib_lvl",        settings.vibMode);
  prefs.putInt ("vib_d0",         settings.vibDuty[0]);
  prefs.putInt ("vib_d1",         settings.vibDuty[1]);
  prefs.putInt ("vib_d2",         settings.vibDuty[2]);
  prefs.putInt ("snooze_count",   settings.snoozeCount);
  prefs.putInt ("last_alarm_day", settings.lastAlarmDay);
  prefs.putInt ("ramp_min",       settings.rampMin);
  prefs.putInt ("chal_time_s",    settings.chalTimeS);
  prefs.putInt ("night_mode",     settings.nightMode);
  prefs.putInt ("night_start",    settings.nightStart);
  prefs.putInt ("night_end",      settings.nightEnd);
  prefs.putBool("demo_mode",      settings.demoMode);
  Serial.println(F("[STORAGE] configuracoes salvas"));
}

/* Liga/desliga o demo: grava a escolha e troca o alarme em RAM. */
void storageSetDemo(bool on) {
  if (on == settings.demoMode) return;
  if (on) {
    storageSave();                // garante a lista de verdade na NVS antes
    settings.demoMode = true;
    applyDemoAlarm();
  } else {
    settings.demoMode = false;
    loadAlarms();
  }
  prefs.putBool("demo_mode", settings.demoMode);
  Serial.printf("[STORAGE] modo %s (%d alarme(s))\n",
                on ? "DEMO" : "PRODUCAO", settings.alarmCount);
}

/* Grava SSID, senha, IP da lampada e fuso. */
void storageSaveNet() {
  prefs.putString("wifi_ssid", netcfg.ssid);
  prefs.putString("wifi_pass", netcfg.pass);
  prefs.putString("wiz_ip",    netcfg.wizIp);
  prefs.putString("tz",        netcfg.tz);
  Serial.println(F("[STORAGE] rede salva"));
}

/* Carrega o array de calibracao do touch; false se nunca foi calibrado. */
bool storageLoadTouchCal(uint16_t cal[5]) {
  size_t len = prefs.getBytesLength(TOUCH_CAL_KEY);
  if (len != sizeof(uint16_t) * 5) return false;
  prefs.getBytes(TOUCH_CAL_KEY, cal, len);
  return true;
}

/* Salva o array de calibracao do touch (uint16_t[5]). */
void storageSaveTouchCal(const uint16_t cal[5]) {
  prefs.putBytes(TOUCH_CAL_KEY, cal, sizeof(uint16_t) * 5);
  Serial.println(F("[STORAGE] calibracao do touch salva"));
}

/* Apaga a calibracao para que o proximo boot recalibre. */
void storageClearTouchCal() {
  prefs.remove(TOUCH_CAL_KEY);
}
