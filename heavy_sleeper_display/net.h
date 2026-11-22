/*
 * net.h — Wi-Fi do Display: conexao na rede de casa, portal de
 *         configuracao (ponto de acesso proprio), mDNS e hora por NTP.
 *
 * Tudo e nao-bloqueante: netUpdate() e chamado a cada volta do loop.
 *
 *   sem rede salva ............ abre o portal "Despertador-XXXX" e espera
 *   rede salva ................ tenta conectar por NET_CONNECT_TIMEOUT_MS
 *     conectou ................ NTP + mDNS (http://despertador.local)
 *     nao conectou ............ abre o portal e tenta de novo a cada minuto
 *
 * O portal web (portal.cpp) responde tanto pelo ponto de acesso
 * (192.168.4.1) quanto pela rede de casa, entao serve para qualquer ajuste
 * futuro, nao so para cadastrar o Wi-Fi.
 */
#ifndef NET_H
#define NET_H

#include <WiFi.h>
#include "config.h"

enum NetState {
  NET_NO_CREDS = 0,   // nunca configurado: so o portal
  NET_CONNECTING,     // tentando a rede de casa
  NET_CONNECTED,      // na rede de casa
  NET_FAILED          // nao conectou: portal aberto, nova tentativa em breve
};

void netBegin();                               // sobe o Wi-Fi conforme a NVS
void netUpdate();                              // maquina de estados (loop)

NetState    netState();
bool        netConnected();
const char* netStateName();                    // texto curto para a tela
IPAddress   netIp();                           // IP na rede de casa

bool        netApActive();                     // portal (ponto de acesso) no ar?
const char* netApSsid();                       // "Despertador-AB12"
IPAddress   netApIp();                         // 192.168.4.1
int         netApClients();                    // celulares conectados ao portal
void        netOpenPortal();                   // abre o portal pela tela (10 min)
void        netClosePortal();

/* Troca a rede: grava na NVS e reconecta (chamado pelo portal). */
void netSetCredentials(const char* ssid, const char* pass);
void netForget();                              // apaga a rede e abre o portal
void netApplyTimezone();                       // reaplica netcfg.tz

bool netTimeSynced();                          // hora ja veio do NTP?

#endif /* NET_H */
