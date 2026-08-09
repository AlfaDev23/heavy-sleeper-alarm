/*
 * storage.h — Persistencia das configuracoes na NVS (Preferences).
 */
#ifndef STORAGE_H
#define STORAGE_H

#include "config.h"

/* Todas as configuracoes do usuario que sobrevivem a um reboot. */
struct Settings {
  int  almHour;      // 0-23
  int  almMin;       // 0-59
  bool almEnabled;   // alarme armado?
  int  diffLevel;    // DiffLevel 0-3
  int  diffType;     // ChalType 0-3
  int  lightPct;     // 0-100
  int  vibMode;      // VibMode 0-2
  int  snoozeCount;  // sonecas usadas hoje
  int  lastAlarmDay; // indice do dia do ultimo alarme (reset do snoozeCount)
};

extern Settings settings;

void storageBegin();                          // abre a NVS e carrega tudo
void storageSave();                           // grava as configuracoes atuais
void storageResetDefaults();                  // volta aos padroes de fabrica
bool storageLoadTouchCal(uint16_t cal[5]);    // true se havia calibracao salva
void storageSaveTouchCal(const uint16_t cal[5]);
void storageClearTouchCal();                  // forca recalibracao no proximo boot

#endif /* STORAGE_H */
