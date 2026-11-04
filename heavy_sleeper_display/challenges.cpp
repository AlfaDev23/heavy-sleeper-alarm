/*
 * challenges.cpp — Os quatro tipos de desafio e sua logica de dificuldade.
 */
#include <esp_system.h>
#include "challenges.h"

static Challenge ch;
static int attempts = 0;

/* Janela de resposta do desafio de reacao, por nivel (declarada em config.h). */
const uint16_t REACTION_WINDOW_MS[4] = { 2000, 2000, 1800, 1600 };

/* Cores fixas usadas pelos desafios de memoria e reacao. */
static const uint16_t PAL[4]      = { 0xF800, 0x07E0, 0x001F, 0xFFE0 };
static const char*    PAL_NAME[4] = { "VERMELHO", "VERDE", "AZUL", "AMARELO" };

/* ─────────────────────────── utilidades ─────────────────────────── */

/* Nome legivel do nivel de dificuldade. */
const char* diffName(int level) {
  switch (level) {
    case DIFF_EASY:    return "FACIL";
    case DIFF_MEDIUM:  return "MEDIO";
    case DIFF_HARD:    return "DIFICIL";
    default:           return "EXTREMO";
  }
}

/* Nome legivel do tipo de desafio. */
const char* typeName(int type) {
  switch (type) {
    case CHAL_MATH:     return "MATEMATICA";
    case CHAL_MEMORY:   return "MEMORIA";
    case CHAL_SEQUENCE: return "SEQUENCIA";
    default:            return "REACAO";
  }
}

/* Inteiro aleatorio em [lo, hi]. */
static int rnd(int lo, int hi) {
  return (int)random(lo, hi + 1);
}

/* Embaralha um vetor de inteiros (Fisher-Yates). */
static void shuffleInts(int* v, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = rnd(0, i);
    int t = v[i]; v[i] = v[j]; v[j] = t;
  }
}

/* True se o valor ja existe no vetor. */
static bool contains(const int* v, int n, int val) {
  for (int i = 0; i < n; i++) if (v[i] == val) return true;
  return false;
}

/* Zera a struct antes de montar um desafio novo. */
static void resetChallenge(int type, int level) {
  memset(&ch, 0, sizeof(ch));
  ch.type        = type;
  ch.level       = constrain(level, 0, 3);
  ch.promptColor = C_ACCENT;
  ch.highlight   = -1;
  ch.showIdx     = -1;
  ch.phase       = CHP_INPUT;
  ch.phaseMs     = millis();
  ch.redraw      = true;
  ch.colored     = false;
  ch.hint[0]     = '\0';
}

/* Preenche os rotulos numericos a partir de optVal. */
static void labelsFromValues() {
  for (int i = 0; i < ch.optCount; i++) {
    snprintf(ch.optLabel[i], sizeof(ch.optLabel[i]), "%d", ch.optVal[i]);
    ch.optColor[i] = C_BTN;
  }
}

/* ────────────────────────── MATEMATICA ──────────────────────────── */

/* Monta uma conta com 4 alternativas, uma correta. */
static void makeMath() {
  int a, b, c, answer;
  char expr[32];

  switch (ch.level) {
    case DIFF_EASY:                       // 1 digito + 1 digito
      a = rnd(1, 9); b = rnd(1, 9);
      answer = a + b;
      snprintf(expr, sizeof(expr), "%d + %d = ?", a, b);
      break;

    case DIFF_MEDIUM:                     // 2 digitos +/- 1 digito
      a = rnd(11, 99); b = rnd(2, 9);
      if (random(2)) { answer = a + b; snprintf(expr, sizeof(expr), "%d + %d = ?", a, b); }
      else           { answer = a - b; snprintf(expr, sizeof(expr), "%d - %d = ?", a, b); }
      break;

    case DIFF_HARD:                       // multiplicacao
      a = rnd(6, 15); b = rnd(4, 9);
      answer = a * b;
      snprintf(expr, sizeof(expr), "%d x %d = ?", a, b);
      break;

    default:                              // operacao dupla
      a = rnd(6, 15); b = rnd(3, 8); c = rnd(4, 19);
      answer = a * b + c;
      snprintf(expr, sizeof(expr), "%d x %d + %d = ?", a, b, c);
      break;
  }

  snprintf(ch.prompt, sizeof(ch.prompt), "%s", expr);
  snprintf(ch.hint, sizeof(ch.hint), "Toque na resposta certa");

  // Alternativas: a correta + 3 distratores proximos e distintos.
  int vals[4];
  vals[0] = answer;
  int n = 1;
  int guard = 0;
  while (n < 4 && guard++ < 200) {
    int delta = rnd(1, (ch.level >= DIFF_HARD) ? 12 : 4);
    if (random(2)) delta = -delta;
    int cand = answer + delta;
    if (cand < 0 || contains(vals, n, cand)) continue;
    vals[n++] = cand;
  }
  while (n < 4) { vals[n] = answer + 100 + n; n++; }   // rede de seguranca

  shuffleInts(vals, 4);
  ch.optCount = 4;
  for (int i = 0; i < 4; i++) ch.optVal[i] = vals[i];
  labelsFromValues();

  ch.expectedLen = 1;
  for (int i = 0; i < 4; i++) if (ch.optVal[i] == answer) ch.expected[0] = i;

  ch.phase = CHP_INPUT;
}

/* ──────────────────────────── MEMORIA ───────────────────────────── */

/* Sorteia uma sequencia de cores para o usuario repetir. */
static void makeMemory() {
  static const int LEN[4]    = { 3, 5, 7, 9 };
  static const int ON_MS[4]  = { 650, 600, 550, 1500 };  // extremo: 1,5 s por cor

  ch.optCount = 4;
  ch.colored  = true;
  for (int i = 0; i < 4; i++) {
    ch.optColor[i]  = PAL[i];
    ch.optLabel[i][0] = '\0';
    ch.optVal[i]    = i;
  }

  ch.expectedLen = LEN[ch.level];
  for (int i = 0; i < ch.expectedLen; i++) ch.expected[i] = rnd(0, 3);

  snprintf(ch.prompt, sizeof(ch.prompt), "MEMORIZE %d CORES", ch.expectedLen);
  snprintf(ch.hint,   sizeof(ch.hint),   "Observe a sequencia...");

  ch.phase       = CHP_SHOW;
  ch.phaseMs     = millis();
  ch.windowMs    = ON_MS[ch.level];   // tempo aceso de cada cor
  ch.showIdx     = 0;
  ch.showVisible = true;
  ch.highlight   = ch.expected[0];
}

/* ─────────────────────────── SEQUENCIA ──────────────────────────── */

/* Sorteia numeros para o usuario tocar na ordem pedida. */
static void makeSequence() {
  bool descending = false;
  bool onlyEven   = false;
  int  count;

  switch (ch.level) {
    case DIFF_EASY:   count = 4; break;
    case DIFF_MEDIUM: count = 5; descending = random(2); break;
    case DIFF_HARD:   count = 6; break;
    default:          count = 7; onlyEven = true; break;
  }

  int vals[CH_MAX_OPT];
  int n = 0;

  if (onlyEven) {
    // 4 pares + 3 impares, para sempre haver pares suficientes.
    while (n < 4) { int v = rnd(1, 24) * 2;     if (!contains(vals, n, v)) vals[n++] = v; }
    while (n < 7) { int v = rnd(1, 24) * 2 - 1; if (!contains(vals, n, v)) vals[n++] = v; }
  } else {
    while (n < count) { int v = rnd(1, 99); if (!contains(vals, n, v)) vals[n++] = v; }
  }
  shuffleInts(vals, count);

  ch.optCount = count;
  for (int i = 0; i < count; i++) ch.optVal[i] = vals[i];
  labelsFromValues();

  // Monta a ordem esperada como indices dos botoes.
  int idx[CH_MAX_OPT];
  int m = 0;
  for (int i = 0; i < count; i++) {
    if (onlyEven && (ch.optVal[i] % 2 != 0)) continue;   // extremo: so pares
    idx[m++] = i;
  }
  // Ordenacao por selecao sobre os indices, comparando os valores.
  for (int i = 0; i < m - 1; i++) {
    int best = i;
    for (int j = i + 1; j < m; j++) {
      bool better = descending ? (ch.optVal[idx[j]] > ch.optVal[idx[best]])
                               : (ch.optVal[idx[j]] < ch.optVal[idx[best]]);
      if (better) best = j;
    }
    int t = idx[i]; idx[i] = idx[best]; idx[best] = t;
  }
  ch.expectedLen = m;
  for (int i = 0; i < m; i++) ch.expected[i] = idx[i];

  if (onlyEven) {
    snprintf(ch.prompt, sizeof(ch.prompt), "TOQUE SO OS PARES");
    snprintf(ch.hint,   sizeof(ch.hint),   "em ordem crescente (%d)", m);
  } else {
    snprintf(ch.prompt, sizeof(ch.prompt), "ORDEM %s",
             descending ? "DECRESCENTE" : "CRESCENTE");
    snprintf(ch.hint,   sizeof(ch.hint),   "Toque os %d na ordem", m);
  }

  ch.phase = CHP_INPUT;
}

/* ──────────────────────────── REACAO ────────────────────────────── */

/* Monta um estimulo de reacao; comeca aguardando (CHP_SHOW). */
static void makeReaction() {
  switch (ch.level) {
    case DIFF_EASY: {                     // toque na cor pedida
      int target = rnd(0, 3);
      ch.optCount = 4;
      ch.colored  = true;
      for (int i = 0; i < 4; i++) {
        ch.optColor[i]    = PAL[i];
        ch.optLabel[i][0] = '\0';
        ch.optVal[i]      = i;
      }
      snprintf(ch.prompt, sizeof(ch.prompt), "TOQUE EM %s", PAL_NAME[target]);
      snprintf(ch.hint,   sizeof(ch.hint),   "assim que os botoes aparecerem");
      ch.expectedLen = 1;
      ch.expected[0] = target;
      break;
    }

    case DIFF_MEDIUM: {                   // Stroop: palavra x cor da tinta
      int word = rnd(0, 3);
      int ink  = random(2) ? word : rnd(0, 3);
      snprintf(ch.prompt, sizeof(ch.prompt), "%s", PAL_NAME[word]);
      snprintf(ch.hint,   sizeof(ch.hint),   "A palavra combina com a cor?");
      ch.promptColor = PAL[ink];

      ch.optCount = 2;
      ch.colored  = false;
      snprintf(ch.optLabel[0], sizeof(ch.optLabel[0]), "SIM");
      snprintf(ch.optLabel[1], sizeof(ch.optLabel[1]), "NAO");
      ch.optColor[0] = C_BTN;
      ch.optColor[1] = C_BTN;
      ch.expectedLen = 1;
      ch.expected[0] = (ink == word) ? 0 : 1;
      break;
    }

    case DIFF_HARD: {                      // ache o simbolo diferente
      static const char* SYM[4] = { "#", "@", "%", "&" };
      int common = rnd(0, 3);
      int odd    = rnd(0, 3);
      while (odd == common) odd = rnd(0, 3);
      int oddPos = rnd(0, 3);

      ch.optCount = 4;
      ch.colored  = false;
      for (int i = 0; i < 4; i++) {
        snprintf(ch.optLabel[i], sizeof(ch.optLabel[i]), "%s",
                 (i == oddPos) ? SYM[odd] : SYM[common]);
        ch.optColor[i] = C_BTN;
      }
      snprintf(ch.prompt, sizeof(ch.prompt), "ACHE O DIFERENTE");
      snprintf(ch.hint,   sizeof(ch.hint),   "3 iguais, 1 diferente");
      ch.expectedLen = 1;
      ch.expected[0] = oddPos;
      break;
    }

    default: {                             // dois criterios: simbolo E cor
      static const char* SYM[4] = { "#", "@", "%", "&" };
      int common    = rnd(0, 3);
      int oddSym    = rnd(0, 3);  while (oddSym == common) oddSym = rnd(0, 3);
      int commonCol = rnd(0, 3);
      int oddCol    = rnd(0, 3);  while (oddCol == commonCol) oddCol = rnd(0, 3);
      int oddPos    = rnd(0, 3);

      ch.optCount = 4;
      ch.colored  = true;
      for (int i = 0; i < 4; i++) {
        bool isOdd = (i == oddPos);
        snprintf(ch.optLabel[i], sizeof(ch.optLabel[i]), "%s",
                 isOdd ? SYM[oddSym] : SYM[common]);
        ch.optColor[i] = isOdd ? PAL[oddCol] : PAL[commonCol];
      }
      snprintf(ch.prompt, sizeof(ch.prompt), "SIMBOLO E COR UNICOS");
      snprintf(ch.hint,   sizeof(ch.hint),   "so um difere nos dois criterios");
      ch.expectedLen = 1;
      ch.expected[0] = oddPos;
      break;
    }
  }

  // Espera aleatoria antes de liberar os botoes: e isso que mede reacao.
  ch.phase    = CHP_SHOW;
  ch.phaseMs  = millis();
  ch.windowMs = rnd(1200, 3000);
}

/* ────────────────────────── API publica ─────────────────────────── */

/* Semeia o gerador aleatorio com entropia real do hardware. */
void challengeInit() {
  randomSeed(esp_random());
  attempts = 0;
}

/* Sorteia e arma um desafio do tipo e nivel pedidos. */
void challengeStart(int type, int level) {
  resetChallenge(type, level);
  attempts++;

  switch (type) {
    case CHAL_MATH:     makeMath();     break;
    case CHAL_MEMORY:   makeMemory();   break;
    case CHAL_SEQUENCE: makeSequence(); break;
    default:            makeReaction(); break;
  }
  ch.redraw = true;

  Serial.printf("[CHAL] %s nivel %s (tentativa %d)\n",
                typeName(ch.type), diffName(ch.level), attempts);
}

/* Avanca as fases temporizadas (exibicao da memoria, janela da reacao). */
void challengeUpdate() {
  unsigned long now = millis();

  if (ch.type == CHAL_MEMORY && ch.phase == CHP_SHOW) {
    const unsigned long onMs  = ch.windowMs;
    const unsigned long offMs = 220;

    if (ch.showVisible && (now - ch.phaseMs >= onMs)) {
      ch.showVisible = false;
      ch.highlight   = -1;
      ch.phaseMs     = now;
      ch.redraw      = true;
    } else if (!ch.showVisible && (now - ch.phaseMs >= offMs)) {
      ch.showIdx++;
      if (ch.showIdx >= ch.expectedLen) {        // acabou de mostrar tudo
        ch.phase     = CHP_INPUT;
        ch.phaseMs   = now;
        ch.highlight = -1;
        snprintf(ch.prompt, sizeof(ch.prompt), "REPITA A SEQUENCIA");
        snprintf(ch.hint,   sizeof(ch.hint),   "%d cores na mesma ordem", ch.expectedLen);
      } else {
        ch.showVisible = true;
        ch.highlight   = ch.expected[ch.showIdx];
        ch.phaseMs     = now;
      }
      ch.redraw = true;
    }
    return;
  }

  if (ch.type == CHAL_REACTION) {
    if (ch.phase == CHP_SHOW && (now - ch.phaseMs >= ch.windowMs)) {
      ch.phase   = CHP_INPUT;                    // estimulo liberado
      ch.phaseMs = now;
      ch.redraw  = true;
    } else if (ch.phase == CHP_INPUT &&
               (now - ch.phaseMs >= REACTION_WINDOW_MS[ch.level])) {
      ch.phase   = CHP_WRONG;                    // estourou a janela
      ch.phaseMs = now;
      ch.redraw  = true;
      Serial.println(F("[CHAL] tempo esgotado"));
    }
  }
}

/* Toque durante a espera da REACAO: queimou a largada, conta como erro. */
void challengeEarlyTap() {
  if (ch.type != CHAL_REACTION || ch.phase != CHP_SHOW) return;
  ch.phase   = CHP_WRONG;
  ch.phaseMs = millis();
  ch.redraw  = true;
  Serial.println(F("[CHAL] queimou a largada"));
}

/* Acabou o tempo para resolver (definido pelo .ino): conta como erro. */
void challengeTimeout() {
  if (ch.phase == CHP_RIGHT || ch.phase == CHP_WRONG) return;
  ch.phase    = CHP_WRONG;
  ch.timedOut = true;
  ch.phaseMs  = millis();
  ch.redraw   = true;
  Serial.println(F("[CHAL] tempo do desafio esgotado"));
}

/* Registra o toque no botao idx e avalia acerto/erro. */
void challengeTouchIndex(int idx) {
  if (idx < 0 || idx >= ch.optCount) return;
  if (ch.phase != CHP_INPUT) return;   // ignora toques durante a exibicao

  if (idx != ch.expected[ch.entered]) {          // errou
    ch.phase   = CHP_WRONG;
    ch.phaseMs = millis();
    ch.redraw  = true;
    return;
  }

  ch.enteredIdx[ch.entered] = idx;
  ch.entered++;
  ch.redraw = true;

  if (ch.entered >= ch.expectedLen) {            // completou tudo
    ch.phase   = CHP_RIGHT;
    ch.phaseMs = millis();
  }
}

/* Estado atual do desafio (somente leitura para o ui). */
const Challenge& challengeCurrent() { return ch; }

/* Baixa a flag de redesenho depois que o ui atualizou a tela. */
void challengeClearRedraw() { ch.redraw = false; }

/* Quantas tentativas nesta sessao de alarme. */
int challengeAttempts() { return attempts; }

/* Zera o contador ao iniciar um novo alarme. */
void challengeResetAttempts() { attempts = 0; }
