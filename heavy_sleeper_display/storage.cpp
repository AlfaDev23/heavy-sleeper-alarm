/*
 * storage.cpp — Le e grava as configuracoes na NVS Flash via Preferences.
 */
#include <Preferences.h>
#include "storage.h"

static Preferences prefs;
static const char* NS = "heavysleep";   // namespace da NVS

Settings settings;

/* Aplica os valores padrao de fabrica na struct em memoria. */
void storageResetDefaults() {
  settings.almHour      = 7;
  settings.almMin       = 0;
  settings.almEnabled   = true;
  settings.diffLevel    = DIFF_EASY;
  settings.diffType     = CHAL_MATH;
  settings.lightPct     = 50;
  settings.vibMode      = VIB_MODE_WEAK;
  settings.snoozeCount  = 0;
  settings.lastAlarmDay = -1;
}

/* Garante que valores lidos da NVS estejam dentro da faixa valida. */
static void sanitize() {
  bool bad = false;
  if (settings.almHour   < 0 || settings.almHour   > 23) bad = true;
  if (settings.almMin    < 0 || settings.almMin    > 59) bad = true;
  if (settings.diffLevel < 0 || settings.diffLevel > 3)  bad = true;
  if (settings.diffType  < 0 || settings.diffType  > 3)  bad = true;
  if (settings.lightPct  < 0 || settings.lightPct  > 100) bad = true;
  if (settings.vibMode   < 0 || settings.vibMode   > 2)  bad = true;
  if (settings.snoozeCount < 0) bad = true;

  if (bad) {
    Serial.println(F("[STORAGE] Preferences corrompida -> restaurando padroes"));
    storageResetDefaults();
    storageSave();
  }
}

/* Abre a NVS e carrega todas as chaves (usando padroes se ausentes). */
void storageBegin() {
  storageResetDefaults();

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

  sanitize();

#ifdef DEMO_MODE
  // Em modo demo o alarme e forcado para o horario da apresentacao.
  settings.almHour    = DEMO_ALM_H;
  settings.almMin     = DEMO_ALM_M;
  settings.almEnabled = true;
#endif

  Serial.printf("[STORAGE] alarme=%02d:%02d en=%d nivel=%d tipo=%d luz=%d%% vib=%d\n",
                settings.almHour, settings.almMin, settings.almEnabled,
                settings.diffLevel, settings.diffType,
                settings.lightPct, settings.vibMode);
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
  Serial.println(F("[STORAGE] configuracoes salvas"));
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
