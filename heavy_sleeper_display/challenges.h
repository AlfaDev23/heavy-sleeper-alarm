/*
 * challenges.h — Geracao e avaliacao dos desafios que desligam o alarme.
 *
 * Tudo aqui e maquina de estados: nenhuma fase usa delay(). O modulo so
 * mantem o ESTADO do desafio; quem desenha e o ui.cpp.
 */
#ifndef CHALLENGES_H
#define CHALLENGES_H

#include "config.h"

#define CH_MAX_OPT 9
#define CH_MAX_SEQ 9

enum ChalPhase {
  CHP_SHOW = 0,   // exibindo a sequencia (memoria) ou aguardando o estimulo (reacao)
  CHP_INPUT,      // aceitando toques
  CHP_RIGHT,      // acertou
  CHP_WRONG       // errou (ou estourou o tempo)
};

struct Challenge {
  int  type;                       // ChalType
  int  level;                      // nivel efetivo 0..3

  char prompt[42];                 // enunciado principal
  char hint[42];                   // linha de apoio
  uint16_t promptColor;            // cor do enunciado (usada no Stroop)

  char     optLabel[CH_MAX_OPT][6];
  uint16_t optColor[CH_MAX_OPT];
  int      optVal  [CH_MAX_OPT];
  int      optCount;
  bool     colored;                // botoes coloridos (memoria/reacao)

  int  expected[CH_MAX_SEQ];       // indices esperados, em ordem
  int  expectedLen;
  int  enteredIdx[CH_MAX_SEQ];
  int  entered;

  ChalPhase     phase;
  unsigned long phaseMs;           // quando a fase atual comecou
  unsigned long windowMs;          // duracao da fase SHOW / janela de resposta

  int  showIdx;                    // passo atual da exibicao (memoria)
  bool showVisible;                // pisca aceso/apagado
  int  highlight;                  // botao destacado agora (-1 = nenhum)

  bool redraw;                     // ui precisa redesenhar
  bool timedOut;                   // CHP_WRONG porque acabou o tempo do desafio
};

void challengeInit();                        // semeia o gerador aleatorio
void challengeStart(int type, int level);    // sorteia e arma um novo desafio
void challengeUpdate();                      // avanca as fases (chame no loop)
void challengeTouchIndex(int idx);           // registra toque no botao idx
void challengeEarlyTap();                    // toque antes do estimulo (reacao)
void challengeTimeout();                     // acabou o tempo total -> CHP_WRONG

const Challenge& challengeCurrent();
void challengeClearRedraw();                 // ui avisa que ja redesenhou
int  challengeAttempts();                    // tentativas nesta sessao de alarme
void challengeResetAttempts();

const char* diffName(int level);             // "FACIL".."EXTREMO"
const char* typeName(int type);              // "MATEMATICA".."REACAO"

#endif /* CHALLENGES_H */
