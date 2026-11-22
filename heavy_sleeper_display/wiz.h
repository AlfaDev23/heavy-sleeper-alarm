/*
 * wiz.h — Lampada Wi-Fi WiZ (Philips Smart LED / app WiZ) pela rede local.
 *
 * Protocolo: JSON por UDP na porta 38899, sem nuvem e sem conta. Exemplo:
 *   {"id":1,"method":"setPilot","params":{"state":true,"dimming":40,"temp":3000}}
 * A lampada responde {"method":"setPilot",...,"result":{"success":true}}.
 *
 * No app WiZ, a opcao de "comunicacao local" precisa estar ligada
 * (Configuracoes -> Seguranca, nas versoes recentes do app).
 *
 * IP da lampada: netcfg.wizIp se preenchido; senao, a primeira lampada que
 * responder a uma busca por broadcast na rede.
 */
#ifndef WIZ_H
#define WIZ_H

#include "config.h"

void wizBegin();
void wizUpdate();                     // respostas, reenvios, busca e ping (loop)

/* Comandos. Repetir o mesmo valor em menos de 20 s nao gera trafego. */
void wizSetLight(int dimmingPct, int tempK);   // liga com brilho e cor
void wizOff();
void wizTestBlink();                  // pisca 2x e volta ao estado anterior

/* Busca por broadcast; o resultado aparece em wizFound*() em ~2 s. */
void wizDiscover();
bool wizDiscovering();
int         wizFoundCount();
const char* wizFoundIp(int i);
const char* wizFoundMac(int i);

const char* wizTargetIp();            // IP em uso ("" = nenhuma lampada)
bool        wizTargetIsFixed();       // true se veio do netcfg.wizIp
bool        wizOnline();              // respondeu nos ultimos WIZ_ONLINE_MS
const char* wizStatusName();          // texto curto para a tela

#endif /* WIZ_H */
