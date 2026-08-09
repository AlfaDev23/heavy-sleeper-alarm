/*
 * comm.h — Protocolo UART com o Actuator ESP32 (Serial2, 115200).
 */
#ifndef COMM_H
#define COMM_H

#include "config.h"

void commBegin();                 // Serial2 em TX=IO16 / RX=IO22 + LED de status
void commPoll();                  // le respostas e atualiza o LED (chame no loop)

void commSendLight(int percent);  // LIGHT:0-100
void commSendVib(const char* mode); // VIB:ON | VIB:OFF | VIB:PULSE
void commRequestStatus();         // STATUS?
void commSendAlarm(const char* what); // ALARM:START | STOP | ESCALATED

bool        commActuatorOnline(); // false se nao responde ha ACTUATOR_OFFLINE_MS
const char* commLastStatus();     // ultima linha STATUS:... recebida

#endif /* COMM_H */
