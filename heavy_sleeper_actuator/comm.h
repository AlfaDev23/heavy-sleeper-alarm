/*
 * comm.h — Parser do protocolo UART vindo do Display ESP32.
 */
#ifndef COMM_H
#define COMM_H

#include "config.h"

void commBegin();                 // inicia a Serial2 e anuncia READY
void commPoll();                  // monta e despacha linhas (chame no loop)
void parseCommand(String cmd);    // executa um comando e responde
void commWatchdog();              // PING? apos ociosidade; detecta link caido

bool commLinkLost();              // true se o Display parou de responder
unsigned long commLastRxMs();     // millis do ultimo dado recebido

#endif /* COMM_H */
