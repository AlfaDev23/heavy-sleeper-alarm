/*
 * clock.cpp — Contagem de tempo baseada em millis(), sem delay().
 */
#include "clock.h"

TimeKeeper rtc;

/* Define a hora inicial e zera o cronometro de tick. */
void TimeKeeper::begin(int h, int m, int s) {
  setTime(h, m, s);
  dayIndex   = 0;
  synced     = false;
  lastTickMs = millis();
}

/* Acerto vindo do NTP (chamado pelo net.cpp a cada NTP_RESYNC_MS). */
void TimeKeeper::syncTo(int h, int m, int s, int wday) {
  long before = secondsOfDay();
  setTime(h, m, s);
  long after = secondsOfDay();
  if (before - after > 12L * 3600L) {             // virou o dia no acerto
    dayIndex++;
    weekday = (weekday + 1) % 7;
  }
  if (wday >= 0) weekday = wday % 7;              // o NTP sabe o dia certo
  lastTickMs = millis();                          // segundo comeca agora
  synced = true;
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
        weekday = (weekday + 1) % 7;
      }
    }
  }
  return true;
}

/* Dia da semana manual (sem internet). */
void TimeKeeper::setWeekday(int wd) {
  weekday = ((wd % 7) + 7) % 7;
}

/* Soma minutos ao relogio (soneca), tratando viradas de hora e dia. */
void TimeKeeper::addMinutes(int mins) {
  int total = hour * 60 + minute + mins;
  while (total >= 24 * 60) { total -= 24 * 60; dayIndex++; weekday = (weekday + 1) % 7; }
  while (total < 0)        { total += 24 * 60; }
  hour   = total / 60;
  minute = total % 60;
}
