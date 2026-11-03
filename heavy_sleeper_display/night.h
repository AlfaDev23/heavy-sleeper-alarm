/*
 * night.h — Decide se a tela deve estar no modo noturno.
 *
 *   NIGHT_FORCE_DAY ..... nunca (tema normal o tempo todo)
 *   NIGHT_FORCE_NIGHT ... sempre
 *   NIGHT_AUTO .......... pelo horario settings.nightStart -> nightEnd
 *                         (ou pelo LDR, se PIN_LDR estiver definido)
 */
#ifndef NIGHT_H
#define NIGHT_H

#include "config.h"

void nightBegin();
bool nightShouldBeActive();        // avalia agora (chame 1x por segundo)
bool nightInSchedule(int minuteOfDay);
const char* nightModeName(int mode);

#endif /* NIGHT_H */
