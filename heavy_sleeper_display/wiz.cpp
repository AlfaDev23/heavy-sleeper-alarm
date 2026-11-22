/*
 * wiz.cpp — Cliente UDP da lampada WiZ, nao-bloqueante.
 *
 * UDP nao garante entrega, entao cada setPilot espera a resposta da lampada
 * por WIZ_ACK_TIMEOUT_MS e e reenviado ate WIZ_RETRIES vezes. Um comando
 * novo substitui o pendente (so o ultimo estado importa).
 */
#include <WiFi.h>
#include <WiFiUdp.h>
#include "wiz.h"
#include "net.h"
#include "storage.h"

static WiFiUDP udp;
static bool    udpOn      = false;
static bool    wasOnline  = false;          // netConnected() da volta anterior

/* ── alvo ───────────────────────────────────────────────────────── */
static char          targetIp[16] = "";
static unsigned long lastSeenMs   = 0;       // ultima resposta da lampada
static bool          everSeen     = false;
static unsigned long lastPingMs   = 0;
static unsigned long lastAutoFindMs = 0;

/* ── estado desejado e comando pendente ─────────────────────────── */
static bool desValid = false, desOn = false;
static int  desDim   = 0,     desTemp = 0;
static bool sentValid = false, sentOn = false;   // ultimo estado CONFIRMADO
static int  sentDim  = 0,      sentTemp = 0;

/* Repetir o mesmo estado so e ignorado por pouco tempo: depois disso o
   comando sai de novo, porque alguem pode ter mexido na lampada pelo app. */
#define WIZ_DEDUPE_MS 20000UL
static unsigned long lastPushMs = 0;

static char          pendBuf[128];
static bool          pendWaiting = false;
static int           pendTries   = 0;
static unsigned long pendSentMs  = 0;

/* ── busca ──────────────────────────────────────────────────────── */
static bool          discovering   = false;
static unsigned long discoverEndMs = 0;
static int           foundCount    = 0;
static char          foundIp [WIZ_MAX_FOUND][16];
static char          foundMac[WIZ_MAX_FOUND][13];

/* ── piscar de teste ────────────────────────────────────────────── */
static int           blinkStep = 0;          // 0 = parado
static unsigned long blinkAtMs = 0;
static bool  blinkRestValid = false, blinkRestOn = false;
static int   blinkRestDim   = 0,     blinkRestTemp = 0;

/* ═══════════════════════════════════════════════════════════════════
   Baixo nivel
   ═══════════════════════════════════════════════════════════════════ */

static bool sendTo(const char* ip, const char* json) {
  if (!udpOn || !ip[0]) return false;
  IPAddress addr;
  if (!addr.fromString(ip)) return false;
  udp.beginPacket(addr, WIZ_PORT);
  udp.write((const uint8_t*)json, strlen(json));
  return udp.endPacket() == 1;
}

static void sendBroadcast(const char* json) {
  if (!udpOn) return;
  // Broadcast geral e o da sub-rede: alguns roteadores so repassam um deles.
  IPAddress targets[2] = { IPAddress(255, 255, 255, 255), WiFi.broadcastIP() };
  for (int i = 0; i < 2; i++) {
    udp.beginPacket(targets[i], WIZ_PORT);
    udp.write((const uint8_t*)json, strlen(json));
    udp.endPacket();
  }
}

/* Monta o setPilot do estado desejado e o envia (com reenvio). */
static void pushDesired() {
  if (!desValid) return;
  if (desOn) {
    snprintf(pendBuf, sizeof(pendBuf),
             "{\"id\":1,\"method\":\"setPilot\",\"params\":"
             "{\"state\":true,\"dimming\":%d,\"temp\":%d}}", desDim, desTemp);
  } else {
    snprintf(pendBuf, sizeof(pendBuf),
             "{\"id\":1,\"method\":\"setPilot\",\"params\":{\"state\":false}}");
  }
  lastPushMs  = millis();
  pendTries   = 0;
  pendWaiting = true;
  pendSentMs  = millis() - WIZ_ACK_TIMEOUT_MS;   // envia ja no wizUpdate()
}

/* Guarda IP/MAC de uma lampada achada na busca (sem repetir). */
static void addFound(const char* ip, const char* mac) {
  for (int i = 0; i < foundCount; i++) if (strcmp(foundIp[i], ip) == 0) return;
  if (foundCount >= WIZ_MAX_FOUND) return;
  strncpy(foundIp[foundCount],  ip,  15); foundIp[foundCount][15]  = '\0';
  strncpy(foundMac[foundCount], mac, 12); foundMac[foundCount][12] = '\0';
  foundCount++;
  Serial.printf("[WIZ] lampada encontrada: %s (%s)\n", ip, mac);
}

/* Copia o valor de "mac":"xxxxxxxxxxxx" do JSON, se existir. */
static void parseMac(const char* json, char out[13]) {
  out[0] = '\0';
  const char* p = strstr(json, "\"mac\":\"");
  if (!p) return;
  p += 7;
  int i = 0;
  while (i < 12 && p[i] && p[i] != '"') { out[i] = p[i]; i++; }
  out[i] = '\0';
}

/* Le todas as respostas pendentes no socket. */
static void readReplies() {
  if (!udpOn) return;
  int len;
  while ((len = udp.parsePacket()) > 0) {
    char buf[512];
    int n = udp.read((uint8_t*)buf, sizeof(buf) - 1);
    if (n <= 0) continue;
    buf[n] = '\0';

    char ip[16];
    strncpy(ip, udp.remoteIP().toString().c_str(), sizeof(ip) - 1);
    ip[sizeof(ip) - 1] = '\0';

    if (discovering && strstr(buf, "\"getPilot\"")) {
      char mac[13];
      parseMac(buf, mac);
      addFound(ip, mac);
    }

    if (strcmp(ip, targetIp) != 0) continue;   // resposta de outra lampada
    lastSeenMs = millis();
    everSeen   = true;

    if (strstr(buf, "\"setPilot\"")) {
      if (strstr(buf, "\"success\":true")) {
        pendWaiting = false;
        sentValid = true; sentOn = desOn; sentDim = desDim; sentTemp = desTemp;
      } else if (strstr(buf, "\"error\"")) {
        pendWaiting = false;        // a lampada recusou (ex.: temp fora da faixa)
        Serial.printf("[WIZ] lampada recusou o comando: %s\n", buf);
      }
    }
  }
}

/* Escolhe o IP alvo: fixo da configuracao ou o primeiro achado. */
static void refreshTarget() {
  const char* want = netcfg.wizIp[0] ? netcfg.wizIp
                   : (foundCount > 0 ? foundIp[0] : "");
  if (strcmp(want, targetIp) == 0) return;
  strncpy(targetIp, want, sizeof(targetIp) - 1);
  targetIp[sizeof(targetIp) - 1] = '\0';
  sentValid = false;
  if (!netcfg.wizIp[0] && targetIp[0]) {
    everSeen   = true;                       // acabou de responder a busca
    lastSeenMs = millis();
  } else {
    everSeen   = false;
    lastPingMs = millis() - WIZ_PING_MS;     // IP fixo: pinga ja
  }
  Serial.printf("[WIZ] lampada alvo: %s\n", targetIp[0] ? targetIp : "(nenhuma)");
  if (targetIp[0] && desValid) pushDesired();   // reaplica o estado desejado
}

/* Maquina do piscar de teste: liga, apaga, liga, restaura. */
static void blinkUpdate() {
  if (!blinkStep || (long)(millis() - blinkAtMs) < 0) return;
  switch (blinkStep) {
    case 1: case 3:
      desValid = true; desOn = true; desDim = 100; desTemp = WIZ_TEMP_MAX;
      pushDesired(); break;
    case 2:
      desValid = true; desOn = false;
      pushDesired(); break;
    case 4:
      desValid = blinkRestValid; desOn = blinkRestOn;
      desDim = blinkRestDim; desTemp = blinkRestTemp;
      if (!desValid) { desValid = true; desOn = false; }
      pushDesired();
      blinkStep = 0;
      return;
  }
  blinkStep++;
  blinkAtMs = millis() + 700;
}

/* ═══════════════════════════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════════════════════════ */

void wizBegin() {
  udpOn = false;
  wasOnline = false;
}

void wizUpdate() {
  bool online = netConnected();
  unsigned long now = millis();

  if (online && !wasOnline) {                 // acabou de entrar na rede
    udpOn = udp.begin(WIZ_LOCAL_PORT);
    Serial.printf("[WIZ] UDP %s\n", udpOn ? "pronto" : "FALHOU");
    if (!netcfg.wizIp[0]) wizDiscover();
  } else if (!online && wasOnline) {
    udp.stop();
    udpOn = false;
  }
  wasOnline = online;
  if (!udpOn) return;

  readReplies();
  refreshTarget();
  blinkUpdate();

  if (discovering && (long)(now - discoverEndMs) >= 0) {
    discovering = false;
    Serial.printf("[WIZ] busca terminou: %d lampada(s)\n", foundCount);
    refreshTarget();
  }

  /* Reenvio do comando pendente. */
  if (pendWaiting && targetIp[0] && now - pendSentMs >= WIZ_ACK_TIMEOUT_MS) {
    if (pendTries >= WIZ_RETRIES) {
      pendWaiting = false;
      Serial.println(F("[WIZ] sem resposta da lampada"));
    } else {
      sendTo(targetIp, pendBuf);
      pendTries++;
      pendSentMs = now;
    }
  }

  /* Ping periodico para saber se a lampada esta viva. */
  if (targetIp[0] && now - lastPingMs >= WIZ_PING_MS) {
    lastPingMs = now;
    sendTo(targetIp, "{\"method\":\"getPilot\",\"params\":{}}");
  }

  /* Modo automatico sem lampada (ou com a lampada sumida): procura de novo.
     O IP pode ter mudado se o roteador nao reserva o endereco dela. */
  if (!netcfg.wizIp[0] && !discovering && now - lastAutoFindMs >= 60000UL &&
      (!targetIp[0] || (everSeen && !wizOnline()))) {
    lastAutoFindMs = now;
    foundCount = 0;
    targetIp[0] = '\0';
    wizDiscover();
  }
}

void wizSetLight(int dimmingPct, int tempK) {
  dimmingPct = constrain(dimmingPct, WIZ_DIM_MIN, WIZ_DIM_MAX);
  tempK      = constrain(tempK, WIZ_TEMP_MIN, WIZ_TEMP_MAX);
  if (desValid && desOn && desDim == dimmingPct && desTemp == tempK &&
      millis() - lastPushMs < WIZ_DEDUPE_MS) return;
  desValid = true; desOn = true; desDim = dimmingPct; desTemp = tempK;
  blinkStep = 0;
  pushDesired();
}

void wizOff() {
  if (desValid && !desOn && millis() - lastPushMs < WIZ_DEDUPE_MS) return;
  desValid = true; desOn = false;
  blinkStep = 0;
  pushDesired();
}

void wizTestBlink() {
  blinkRestValid = desValid; blinkRestOn = desOn;
  blinkRestDim   = desDim;   blinkRestTemp = desTemp;
  blinkStep = 1;
  blinkAtMs = millis();
}

void wizDiscover() {
  if (!udpOn) return;
  foundCount    = 0;
  discovering   = true;
  discoverEndMs = millis() + 2000;
  lastAutoFindMs = millis();
  sendBroadcast("{\"method\":\"getPilot\",\"params\":{}}");
  Serial.println(F("[WIZ] procurando lampadas..."));
}

bool        wizDiscovering()     { return discovering; }
int         wizFoundCount()      { return foundCount; }
const char* wizFoundIp(int i)    { return (i >= 0 && i < foundCount) ? foundIp[i]  : ""; }
const char* wizFoundMac(int i)   { return (i >= 0 && i < foundCount) ? foundMac[i] : ""; }
const char* wizTargetIp()        { return targetIp; }
bool        wizTargetIsFixed()   { return netcfg.wizIp[0] != '\0'; }

bool wizOnline() {
  return everSeen && (millis() - lastSeenMs < WIZ_ONLINE_MS);
}

const char* wizStatusName() {
  if (!netConnected())  return "SEM WI-FI";
  if (discovering)      return "PROCURANDO";
  if (!targetIp[0])     return "NAO ACHADA";
  return wizOnline() ? "ONLINE" : "SEM RESPOSTA";
}
