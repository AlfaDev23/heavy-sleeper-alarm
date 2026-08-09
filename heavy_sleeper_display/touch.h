/*
 * touch.h — Touch resistivo XPT2046 pela biblioteca XPT2046_Touchscreen.
 *
 * O touch do board E32R28T tem BARRAMENTO SPI PROPRIO (CLK=25, MOSI=32,
 * MISO=39, CS=33, IRQ=36), separado do SPI da LCD. Por isso NAO se usa o
 * tft.getTouch() da TFT_eSPI: ela assume SPI compartilhado com a tela.
 *
 * Dependencia: "XPT2046_Touchscreen" de Paul Stoffregen (Library Manager).
 */
#ifndef TOUCH_H
#define TOUCH_H

#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include "config.h"

/* Instancia global do controlador (pinos vem do config.h). */
extern XPT2046_Touchscreen touch;

/* Retangulo clicavel com rotulo. */
struct Button {
  int x, y, w, h;
  const char* label;
};

void touchBegin();                        // carrega calibracao ou calibra
void touchInit();                         // alias de touchBegin()
void touchUpdate();                       // atualiza a maquina de eventos (loop)

/* Calibracao de 3 pontos. force=false apenas carrega o que estiver salvo na
   NVS e devolve false se nao houver nada; force=true roda a rotina na tela. */
bool touchCalibrate(bool force = false);

bool touchAvailable();                    // false se o touch nao inicializou
bool getTouchPoint(int &x, int &y);       // true + coordenadas ja em pixels
bool touchTap(int &x, int &y);            // evento: toque curto (consome)
bool touchLongPress(int &x, int &y);      // evento: toque de 1,5 s (consome)
void touchFlush();                        // descarta eventos pendentes

bool isTouched(const Button& btn, int tx, int ty);           // ponto no botao?
bool isTouched(int bx, int by, int bw, int bh, int tx, int ty);

#endif /* TOUCH_H */
