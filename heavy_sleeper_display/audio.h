/*
 * audio.h — Geracao de tons pelo DAC (PIN_DAC_AUDIO = IO26) em task
 *           FreeRTOS no Core 0, com controle do amplificador do board.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include "config.h"

void audioBegin();                          // cria a task de audio no Core 0
void audioInit();                           // alias de audioBegin()
void audioSetLevel(int level);              // 0=mudo, 1=suave ... 4=agressivo
int  audioGetLevel();                       // nivel atual
void stopAudio();                           // silencia imediatamente

/* Amplificador do board (PIN_AMP_ENABLE, LOW = ligado). Chamadas feitas
   DENTRO da task de audio, junto com a geracao dos tons. */
void audioEnable();                         // liga o amplificador
void audioDisable();                        // silencia o DAC e muta o amp

/* Chamadas de baixo nivel — executam DENTRO da task de audio. */
void playTone(int frequencyHz, int durationMs);
void playAlarmMelody(int level);            // toca um ciclo da melodia do nivel

#endif /* AUDIO_H */
