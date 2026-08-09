/*
 * actuator.h — Controle de luz (PWM com rampa) e vibracao (PWM pulsado).
 */
#ifndef ACTUATOR_H
#define ACTUATOR_H

#include "config.h"

void actuatorBegin();               // configura o PWM e cria as tasks

void setLight(int percent);         // alvo 0-100 (sobe em rampa de 30 s)
int  lightTarget();                 // alvo atual em %
int  lightCurrent();                // valor aplicado agora em %

void setVib(int mode);              // VIB_OFF | VIB_ON | VIB_PULSE
int  vibMode();
const char* vibName();              // "OFF" | "ON" | "PULSE"

#endif /* ACTUATOR_H */
