/*
 * comm.cpp — Recebe comandos do Display, executa e responde com ACK/NACK.
 */
#include "comm.h"
#include "actuator.h"

static char          rxBuf[64];
static uint8_t       rxLen       = 0;
static unsigned long lastRxMs    = 0;
static unsigned long pingSentMs  = 0;
static bool          pingPending = false;
static bool          linkLost    = false;

/* Inicia a Serial2 nos pinos definidos e anuncia que esta pronto. */
void commBegin() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, PIN_UART2_RX, PIN_UART2_TX);
  rxLen    = 0;
  lastRxMs = millis();
  Serial2.print(F("READY\n"));
  Serial.println(F("[COMM] READY enviado"));
}

/* Envia o estado atual da luz e da vibracao. */
static void sendStatus() {
  Serial2.printf("STATUS:LIGHT:%d,VIB:%s\n", lightTarget(), vibName());
}

/* Executa um comando do protocolo e responde. */
void parseCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.printf("[COMM] rx: %s\n", cmd.c_str());

  if (cmd.startsWith("LIGHT:")) {
#ifdef USE_MOSFET_LIGHT
    int pct = cmd.substring(6).toInt();
    pct = constrain(pct, 0, 100);
    setLight(pct);
    Serial2.printf("ACK:LIGHT:%d\n", pct);
#else
    Serial2.print(F("NACK:LIGHT_DISABLED\n"));   // luz agora e a lampada WiZ
#endif

  } else if (cmd == "VIB:ON") {
    setVib(VIB_ON);
    Serial2.print(F("ACK:VIB:ON\n"));

  } else if (cmd == "VIB:OFF") {
    setVib(VIB_OFF);
    Serial2.print(F("ACK:VIB:OFF\n"));

  } else if (cmd == "VIB:PULSE") {
    setVib(VIB_PULSE);
    Serial2.print(F("ACK:VIB:PULSE\n"));

  } else if (cmd == "STATUS?") {
    sendStatus();

  } else if (cmd.startsWith("ALARM:")) {
    // Somente log — quem decide os estimulos e o Display.
    Serial.printf("[COMM] evento de alarme: %s\n", cmd.c_str() + 6);
    Serial2.printf("ACK:%s\n", cmd.c_str());

  } else if (cmd == "PONG" || cmd == "PING?") {
    // Sinal de vida do Display; se ele perguntou, respondemos.
    if (cmd == "PING?") Serial2.print(F("PONG\n"));

  } else {
    Serial2.print(F("NACK:INVALID\n"));
    Serial.println(F("[COMM] comando invalido"));
  }
}

/* Le bytes pendentes montando linhas terminadas em \n. */
void commPoll() {
  while (Serial2.available()) {
    char c = (char)Serial2.read();

    lastRxMs    = millis();
    pingPending = false;      // qualquer trafego ja prova que o link vive
    linkLost    = false;

    if (c == '\n' || c == '\r') {
      if (rxLen > 0) {
        rxBuf[rxLen] = '\0';
        parseCommand(String(rxBuf));
        rxLen = 0;
      }
    } else if (rxLen < sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    } else {
      rxLen = 0;              // linha longa demais -> descarta
    }
  }
}

/* Depois de 60 s sem comandos manda PING?; sem resposta em 5 s, marca queda. */
void commWatchdog() {
  unsigned long now = millis();

  if (!pingPending && (now - lastRxMs >= WATCHDOG_IDLE_MS)) {
    Serial2.print(F("PING?\n"));
    pingSentMs  = now;
    pingPending = true;
    Serial.println(F("[COMM] PING? enviado"));
  }

  if (pingPending && (now - pingSentMs >= WATCHDOG_REPLY_MS)) {
    pingPending = false;
    linkLost    = true;
    lastRxMs    = now;        // evita floodar de PING?
    Serial.println(F("[COMM] Display nao respondeu — link caido"));
  }
}

/* True se o Display parou de responder. */
bool commLinkLost() { return linkLost; }

/* Momento do ultimo byte recebido. */
unsigned long commLastRxMs() { return lastRxMs; }
