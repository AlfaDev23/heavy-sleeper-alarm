/*
 * night.cpp — Regra do modo noturno (horario ou sensor de luz).
 */
#include "night.h"
#include "storage.h"
#include "clock.h"

#ifdef PIN_LDR
static bool ldrDark = false;     // ultimo estado, para a histerese
#endif

void nightBegin() {
#ifdef PIN_LDR
  pinMode(PIN_LDR, INPUT);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);   // faixa 0-3,3 V
#endif
}

/* True se o minuto do dia cai dentro da janela noturna (pode virar a meia-noite). */
bool nightInSchedule(int m) {
  int a = settings.nightStart, b = settings.nightEnd;
  if (a == b) return false;              // janela vazia
  if (a < b)  return m >= a && m < b;    // ex. 01:00 -> 05:00
  return m >= a || m < b;                // ex. 22:00 -> 06:00
}

bool nightShouldBeActive() {
  switch (settings.nightMode) {
    case NIGHT_FORCE_DAY:   return false;
    case NIGHT_FORCE_NIGHT: return true;
    default: break;
  }
#ifdef PIN_LDR
  int v = analogRead(PIN_LDR);
  if (ldrDark  && v > LDR_DARK_ADC + LDR_HYST_ADC) ldrDark = false;
  if (!ldrDark && v < LDR_DARK_ADC)                 ldrDark = true;
  return ldrDark;
#else
  return nightInSchedule(rtc.minuteOfDay());
#endif
}

const char* nightModeName(int mode) {
  switch (mode) {
    case NIGHT_FORCE_DAY:   return "DIA";
    case NIGHT_FORCE_NIGHT: return "NOITE";
    default:                return "AUTO";
  }
}
