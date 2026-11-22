/*
 ═══════════════════════════════════════════════════════════════════
  DESPERTADOR HEAVY SLEEPERS — Actuator ESP32
  Hardware : ESP32 devkit 30 pinos (sem tela, sem touch)

  Responsabilidades desta placa:
    - receber comandos por UART2 (115200) vindos do Display ESP32
    - acionar a vibracao por PWM no GPIO33 (ON / OFF / PULSE)
    - (reservado) luz por PWM no GPIO32 com rampa de 30 s — so com
      USE_MOSFET_LIGHT; desde a v0.4 a luz e a lampada WiZ do Display
    - responder ACK/NACK e STATUS, e vigiar o link com um watchdog

  Os pinos 32 (reservado) e 33 vao para os GATES dos MOSFETs atraves de um resistor
  de 100-470 R. Detalhes em WIRING.md.
 ═══════════════════════════════════════════════════════════════════
*/

#include "config.h"
#include "actuator.h"
#include "comm.h"

static unsigned long lastBlinkMs = 0;
static bool          ledState    = false;

/*
 * LED de status:
 *   aceso fixo      -> comando recebido nos ultimos 2 s
 *   pisca devagar   -> link ocioso, mas saudavel
 *   pisca rapido    -> Display nao respondeu ao PING?
 */
static void updateStatusLed() {
  unsigned long now = millis();
  unsigned long since = now - commLastRxMs();

  if (commLinkLost()) {
    if (now - lastBlinkMs >= 100) {          // rapido
      lastBlinkMs = now;
      ledState = !ledState;
      digitalWrite(LED_STATUS, ledState);
    }
  } else if (since < 2000) {
    digitalWrite(LED_STATUS, HIGH);          // atividade recente
    ledState = true;
  } else if (now - lastBlinkMs >= 1000) {    // devagar
    lastBlinkMs = now;
    ledState = !ledState;
    digitalWrite(LED_STATUS, ledState);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[BOOT] HeavySleep Actuator %s\n", FW_VERSION);

  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, LOW);

  actuatorBegin();
  commBegin();

  Serial.println(F("[BOOT] aguardando comandos do Display"));
}

void loop() {
  commPoll();        // le e executa comandos
  commWatchdog();    // PING? apos ociosidade
  updateStatusLed();
}
