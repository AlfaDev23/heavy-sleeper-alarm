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

/* Aviso de alarme que parou sozinho por tempo maximo ("" = nenhum).
   Some no proximo toque na tela. */
const char* appMissedAlarm();

/* Troca entre demo e producao sem recompilar (tela AJUSTES ou portal).
   Recusa (false) com o alarme tocando ou no desafio. */
bool appSetDemo(bool on);

/* Botao "Testar" do portal: vibra VIB_TEST_MS na intensidade dada (%).
   Recusa (false) com o alarme tocando ou no desafio. */
bool appVibTest(int percent);

/* Botao "Testar" do volume no portal: toca SOUND_TEST_MS a melodia do nivel
   (1 = inicio, 4 = fim da escalada) no volume pct. false com alarme ativo. */
bool appSoundTest(int melody, int pct);

#endif /* APP_H */
