/*
 * actuator.cpp — Luz e vibracao em tasks FreeRTOS separadas.
 *
 * As duas tasks podem usar vTaskDelay livremente: elas nao seguram o loop
 * principal, que continua atendendo a UART.
 */
#include "actuator.h"
#include "pwm.h"

/* ── Luz ────────────────────────────────────────────────────────── */
static volatile int  gLightTarget  = 0;   // alvo em %
static volatile int  gLightCurrent = 0;   // aplicado em %
static volatile int  gRampFrom     = 0;   // % no inicio da rampa
static volatile unsigned long gRampStartMs = 0;
static volatile bool gRamping      = false;

/* ── Vibracao ───────────────────────────────────────────────────── */
static volatile int gVibMode = VIB_OFF;

/* Aplica a porcentagem no PWM da luz. */
static void writeLight(int percent) {
  percent = constrain(percent, 0, 100);
  gLightCurrent = percent;
  pwmWrite(PWM_LIGHT, LEDC_CH_LIGHT, map(percent, 0, 100, 0, 255));
}

/*
 * Task da luz: interpola de gRampFrom ate gLightTarget em LIGHT_RAMP_MS,
 * dando um passo a cada LIGHT_RAMP_STEP_MS. E isso que produz o
 * "amanhecer" gradual em vez de acender de uma vez.
 */
static void lightTask(void* arg) {
  (void)arg;
  for (;;) {
    if (gRamping) {
      unsigned long elapsed = millis() - gRampStartMs;
      if (elapsed >= LIGHT_RAMP_MS) {
        writeLight(gLightTarget);
        gRamping = false;
      } else {
        int span = gLightTarget - gRampFrom;
        int val  = gRampFrom + (int)((long)span * (long)elapsed / LIGHT_RAMP_MS);
        writeLight(val);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(LIGHT_RAMP_STEP_MS));
  }
}

/* Task da vibracao: mantem ligado, desligado ou pulsando. */
static void vibTask(void* arg) {
  (void)arg;
  for (;;) {
    switch (gVibMode) {
      case VIB_ON:
        pwmWrite(PWM_VIB, LEDC_CH_VIB, VIB_DUTY_ON);
        vTaskDelay(pdMS_TO_TICKS(50));
        break;

      case VIB_PULSE:
        pwmWrite(PWM_VIB, LEDC_CH_VIB, VIB_DUTY_ON);
        for (int i = 0; i < VIB_PULSE_ON_MS / 50 && gVibMode == VIB_PULSE; i++)
          vTaskDelay(pdMS_TO_TICKS(50));      // sai rapido se o modo mudar

        pwmWrite(PWM_VIB, LEDC_CH_VIB, 0);
        for (int i = 0; i < VIB_PULSE_OFF_MS / 50 && gVibMode == VIB_PULSE; i++)
          vTaskDelay(pdMS_TO_TICKS(50));
        break;

      default:
        pwmWrite(PWM_VIB, LEDC_CH_VIB, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        break;
    }
  }
}

/* Configura os dois canais PWM e sobe as tasks. */
void actuatorBegin() {
  pwmAttach(PWM_LIGHT, LEDC_CH_LIGHT, LIGHT_FREQ, LIGHT_RES);
  pwmAttach(PWM_VIB,   LEDC_CH_VIB,   VIB_FREQ,   VIB_RES);
  writeLight(0);
  pwmWrite(PWM_VIB, LEDC_CH_VIB, 0);

  xTaskCreatePinnedToCore(lightTask, "light", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(vibTask,   "vib",   2048, NULL, 1, NULL, 1);
}

/*
 * Define o alvo da luz. Subida e gradual (rampa de 30 s); descida e
 * imediata, porque "apagar a luz" precisa ser instantaneo.
 */
void setLight(int percent) {
  percent = constrain(percent, 0, 100);
  gLightTarget = percent;

  if (percent <= gLightCurrent) {
    gRamping = false;
    writeLight(percent);
  } else {
    gRampFrom    = gLightCurrent;
    gRampStartMs = millis();
    gRamping     = true;
  }
}

/* Alvo configurado da luz, em %. */
int lightTarget() { return gLightTarget; }

/* Valor aplicado agora na luz, em %. */
int lightCurrent() { return gLightCurrent; }

/* Define o modo de vibracao. */
void setVib(int mode) {
  gVibMode = constrain(mode, VIB_OFF, VIB_PULSE);
}

/* Modo de vibracao atual. */
int vibMode() { return gVibMode; }

/* Nome do modo de vibracao, para o STATUS. */
const char* vibName() {
  switch (gVibMode) {
    case VIB_ON:    return "ON";
    case VIB_PULSE: return "PULSE";
    default:        return "OFF";
  }
}
