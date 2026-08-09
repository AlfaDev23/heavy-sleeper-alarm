/*
 * comm.cpp — Envio de comandos e leitura nao-bloqueante das respostas
 *            do Actuator ESP32.
 *
 * PINOS: TX = PIN_UART2_TX (IO16), RX = PIN_UART2_RX (IO22).
 *   Esses dois pinos sao, no board E32R28T, os LEDs Verde e Vermelho do
 *   LED RGB. Eles vao piscar junto com o trafego da UART — comportamento
 *   aceito de proposito. Consequencia: este arquivo NAO pode acionar IO16
 *   nem IO22 como LED. So o LED Azul (IO17) sobra para status.
 */
#include "comm.h"

/* LED de status do link (common anode: LOW acende). */
#define LED_COMM_STATUS PIN_LED_BLUE
#define LED_ON          LOW
#define LED_OFF         HIGH

#define LED_RX_BLINK_MS   60    // piscada unica ao receber uma linha
#define LED_OFFLINE_MS   150    // meio periodo do piscar rapido de offline

static char          rxBuf[64];
static uint8_t       rxLen        = 0;
static unsigned long lastRxMs     = 0;
static bool          everSeen     = false;   // ja recebeu READY/algo alguma vez?
static char          lastStatus[48] = "STATUS: --";
static int           lastLightSent  = -1;    // evita reenviar o mesmo LIGHT
static unsigned long ledBlinkUntil  = 0;     // fim da piscada de recepcao

/* Inicia a Serial2 nos pinos definidos em config.h e apaga o LED de status. */
void commBegin() {
  Serial2.begin(UART2_BAUD, SERIAL_8N1, PIN_UART2_RX, PIN_UART2_TX);

  pinMode(LED_COMM_STATUS, OUTPUT);
  digitalWrite(LED_COMM_STATUS, LED_OFF);

  rxLen         = 0;
  everSeen      = false;
  ledBlinkUntil = 0;
  lastRxMs      = millis();
}

/*
 * LED azul: apagado com o link em ordem, piscada curta a cada linha
 * recebida e piscar rapido continuo enquanto o Actuator estiver mudo.
 */
static void ledUpdate() {
  if (!commActuatorOnline()) {
    bool on = ((millis() / LED_OFFLINE_MS) % 2) == 0;
    digitalWrite(LED_COMM_STATUS, on ? LED_ON : LED_OFF);
    ledBlinkUntil = 0;
    return;
  }

  if (ledBlinkUntil && (long)(millis() - ledBlinkUntil) < 0) {
    digitalWrite(LED_COMM_STATUS, LED_ON);
  } else {
    ledBlinkUntil = 0;
    digitalWrite(LED_COMM_STATUS, LED_OFF);
  }
}

/* Processa uma linha completa vinda do Actuator. */
static void handleLine(const char* line) {
  lastRxMs      = millis();
  everSeen      = true;
  ledBlinkUntil = millis() + LED_RX_BLINK_MS;   // pisca o azul 1x

  if (strncmp(line, "STATUS:", 7) == 0) {
    strncpy(lastStatus, line, sizeof(lastStatus) - 1);
    lastStatus[sizeof(lastStatus) - 1] = '\0';
  } else if (strcmp(line, "READY") == 0) {
    Serial.println(F("[COMM] Actuator pronto"));
    lastLightSent = -1;            // forca reenvio do estado apos reboot dele
  } else if (strcmp(line, "PING?") == 0) {
    Serial2.print(F("PONG\n"));    // responde ao watchdog do Actuator
  }
  Serial.printf("[COMM] rx: %s\n", line);
}

/* Le bytes pendentes da Serial2 montando linhas terminadas em \n. */
void commPoll() {
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n' || c == '\r') {
      if (rxLen > 0) {
        rxBuf[rxLen] = '\0';
        handleLine(rxBuf);
        rxLen = 0;
      }
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    } else {
      rxLen = 0;   // linha longa demais -> descarta
    }
  }
  ledUpdate();   // o loop ja chama commPoll() toda volta
}

/* Envia a intensidade da luz (0-100), ignorando repeticoes. */
void commSendLight(int percent) {
  percent = constrain(percent, 0, 100);
  if (percent == lastLightSent) return;
  lastLightSent = percent;
  Serial2.printf("LIGHT:%d\n", percent);
  Serial.printf("[COMM] tx: LIGHT:%d\n", percent);
}

/* Envia um comando de vibracao: "ON", "OFF" ou "PULSE". */
void commSendVib(const char* mode) {
  Serial2.printf("VIB:%s\n", mode);
  Serial.printf("[COMM] tx: VIB:%s\n", mode);
}

/* Pede o estado atual do Actuator. */
void commRequestStatus() {
  Serial2.print(F("STATUS?\n"));
}

/* Sinaliza eventos do alarme para log no Actuator. */
void commSendAlarm(const char* what) {
  Serial2.printf("ALARM:%s\n", what);
  Serial.printf("[COMM] tx: ALARM:%s\n", what);
}

/* True se o Actuator respondeu dentro da janela de timeout. */
bool commActuatorOnline() {
  return everSeen && (millis() - lastRxMs < ACTUATOR_OFFLINE_MS);
}

/* Ultima linha de status recebida (para exibir na tela). */
const char* commLastStatus() {
  return lastStatus;
}
