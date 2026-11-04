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
  ACT_NIGHT_PEEK,       // toque no relogio noturno: mostra tudo por 10 s

  /* hub de ajustes */
  ACT_GO_ALARM_CFG,
  ACT_GO_NIGHT_CFG,
  ACT_GO_NETWORK,
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
  ACT_RAMP_CYCLE,
  ACT_CHTIME_CYCLE,
  ACT_SAVE,

  /* modo noturno */
  ACT_NIGHT_MODE_0, ACT_NIGHT_MODE_1, ACT_NIGHT_MODE_2,
  ACT_NIGHT_S_UP, ACT_NIGHT_S_DN,
  ACT_NIGHT_E_UP, ACT_NIGHT_E_DN,

  /* rede e lampada */
  ACT_NET_PORTAL,
  ACT_NET_WIZ_FIND,
  ACT_NET_WIZ_TEST,

  /* ajuste da hora atual */
  ACT_T_H_UP, ACT_T_H_DN,
  ACT_T_M_UP, ACT_T_M_DN,
  ACT_T_S_UP, ACT_T_S_DN,
  ACT_T_CONFIRM,

  /* alarme tocando */
  ACT_SNOOZE,
  ACT_DISMISS_HOLD,     // area do DESLIGAR (o .ino mede o tempo segurado)
  ACT_EARLY_TAP,        // tocou antes do estimulo aparecer (reacao)

  ACT_OPT_BASE = 100   // ACT_OPT_BASE + i = alternativa i do desafio
};

/* Botao DESLIGAR da tela de alarme tocando (o .ino testa o dedo nele). */
#define DISMISS_X  20
#define DISMISS_Y  142
#define DISMISS_W  200
#define DISMISS_H  104

void uiBegin();                       // inicia a TFT e o PWM do backlight
void uiSetBacklight(uint8_t duty);    // 0-255

/* Relogio: tema noturno (so a hora, vermelho) ou normal. */
void uiSetNight(bool night);
bool uiIsNight();

void uiDrawScreen(Screen s);          // redesenho completo de uma tela
void uiClockTick();                   // atualiza hora/status na tela do relogio
void uiNetworkTick();                 // atualiza a tela REDE se algo mudou

/* Alarme tocando: estagio, tempo tocando e piscar do cabecalho. */
void uiRingingUpdate(int stage, unsigned long ringMs, int nextLevel, bool flash);
void uiRingHoldProgress(float p);     // 0..1 barra do "segure para desligar"

/* Desafio: tempo restante e total, para o contador e a barra. */
void uiChallengeUpdate(unsigned long remainMs, unsigned long totalMs);

int  uiHitTest(int x, int y);         // UiAction do botao sob o ponto
void uiFlashAction(int action);       // pisca o botao por 100 ms
void uiFlashUpdate();                 // restaura o botao apos o piscar

void uiToast(const char* msg, uint16_t color);
void uiDrawCalibrationPrompt();

#endif /* UI_H */
