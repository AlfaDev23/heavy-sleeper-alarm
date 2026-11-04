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
  int  lightPct;     // brilho maximo da lampada WiZ, 10-100
  int  vibMode;      // VibMode 0-2
  int  snoozeCount;  // sonecas usadas hoje
  int  lastAlarmDay; // indice do dia do ultimo alarme (reset do snoozeCount)

  int  rampMin;      // minutos de "nascer do sol" antes do alarme (5-20)
  int  chalTimeS;    // segundos para resolver o desafio (30-120)

  int  nightMode;    // NightMode: AUTO / sempre dia / sempre noite
  int  nightStart;   // inicio da noite, em minutos do dia (ex. 22*60)
  int  nightEnd;     // fim da noite, em minutos do dia (ex. 6*60)
};

/* Configuracao de rede (so muda pelo portal). */
struct NetConfig {
  char ssid[33];     // rede Wi-Fi ("" = nunca configurado -> abre o portal)
  char pass[65];
  char wizIp[16];    // IP fixo da lampada ("" = procurar sozinho)
  char tz[40];       // fuso no formato POSIX, ex. "<-03>3"
};

extern Settings  settings;
extern NetConfig netcfg;

void storageBegin();                          // abre a NVS e carrega tudo
void storageSave();                           // grava as configuracoes atuais
void storageSaveNet();                        // grava a NetConfig
void storageResetDefaults();                  // volta aos padroes de fabrica
bool storageLoadTouchCal(uint16_t cal[5]);    // true se havia calibracao salva
void storageSaveTouchCal(const uint16_t cal[5]);
void storageClearTouchCal();                  // forca recalibracao no proximo boot

#endif /* STORAGE_H */
