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

  /* Copia a hora vinda do NTP. Se a hora "voltou" mais de 12 h, entendemos
     que a meia-noite passou no meio e avancamos o dayIndex. */
  void syncTo(int h, int m, int s);
  long secondsOfDay() const { return hour * 3600L + minute * 60L + second; }
  int  minuteOfDay()  const { return hour * 60 + minute; }
  bool synced;                              // true depois do 1o acerto por NTP
};

extern TimeKeeper rtc;

#endif /* CLOCK_H */
