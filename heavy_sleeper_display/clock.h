/*
 * clock.h — Base de tempo por software (millis), sem RTC externo.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include "config.h"

struct TimeKeeper {
  int hour, minute, second;
  unsigned long lastTickMs;
  int  dayIndex;      // incrementa a cada virada de meia-noite

  void begin(int h, int m, int s);          // inicializa hora e cronometro
  bool tick();                              // true se 1 segundo passou (chame no loop)
  void setTime(int h, int m, int s);        // ajusta a hora atual
  bool isAlarmTime(int almH, int almM) const; // true se hora/min batem com o alarme
  void addMinutes(int mins);                // soma minutos (usado pela soneca)
};

extern TimeKeeper rtc;

#endif /* CLOCK_H */
