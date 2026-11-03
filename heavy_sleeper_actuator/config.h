/*
 * config.h — Pinos e constantes do Actuator ESP32 (30 pinos, sem tela).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FW_VERSION "0.4.0-alpha"

/* ── Luz por MOSFET — RESERVADA ─────────────────────────────────────
   Desde a v0.4 a luz do despertar e a lampada Wi-Fi WiZ, controlada pelo
   Display. O canal de luz (GPIO32) ficou reservado para testes: com a
   linha abaixo comentada o pino nao e configurado e o comando LIGHT:
   responde NACK:LIGHT_DISABLED. Para reativar, descomente AQUI e no
   config.h do Display.                                                 */
// #define USE_MOSFET_LIGHT

/* ── Pinos ──────────────────────────────────────────────────────── */
#define PIN_UART2_RX 16   // recebe do Display ESP32
#define PIN_UART2_TX 17   // envia ao Display ESP32
#define PWM_LIGHT    32   // reservado: gate do MOSFET da luz (USE_MOSFET_LIGHT)
#define PWM_VIB      33   // -> resistor 100-470R -> gate do MOSFET da vibracao
#define LED_STATUS    2   // LED azul onboard

/* ── Canais LEDC (usados apenas no core 2.x; ver pwm.h) ─────────── */
#define LEDC_CH_LIGHT 0
#define LEDC_CH_VIB   1

/* ── PWM ────────────────────────────────────────────────────────── */
#define LIGHT_FREQ 5000   // 5 kHz: sem cintilacao visivel
#define LIGHT_RES  8      // 8 bits -> 0..255
#define VIB_FREQ   200    // motores vibracall respondem bem entre 100-300 Hz
#define VIB_RES    8

#define VIB_DUTY_ON  204  // 80% de 255

/* ── Rampa da luz (despertar gradual) ───────────────────────────── */
#define LIGHT_RAMP_MS      30000   // atinge o alvo em 30 s
#define LIGHT_RAMP_STEP_MS 500     // incrementa a cada 500 ms

/* ── Pulso da vibracao ──────────────────────────────────────────── */
#define VIB_PULSE_ON_MS  600
#define VIB_PULSE_OFF_MS 400

/* ── Comunicacao ────────────────────────────────────────────────── */
#define UART_BAUD          115200
#define WATCHDOG_IDLE_MS   60000   // sem comandos por 60 s -> envia PING?
#define WATCHDOG_REPLY_MS  5000    // sem resposta em 5 s -> pisca o LED

/* ── Modos de vibracao ──────────────────────────────────────────── */
enum VibState { VIB_OFF = 0, VIB_ON, VIB_PULSE };

#endif /* CONFIG_H */
