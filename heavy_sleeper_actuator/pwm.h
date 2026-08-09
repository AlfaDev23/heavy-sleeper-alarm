/*
 * pwm.h — Camada de compatibilidade LEDC entre o core ESP32 Arduino 2.x e 3.x.
 *
 * No core 2.x o PWM era por CANAL (ledcSetup/ledcAttachPin/ledcWrite(canal,...)).
 * No core 3.x essas funcoes foram REMOVIDAS e a API passou a ser por PINO
 * (ledcAttach/ledcWrite(pino,...)). As funcoes abaixo escolhem a certa em
 * tempo de compilacao, entao o mesmo codigo roda nos dois cores.
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
