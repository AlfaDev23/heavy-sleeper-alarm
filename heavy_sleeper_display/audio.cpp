/*
 * audio.cpp — Onda quadrada por software no DAC do ESP32.
 *
 * CIRCUITO (ver tambem WIRING.md):
 *   O board E32R28T traz DAC em IO26 (PIN_DAC_AUDIO) ligado a um
 *   amplificador que alimenta o conector SPEAKER. O amplificador tem um
 *   pino de habilitacao em IO4 (PIN_AMP_ENABLE): LOW liga, HIGH muta.
 *   Sem acionar esse pino nao sai som nenhum pelo alto-falante do board.
 *
 *   ATENCAO: IO25 NAO e o DAC de audio deste board — e o CLK do touch.
 *
 * A task roda no Core 0 para nao travar a UI, que vive no Core 1.
 * playTone() bloqueia (delayMicroseconds) mas cede a CPU a cada ~100 ms,
 * o que mantem a IDLE0 alimentada e evita o task watchdog.
 *
 * O enable/disable do amplificador e feito DENTRO da task de audio, no
 * mesmo ponto em que o nivel muda: assim o amp so fica ligado enquanto ha
 * som de verdade e nao ha corrida entre a UI e a geracao dos tons.
 */
#include "audio.h"

static volatile int  gLevel   = 0;      // 0 = mudo; 1..4 = intensidade
static TaskHandle_t  gTaskH   = NULL;
static bool          gAmpOn   = false;  // espelho do estado de PIN_AMP_ENABLE

#define DAC_MID 128                      // meio da escala = silencio

/* Liga o amplificador do board (LOW = habilitado). */
void audioEnable() {
  if (gAmpOn) return;
  digitalWrite(PIN_AMP_ENABLE, LOW);
  gAmpOn = true;
}

/* Silencia o DAC e muta o amplificador (HIGH = desabilitado). */
void audioDisable() {
  dacWrite(PIN_DAC_AUDIO, DAC_MID);   // silencio antes de cortar o amp
  if (!gAmpOn) return;
  delay(10);                          // deixa o DAC assentar, evita "poc"
  digitalWrite(PIN_AMP_ENABLE, HIGH);
  gAmpOn = false;
}

/* Amplitude (pico a pico) por nivel — funciona como "volume". */
static const uint8_t LEVEL_AMP[5] = { 0, 60, 110, 180, 230 };

/* True se o nivel mudou desde o inicio da nota (para abortar rapido). */
static inline bool levelChanged(int snapshot) {
  return gLevel != snapshot;
}

/* Pausa cedendo CPU, abortando se o nivel do alarme mudar. */
static void restMs(int ms, int snapshot) {
  unsigned long end = millis() + ms;
  while ((long)(millis() - end) < 0) {
    if (levelChanged(snapshot)) return;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

/*
 * Gera uma onda quadrada na frequencia pedida pela duracao pedida.
 * Divide em blocos de ~100 ms com vTaskDelay(1) entre eles: sem isso a
 * task nunca cederia a CPU e o watchdog do Core 0 reiniciaria o ESP32.
 */
static void toneWithAmp(int freq, int durationMs, uint8_t amp, int snapshot) {
  if (freq <= 0 || durationMs <= 0) { restMs(durationMs, snapshot); return; }

  const int  halfPeriod = 500000 / freq;          // meio periodo em us
  const uint8_t hi = DAC_MID + amp / 2;
  const uint8_t lo = DAC_MID - amp / 2;

  long cycles        = (long)freq * durationMs / 1000;  // total de ciclos
  long cyclesPerStep = freq / 10;                       // ~100 ms por bloco
  if (cyclesPerStep < 1) cyclesPerStep = 1;

  long done = 0;
  while (done < cycles) {
    long n = cycles - done;
    if (n > cyclesPerStep) n = cyclesPerStep;

    for (long i = 0; i < n; i++) {
      dacWrite(PIN_DAC_AUDIO, hi);
      delayMicroseconds(halfPeriod);
      dacWrite(PIN_DAC_AUDIO, lo);
      delayMicroseconds(halfPeriod);
    }
    done += n;

    if (levelChanged(snapshot)) break;
    vTaskDelay(1);   // cede a CPU (~1 ms) — alimenta a IDLE0
  }
  dacWrite(PIN_DAC_AUDIO, DAC_MID);
}

/* Versao publica com amplitude do nivel corrente. */
void playTone(int frequencyHz, int durationMs) {
  int snap = gLevel;
  uint8_t amp = LEVEL_AMP[constrain(snap, 0, 4)];
  if (amp == 0) amp = 110;   // se chamado fora do alarme, usa volume medio
  audioEnable();             // sem o amp habilitado nao sai som no speaker
  toneWithAmp(frequencyHz, durationMs, amp, snap);
}

/* Toca um ciclo da melodia correspondente ao nivel (1..4). */
void playAlarmMelody(int level) {
  const int snap = level;
  const uint8_t amp = LEVEL_AMP[constrain(level, 0, 4)];

  audioEnable();

  switch (level) {
    case 1: {   // suave: 440, 494, 523 — lento, com pausa
      static const int n1[] = { 440, 494, 523 };
      for (int i = 0; i < 3 && !levelChanged(snap); i++) {
        toneWithAmp(n1[i], 300, amp, snap);
        restMs(120, snap);
      }
      restMs(500, snap);
      break;
    }
    case 2: {   // medio: 880, 988, 1047 — mais rapido
      static const int n2[] = { 880, 988, 1047 };
      for (int i = 0; i < 3 && !levelChanged(snap); i++) {
        toneWithAmp(n2[i], 180, amp, snap);
        restMs(60, snap);
      }
      restMs(250, snap);
      break;
    }
    case 3: {   // forte: 1200, 1400, 1600 — curto, agudo, sem pausa
      static const int n3[] = { 1200, 1400, 1600 };
      for (int i = 0; i < 3 && !levelChanged(snap); i++) {
        toneWithAmp(n3[i], 120, amp, snap);
      }
      restMs(80, snap);
      break;
    }
    case 4: {   // extremo: sweep continuo 800 -> 2000 Hz
      for (int f = 800; f <= 2000 && !levelChanged(snap); f += 40) {
        toneWithAmp(f, 30, amp, snap);
      }
      break;
    }
    default:
      restMs(50, snap);
      break;
  }
}

/*
 * Corpo da task: toca a melodia do nivel atual em loop, ou dorme se mudo.
 * O amplificador e ligado/desligado aqui dentro — nivel 0 sempre acaba
 * mutando o amp, entao ele nunca fica ligado a toa consumindo corrente.
 */
static void audioTask(void* arg) {
  (void)arg;
  for (;;) {
    int level = gLevel;
    if (level <= 0) {
      audioDisable();                  // ja escreve DAC_MID antes de mutar
      vTaskDelay(pdMS_TO_TICKS(50));
    } else {
      playAlarmMelody(level);          // playAlarmMelody() chama audioEnable()
    }
  }
}

/*
 * Configura o DAC e o pino de habilitacao do amplificador e cria a task de
 * audio fixada no Core 0 (a UI fica no Core 1). O amp comeca MUDO.
 */
void audioBegin() {
  pinMode(PIN_AMP_ENABLE, OUTPUT);
  digitalWrite(PIN_AMP_ENABLE, HIGH);  // HIGH = amplificador mudo
  gAmpOn = false;
  dacWrite(PIN_DAC_AUDIO, DAC_MID);    // nivel medio = silencio DC
  gLevel = 0;
  if (gTaskH == NULL) {
    xTaskCreatePinnedToCore(audioTask, "audio", 2560, NULL, 1, &gTaskH, 0);
  }
}

/* Alias com o nome usado no documento de hardware. */
void audioInit() { audioBegin(); }

/* Define a intensidade do som (0 silencia). */
void audioSetLevel(int level) {
  gLevel = constrain(level, 0, 4);
}

/* Nivel de audio corrente. */
int audioGetLevel() {
  return gLevel;
}

/*
 * Silencia o alarme. Zera o DAC na hora e deixa o corte do amplificador
 * para a task: assim o digitalWrite() do PIN_AMP_ENABLE acontece sempre
 * no mesmo contexto que gera o som, sem corrida com a UI.
 */
void stopAudio() {
  gLevel = 0;
  dacWrite(PIN_DAC_AUDIO, DAC_MID);
}
