/*
 * config.h — Pinos, constantes e tipos globais do Display ESP32.
 * Despertador Heavy Sleepers.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FW_VERSION "0.4.0-alpha"

/* ═══════════════════════════════════════════════════════════════════
   MODO DEMO
   Comente a linha DEMO_MODE para uso real (relogio comeca em 00:00).
   ═══════════════════════════════════════════════════════════════════ */
#define DEMO_MODE

#ifdef DEMO_MODE
  #define DEMO_START_H 7    // relogio inicia em 07:29:00 ...
  #define DEMO_START_M 29
  #define DEMO_START_S 0
  #define DEMO_ALM_H   7    // ... e o alarme dispara em ~1 minuto
  #define DEMO_ALM_M   30
  // Escala o tempo de escalonamento para segundos, senao a demo
  // levaria 10 minutos para chegar ao nivel maximo. Comente para
  // usar os tempos reais (2/5/10 min) mesmo em modo demo.
  #define DEMO_FAST_ESCALATE
  // Em modo demo a hora NAO e sobrescrita pelo NTP, senao a demo perderia
  // o horario 07:29 assim que o Wi-Fi conectasse.
#endif

/* ═══════════════════════════════════════════════════════════════════
   PINOS — board E32R28T (LCDWIKI / QDtech), conforme datasheet oficial
   Nenhum numero de pino deve aparecer fora deste arquivo.
   ═══════════════════════════════════════════════════════════════════ */

/* ── LCD (fixo, controlado pela TFT_eSPI via User_Setup.h) ─────────
   TFT_CS=15  TFT_DC=2  TFT_SCLK=14  TFT_MOSI=13  TFT_MISO=12  TFT_BL=21
   NAO reutilize esses pinos aqui.                                     */

/* ── Touch XPT2046 — BARRAMENTO SPI PROPRIO, separado do da LCD ────
   Por isso o firmware usa a biblioteca XPT2046_Touchscreen e NAO o
   tft.getTouch() da TFT_eSPI (que assume SPI compartilhado).          */
#define PIN_TOUCH_CS    33
#define PIN_TOUCH_CLK   25
#define PIN_TOUCH_MOSI  32
#define PIN_TOUCH_MISO  39   // INPUT ONLY
#define PIN_TOUCH_IRQ   36   // INPUT ONLY — nao use como saida

/* ── Audio ────────────────────────────────────────────────────────── */
#define PIN_DAC_AUDIO   26   // DAC2 do ESP32 (NAO e o 25: aquele e o CLK do touch)
#define PIN_AMP_ENABLE   4   // LOW = amplificador ligado, HIGH = mudo

/* ── Comunicacao UART com o Actuator ESP32 ─────────────────────────
   ATENCAO: IO16 = LED Verde e IO22 = LED Vermelho do board. Os LEDs
   piscam junto com os dados da UART — comportamento aceito de proposito. */
#define PIN_UART2_TX    16
#define PIN_UART2_RX    22
#define UART2_BAUD      115200

/* ── LED RGB do board (common anode: LOW = acende) ─────────────────
   VERMELHO e VERDE estao ocupados pela UART2; so o AZUL esta livre.   */
#define PIN_LED_RED     22   // == PIN_UART2_RX — nao usar como LED
#define PIN_LED_GREEN   16   // == PIN_UART2_TX — nao usar como LED
#define PIN_LED_BLUE    17   // LIVRE: indicador de status do link UART

/* ── Luz por MOSFET no Actuator — RESERVADA ─────────────────────────
   Desde a v0.4 a luz do despertar e a lampada Wi-Fi WiZ. O canal de luz
   por MOSFET (GPIO32 do Actuator) ficou reservado para testes: descomente
   a linha abaixo AQUI e no config.h do Actuator para reativar o LIGHT:.  */
// #define USE_MOSFET_LIGHT

/* ── Sensor de luminosidade (futuro) ───────────────────────────────
   LDR em divisor de tensao no IO35 (ADC1: funciona com o Wi-Fi ligado;
   os pinos ADC2 nao funcionam com Wi-Fi). Descomente para que o modo
   noturno AUTO passe a seguir a luz do ambiente em vez do horario.     */
// #define PIN_LDR           35
#define LDR_DARK_ADC      600    // abaixo disso = ambiente escuro (0-4095)
#define LDR_HYST_ADC      150    // histerese para nao ficar alternando

/* ── Backlight (PWM via LEDC) ─────────────────────────────────────── */
#define PIN_BL        21   // backlight, PWM via LEDC

/* ── Canais LEDC (usados apenas no core 2.x; ver pwm.h) ──────────── */
#define LEDC_CH_BL 0

/* ═══════════════════════════════════════════════════════════════════
   TOUCH
   ═══════════════════════════════════════════════════════════════════ */
#define TOUCH_DEBOUNCE_MS  150
#define TOUCH_LONGPRESS_MS 1500
#define TOUCH_CAL_KEY      "touch_cal"

/* Faixa bruta tipica do XPT2046 (fallback quando nao ha calibracao). */
#define TOUCH_RAW_MIN      300
#define TOUCH_RAW_MAX      3800

/* ═══════════════════════════════════════════════════════════════════
   ALARME
   ═══════════════════════════════════════════════════════════════════ */
#define ALARM_SNOOZE_MIN     5
#define ALARM_ESCALATE_2MIN  2
#define ALARM_ESCALATE_5MIN  5
#define ALARM_ESCALATE_10MIN 10

/* Limiares de escalonamento em milissegundos desde o disparo. */
#ifdef DEMO_FAST_ESCALATE
  #define ESC_T1_MS  20000UL   // estagio 2
  #define ESC_T2_MS  45000UL   // estagio 3
  #define ESC_T3_MS  80000UL   // estagio 4 (log ALARM:ESCALATED)
  #define SNOOZE_MS  30000UL   // soneca curta para demonstracao
#else
  #define ESC_T1_MS  (ALARM_ESCALATE_2MIN  * 60000UL)
  #define ESC_T2_MS  (ALARM_ESCALATE_5MIN  * 60000UL)
  #define ESC_T3_MS  (ALARM_ESCALATE_10MIN * 60000UL)
  #define SNOOZE_MS  (ALARM_SNOOZE_MIN     * 60000UL)
#endif

/* Desligar o alarme: segurar o botao DESLIGAR por este tempo. Tem que ser
   MAIOR que TOUCH_LONGPRESS_MS, senao a soltura do dedo vira um toque. */
#define DISMISS_HOLD_MS  2000UL

/* Opcoes de tempo para resolver o desafio (s) e de duracao da rampa (min). */
#define CHAL_TIME_OPTS   { 30, 60, 90, 120 }
#define CHAL_TIME_COUNT  4
#define RAMP_MIN_OPTS    { 5, 10, 15, 20 }
#define RAMP_MIN_COUNT   4

/* Em demo a rampa precisa caber no minuto entre 07:29 e 07:30. */
#ifdef DEMO_FAST_ESCALATE
  #define DEMO_RAMP_S    45
#endif

/* ═══════════════════════════════════════════════════════════════════
   WI-FI, PORTAL E HORA PELA INTERNET
   ═══════════════════════════════════════════════════════════════════ */
#define NET_HOSTNAME       "despertador"   // http://despertador.local
#define AP_SSID_PREFIX     "Despertador-"  // + 4 digitos do MAC
#define AP_PASSWORD        "despertador"   // minimo 8 caracteres (WPA2)
#define NET_CONNECT_TIMEOUT_MS  20000UL    // sem conectar -> abre o portal
#define NET_RETRY_MS            60000UL    // nova tentativa com o portal aberto
#define AP_AUTOCLOSE_MS         60000UL    // fecha o portal 1 min apos conectar
#define AP_MANUAL_MS           600000UL    // portal aberto pela tela: 10 min
#define NTP_SERVER_1       "a.st1.ntp.br"
#define NTP_SERVER_2       "pool.ntp.org"
#define TZ_DEFAULT         "<-03>3"        // Brasilia, sem horario de verao
#define NTP_RESYNC_MS      30000UL         // copia a hora do sistema p/ o relogio

/* ═══════════════════════════════════════════════════════════════════
   LAMPADA WIZ (Philips / WiZ, protocolo UDP local na porta 38899)
   ═══════════════════════════════════════════════════════════════════ */
#define WIZ_PORT           38899
#define WIZ_LOCAL_PORT     38900
#define WIZ_DIM_MIN        10      // a WiZ nao aceita dimming abaixo de 10%
#define WIZ_DIM_MAX        100
/* Faixa de temperatura de cor. As lampadas COLORIDAS aceitam 2200-6500 K;
   as "branco ajustavel" aceitam 2700-6500 K. Se a sua for branco
   ajustavel e a rampa nao comecar, troque WIZ_TEMP_MIN para 2700.      */
#define WIZ_TEMP_MIN       2200
#define WIZ_TEMP_MAX       6500
#define WIZ_ACK_TIMEOUT_MS 700     // sem resposta -> reenvia
#define WIZ_RETRIES        3
#define WIZ_PING_MS        30000UL // getPilot periodico (lampada online?)
#define WIZ_ONLINE_MS      95000UL
#define WIZ_RAMP_STEP_MS   3000UL  // intervalo entre passos da rampa
#define WIZ_MAX_FOUND      4

/* ═══════════════════════════════════════════════════════════════════
   COMUNICACAO
   ═══════════════════════════════════════════════════════════════════ */
#define UART_BAUD            UART2_BAUD   // baud do link com o Actuator
#define UART_TIMEOUT_MS      5000
#define ACTUATOR_OFFLINE_MS  3000   // sem resposta por 3s -> OFFLINE
#define COMM_POLL_MS         1000   // intervalo entre STATUS?

/* ═══════════════════════════════════════════════════════════════════
   BACKLIGHT (duty 0-255)
   ═══════════════════════════════════════════════════════════════════ */
#define BL_STANDBY  77    // ~30%
#define BL_CONFIG   204   // ~80%
#define BL_ALARM    255   // 100%
#define BL_NIGHT    6     // ~2%: quarto escuro
#define NIGHT_PEEK_MS 10000UL   // toque no modo noturno mostra tudo por 10 s

/* ═══════════════════════════════════════════════════════════════════
   PALETA (RGB 565)
   ═══════════════════════════════════════════════════════════════════ */
#define C_BG      0x0000  // preto
#define C_HEADER  0x2945  // cinza escuro
#define C_ACCENT  0x07FF  // ciano
#define C_WARN    0xFD20  // laranja
#define C_DANGER  0xF800  // vermelho
#define C_OK      0x07E0  // verde
#define C_BTN     0x10A3  // azul-escuro (botao inativo)
#define C_BTN_SEL 0x02A6  // verde-escuro (botao selecionado)
#define C_TEXT    0xFFFF  // branco
#define C_SUBTEXT 0x8C51  // cinza medio
#define C_LINE    0x4208  // cinza dos separadores
#define C_PANEL   0x0008  // azul bem escuro (fundo de painel)

/* Modo noturno: vermelho apagado (nao ofusca e atrapalha menos o sono). */
#define C_NIGHT     0x7800  // vermelho medio
#define C_NIGHT_DIM 0x3000  // vermelho bem escuro

/* ═══════════════════════════════════════════════════════════════════
   TELA
   ═══════════════════════════════════════════════════════════════════ */
#define SCR_W 240
#define SCR_H 320

/* ═══════════════════════════════════════════════════════════════════
   ENUMS GLOBAIS
   ═══════════════════════════════════════════════════════════════════ */
enum Screen {
  SCREEN_CLOCK = 0,
  SCREEN_SETTINGS,
  SCREEN_SET_TIME,
  SCREEN_ALARM_CONFIG,
  SCREEN_NIGHT_CONFIG,     // modo noturno: AUTO / DIA / NOITE e horarios
  SCREEN_NETWORK,          // Wi-Fi, portal e lampada WiZ
  SCREEN_ALARM_RINGING,    // tocando: segure DESLIGAR ou soneca
  SCREEN_ALARM_ACTIVE      // desafio cronometrado (silencio)
};

enum NightMode { NIGHT_AUTO = 0, NIGHT_FORCE_DAY, NIGHT_FORCE_NIGHT };

enum DiffLevel { DIFF_EASY = 0, DIFF_MEDIUM, DIFF_HARD, DIFF_EXTREME };

enum ChalType { CHAL_MATH = 0, CHAL_MEMORY, CHAL_SEQUENCE, CHAL_REACTION };

enum VibMode { VIB_MODE_OFF = 0, VIB_MODE_WEAK, VIB_MODE_STRONG };

/* Janela de resposta do desafio de REACAO, por nivel (ms).
   Ajuste aqui se os testes indicarem que esta apertado demais. */
extern const uint16_t REACTION_WINDOW_MS[4];

#endif /* CONFIG_H */
