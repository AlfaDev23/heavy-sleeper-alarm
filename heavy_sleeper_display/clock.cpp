/*
 * clock.cpp — Contagem de tempo baseada em millis(), sem delay().
 */
#include "clock.h"

TimeKeeper rtc;

/* Define a hora inicial e zera o cronometro de tick. */
void TimeKeeper::begin(int h, int m, int s) {
  setTime(h, m, s);
  dayIndex   = 0;
  lastTickMs = millis();
}

/* Ajusta a hora atual, normalizando os valores. */
void TimeKeeper::setTime(int h, int m, int s) {
  hour   = constrain(h, 0, 23);
  minute = constrain(m, 0, 59);
  second = constrain(s, 0, 59);
}

/*
 * Avanca o relogio se ja passou 1 segundo. Retorna true nesse caso.
 * Usa lastTickMs += 1000 (em vez de = millis()) para nao acumular drift.
 */
bool TimeKeeper::tick() {
  if (millis() - lastTickMs < 1000UL) return false;
  lastTickMs += 1000UL;

  if (++second >= 60) {
    second = 0;
    if (++minute >= 60) {
      minute = 0;
      if (++hour >= 24) {
        hour = 0;
        dayIndex++;
      }
    }
  }
  return true;
}

/* True quando hora e minuto atuais coincidem com o alarme configurado. */
bool TimeKeeper::isAlarmTime(int almH, int almM) const {
  return (hour == almH) && (minute == almM);
}

/* Soma minutos ao relogio (soneca), tratando viradas de hora e dia. */
void TimeKeeper::addMinutes(int mins) {
  int total = hour * 60 + minute + mins;
  while (total >= 24 * 60) { total -= 24 * 60; dayIndex++; }
  while (total < 0)        { total += 24 * 60; }
  hour   = total / 60;
  minute = total % 60;
}
