/*
 * ui.h — Todo o desenho na tela. Nenhum outro modulo fala com a TFT_eSPI.
 */
#ifndef UI_H
#define UI_H

#include <TFT_eSPI.h>
#include "config.h"

extern TFT_eSPI tft;

/* Acoes que um botao pode disparar. O .ino traduz isso em navegacao. */
enum UiAction {
  ACT_NONE = 0,

  /* tela do relogio */
  ACT_OPEN_ALARM_CFG,
  ACT_OPEN_SET_TIME,

  /* hub de ajustes */
  ACT_GO_ALARM_CFG,
  ACT_GO_SET_TIME,
  ACT_TEST_ALARM,
  ACT_BACK,

  /* configuracao do alarme */
  ACT_ALM_H_UP, ACT_ALM_H_DN,
  ACT_ALM_M_UP, ACT_ALM_M_DN,
  ACT_ALM_TOGGLE,
  ACT_DIFF_0, ACT_DIFF_1, ACT_DIFF_2, ACT_DIFF_3,
  ACT_TYPE_0, ACT_TYPE_1, ACT_TYPE_2, ACT_TYPE_3,
  ACT_LIGHT_UP, ACT_LIGHT_DN,
  ACT_VIB_CYCLE,
  ACT_SAVE,

  /* ajuste da hora atual */
  ACT_T_H_UP, ACT_T_H_DN,
  ACT_T_M_UP, ACT_T_M_DN,
  ACT_T_S_UP, ACT_T_S_DN,
  ACT_T_CONFIRM,

  /* alarme tocando */
  ACT_SNOOZE,
  ACT_EARLY_TAP,        // tocou antes do estimulo aparecer (reacao)

  ACT_OPT_BASE = 100   // ACT_OPT_BASE + i = alternativa i do desafio
};

void uiBegin();                       // inicia a TFT e o PWM do backlight
void uiSetBacklight(uint8_t duty);    // 0-255

void uiDrawScreen(Screen s);          // redesenho completo de uma tela
void uiClockTick();                   // atualiza so hora/segundos/status
void uiClockAlarmBar(bool ringing, bool flash);
void uiAlarmActiveUpdate(int stage, unsigned long elapsedMs, bool flash);

int  uiHitTest(int x, int y);         // UiAction do botao sob o ponto
void uiFlashAction(int action);       // pisca o botao por 100 ms
void uiFlashUpdate();                 // restaura o botao apos o piscar

void uiToast(const char* msg, uint16_t color);
void uiDrawCalibrationPrompt();

#endif /* UI_H */
