/*
 * alarms.cpp — Calculo de quando cada alarme toca, considerando os dias.
 */
#include "alarms.h"

Alarm alarmDraft    = { 7, 0, DAYS_WEEK, 1 };
int   alarmDraftIdx = -1;

bool alarmValid(const Alarm& a) {
  return a.hour < 24 && a.minute < 60 && a.days <= DAYS_ALL && a.enabled <= 1;
}

/* Sem dias marcados (uma vez) vale qualquer dia. */
static bool dayOk(const Alarm& a, int wday) {
  return a.days == DAYS_ONCE || (a.days & (1 << (wday % 7)));
}

bool alarmDueNow(const Alarm& a, int wday, int h, int m) {
  return a.enabled && a.hour == h && a.minute == m && dayOk(a, wday);
}

long alarmSecondsUntil(const Alarm& a, int wday, long sod) {
  if (!a.enabled) return -1;
  long almS = a.hour * 3600L + a.minute * 60L;
  for (int d = 0; d <= 7; d++) {
    long t = d * 86400L + almS - sod;
    if (t <= 0) continue;                         // hoje, mas ja passou
    if (dayOk(a, wday + d)) return t;
  }
  return -1;
}

int alarmNext(const Alarm* list, int n, int wday, long sod, long* secs) {
  int  best  = -1;
  long bestS = 0;
  for (int i = 0; i < n; i++) {
    long s = alarmSecondsUntil(list[i], wday, sod);
    if (s > 0 && (best < 0 || s < bestS)) { best = i; bestS = s; }
  }
  if (secs) *secs = bestS;
  return best;
}

int alarmWdayIn(int wday, long sod, long secs) {
  return (int)((wday + (sod + secs) / 86400L) % 7);
}

const char* wdayShort(int wday) {
  static const char* N[7] = { "DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB" };
  return N[((wday % 7) + 7) % 7];
}

const char* alarmDaysText(uint8_t days) {
  switch (days) {
    case DAYS_ALL:     return "TODO DIA";
    case DAYS_WEEK:    return "SEG-SEX";
    case DAYS_WEEKEND: return "SAB-DOM";
    case DAYS_ONCE:    return "UMA VEZ";
  }
  // Duas letras por dia, para caber na linha da lista: "SE QA SX".
  static const char* S[7] = { "DO", "SE", "TE", "QA", "QI", "SX", "SA" };
  static char buf[24];
  buf[0] = '\0';
  for (int d = 1; d <= 7; d++) {                  // segunda primeiro, domingo no fim
    int b = d % 7;
    if (!(days & (1 << b))) continue;
    if (buf[0]) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, S[b], sizeof(buf) - strlen(buf) - 1);
  }
  return buf;
}
