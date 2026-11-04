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

static bool inList(int v, const int* list, int n) {
  for (int i = 0; i < n; i++) if (list[i] == v) return true;
  return false;
}

/* Aplica os valores padrao de fabrica na struct em memoria. */
void storageResetDefaults() {
  settings.almHour      = 7;
  settings.almMin       = 0;
  settings.almEnabled   = true;
  settings.diffLevel    = DIFF_EASY;
  settings.diffType     = CHAL_MATH;
  settings.lightPct     = 100;
  settings.vibMode      = VIB_MODE_WEAK;
  settings.snoozeCount  = 0;
  settings.lastAlarmDay = -1;
  settings.rampMin      = 10;
  settings.chalTimeS    = 60;
  settings.nightMode    = NIGHT_AUTO;
  settings.nightStart   = 22 * 60;
  settings.nightEnd     = 6 * 60;
}

/* Rede: vazia, lampada em modo automatico e fuso de Brasilia. */
static void netDefaults() {
  memset(&netcfg, 0, sizeof(netcfg));
  strncpy(netcfg.tz, TZ_DEFAULT, sizeof(netcfg.tz) - 1);
}

/* Garante que valores lidos da NVS estejam dentro da faixa valida. */
static void sanitize() {
  bool bad = false;
  if (settings.almHour   < 0 || settings.almHour   > 23) bad = true;
  if (settings.almMin    < 0 || settings.almMin    > 59) bad = true;
  if (settings.diffLevel < 0 || settings.diffLevel > 3)  bad = true;
  if (settings.diffType  < 0 || settings.diffType  > 3)  bad = true;
  if (settings.lightPct  < WIZ_DIM_MIN || settings.lightPct > 100) bad = true;
  if (settings.vibMode   < 0 || settings.vibMode   > 2)  bad = true;
  if (settings.snoozeCount < 0) bad = true;
  if (!inList(settings.rampMin,   RAMP_OPTS, RAMP_MIN_COUNT))  bad = true;
  if (!inList(settings.chalTimeS, CHAL_OPTS, CHAL_TIME_COUNT)) bad = true;
  if (settings.nightMode  < 0 || settings.nightMode  > 2)        bad = true;
  if (settings.nightStart < 0 || settings.nightStart >= 24 * 60) bad = true;
  if (settings.nightEnd   < 0 || settings.nightEnd   >= 24 * 60) bad = true;

  if (bad) {
    Serial.println(F("[STORAGE] Preferences corrompida -> restaurando padroes"));
    storageResetDefaults();
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

  settings.almHour      = prefs.getInt ("alm_hour",      settings.almHour);
  settings.almMin       = prefs.getInt ("alm_min",       settings.almMin);
  settings.almEnabled   = prefs.getBool("alm_enabled",   settings.almEnabled);
  settings.diffLevel    = prefs.getInt ("diff_level",    settings.diffLevel);
  settings.diffType     = prefs.getInt ("diff_type",     settings.diffType);
  settings.lightPct     = prefs.getInt ("light_pct",     settings.lightPct);
  settings.vibMode      = prefs.getInt ("vib_mode",      settings.vibMode);
  settings.snoozeCount  = prefs.getInt ("snooze_count",  settings.snoozeCount);
  settings.lastAlarmDay = prefs.getInt ("last_alarm_day", settings.lastAlarmDay);
  settings.rampMin      = prefs.getInt ("ramp_min",      settings.rampMin);
  settings.chalTimeS    = prefs.getInt ("chal_time_s",   settings.chalTimeS);
  settings.nightMode    = prefs.getInt ("night_mode",    settings.nightMode);
  settings.nightStart   = prefs.getInt ("night_start",   settings.nightStart);
  settings.nightEnd     = prefs.getInt ("night_end",     settings.nightEnd);

  // Chaves de rede. isKey() antes de ler evita o log de erro da NVS
  // ("NOT_FOUND") no primeiro boot, quando elas ainda nao existem.
  if (prefs.isKey("wifi_ssid")) prefs.getString("wifi_ssid", netcfg.ssid,  sizeof(netcfg.ssid));
  if (prefs.isKey("wifi_pass")) prefs.getString("wifi_pass", netcfg.pass,  sizeof(netcfg.pass));
  if (prefs.isKey("wiz_ip"))    prefs.getString("wiz_ip",    netcfg.wizIp, sizeof(netcfg.wizIp));
  if (prefs.isKey("tz"))        prefs.getString("tz",        netcfg.tz,    sizeof(netcfg.tz));
  if (!netcfg.tz[0]) strncpy(netcfg.tz, TZ_DEFAULT, sizeof(netcfg.tz) - 1);

  sanitize();

#ifdef DEMO_MODE
  // Em modo demo o alarme e forcado para o horario da apresentacao.
  settings.almHour    = DEMO_ALM_H;
  settings.almMin     = DEMO_ALM_M;
  settings.almEnabled = true;
#endif

  Serial.printf("[STORAGE] alarme=%02d:%02d en=%d nivel=%d tipo=%d luz=%d%% vib=%d "
                "rampa=%dmin desafio=%ds noite=%d wifi='%s' wiz='%s'\n",
                settings.almHour, settings.almMin, settings.almEnabled,
                settings.diffLevel, settings.diffType,
                settings.lightPct, settings.vibMode,
                settings.rampMin, settings.chalTimeS, settings.nightMode,
                netcfg.ssid, netcfg.wizIp[0] ? netcfg.wizIp : "auto");
}

/* Grava a struct inteira de volta na NVS. */
void storageSave() {
  prefs.putInt ("alm_hour",       settings.almHour);
  prefs.putInt ("alm_min",        settings.almMin);
  prefs.putBool("alm_enabled",    settings.almEnabled);
  prefs.putInt ("diff_level",     settings.diffLevel);
  prefs.putInt ("diff_type",      settings.diffType);
  prefs.putInt ("light_pct",      settings.lightPct);
  prefs.putInt ("vib_mode",       settings.vibMode);
  prefs.putInt ("snooze_count",   settings.snoozeCount);
  prefs.putInt ("last_alarm_day", settings.lastAlarmDay);
  prefs.putInt ("ramp_min",       settings.rampMin);
  prefs.putInt ("chal_time_s",    settings.chalTimeS);
  prefs.putInt ("night_mode",     settings.nightMode);
  prefs.putInt ("night_start",    settings.nightStart);
  prefs.putInt ("night_end",      settings.nightEnd);
  Serial.println(F("[STORAGE] configuracoes salvas"));
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
