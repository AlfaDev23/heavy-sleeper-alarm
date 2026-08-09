/*
 * pwm.h — Camada de compatibilidade LEDC entre o core ESP32 Arduino 2.x e 3.x.
 *
 * POR QUE ISSO EXISTE:
 *   No core 2.x o PWM era configurado por CANAL:
 *       ledcSetup(canal, freq, bits); ledcAttachPin(pino, canal); ledcWrite(canal, duty);
 *   No core 3.x (instalado aqui: 3.3.5) essas funcoes FORAM REMOVIDAS e a API
 *   passou a ser por PINO:
 *       ledcAttach(pino, freq, bits); ledcWrite(pino, duty);
 *   Usar a API antiga no core 3.x da erro de compilacao. As funcoes abaixo
 *   escolhem a API certa em tempo de compilacao, entao o mesmo codigo roda
 *   nos dois cores.
 */
#ifndef PWM_H
#define PWM_H

#include <Arduino.h>

/* Configura um pino para PWM. O parametro 'canal' so e usado no core 2.x. */
static inline void pwmAttach(uint8_t pin, uint8_t channel, uint32_t freq, uint8_t resBits) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  (void)channel;
  ledcAttach(pin, freq, resBits);
#else
  ledcSetup(channel, freq, resBits);
  ledcAttachPin(pin, channel);
#endif
}

/* Escreve o duty cycle em um pino previamente configurado com pwmAttach(). */
static inline void pwmWrite(uint8_t pin, uint8_t channel, uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  (void)channel;
  ledcWrite(pin, duty);
#else
  (void)pin;
  ledcWrite(channel, duty);
#endif
}

#endif /* PWM_H */
