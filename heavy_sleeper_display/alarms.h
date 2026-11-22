/*
 * alarms.h — Lista de alarmes com dias da semana (logica pura, sem tela).
 *
 * Cada alarme tem hora, minuto, ligado/desligado e uma mascara de dias:
 *   bit 0 = domingo, bit 1 = segunda ... bit 6 = sabado
 *   days == 0  -> toca UMA VEZ (no proximo horario) e depois se desativa
 */
#ifndef ALARMS_H
#define ALARMS_H

#include "config.h"

#define MAX_ALARMS    6
#define DAYS_ALL      0x7F   // todos os dias
#define DAYS_WEEK     0x3E   // segunda a sexta
#define DAYS_WEEKEND  0x41   // sabado e domingo
#define DAYS_ONCE     0x00   // uma vez so

struct Alarm {
  uint8_t hour;      // 0-23
  uint8_t minute;    // 0-59
  uint8_t days;      // mascara de dias (ver acima)
  uint8_t enabled;   // 0/1
};

/* Campos dentro da faixa (para validar o que vem da NVS ou do portal). */
bool alarmValid(const Alarm& a);

/* True se o alarme deve tocar agora (dia da semana 0=dom, hora, minuto). */
bool alarmDueNow(const Alarm& a, int wday, int h, int m);

/* Segundos ate o proximo toque deste alarme (1 .. 7 dias), ou -1 se ele
   esta desligado. sod = segundos do dia agora. */
long alarmSecondsUntil(const Alarm& a, int wday, long sod);

/* Indice do proximo alarme a tocar (-1 = nenhum ligado). Em secs, quantos
   segundos faltam. */
int alarmNext(const Alarm* list, int n, int wday, long sod, long* secs);

/* Dia da semana em que cai um toque daqui a secs segundos. */
int alarmWdayIn(int wday, long sod, long secs);

/* "TODO DIA", "SEG-SEX", "SAB-DOM", "UMA VEZ" ou "SE QA SX". Buffer estatico. */
const char* alarmDaysText(uint8_t days);

/* "DOM".."SAB". */
const char* wdayShort(int wday);

/* Rascunho da tela de edicao: a tela desenha, o .ino altera. idx = -1 e um
   alarme novo (ainda fora da lista). */
extern Alarm alarmDraft;
extern int   alarmDraftIdx;

#endif /* ALARMS_H */
