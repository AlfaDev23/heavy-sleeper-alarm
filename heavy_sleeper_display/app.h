/*
 * app.h — Ganchos do programa principal (heavy_sleeper_display.ino) usados
 *         por outros modulos, como o portal web.
 */
#ifndef APP_H
#define APP_H

/* True enquanto o alarme toca ou o desafio esta na tela. O portal recusa
   mudancas no alarme nesse periodo: desligar pelo celular seria um atalho
   para fugir do desafio. */
bool appAlarmBusy();

/* Alguma configuracao mudou por fora da tela (portal): redesenha. */
void appSettingsChanged();

/* True se a rampa de nascer do sol esta acendendo a lampada agora. */
bool appSunriseActive();

#endif /* APP_H */
