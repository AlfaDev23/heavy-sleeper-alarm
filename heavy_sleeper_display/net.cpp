/*
 * net.cpp — Wi-Fi, ponto de acesso do portal, mDNS e NTP.
 *
 * DETALHE IMPORTANTE (AP + STA ao mesmo tempo):
 *   o ESP32 tem um radio so. Enquanto a interface STA procura a rede de
 *   casa ela troca de canal, e quem esta conectado no portal (AP) cai.
 *   Por isso, com o portal aberto, so tentamos reconectar quando nao ha
 *   nenhum celular no portal — e o reconnect automatico do core fica
 *   desligado; quem decide quando tentar e esta maquina de estados.
 *
 * DETALHE IMPORTANTE (trocar de modo logo depois de mexer no Wi-Fi):
 *   neste core a lista de callbacks da biblioteca Network (_cbEventList) nao
 *   tem trava (NETWORK_EVENTS_MUTEX vem desligado). A task arduino_events
 *   percorre essa lista e dorme dentro do callback do STA_START, no
 *   esp_wifi_set_ps(). Se o loop chamar WiFi.mode(WIFI_AP_STA) nesses ~2 ms, o
 *   AP.onEnable() insere na mesma lista, a task acorda com o iterador invalido
 *   e o Core 1 trava: reinicia com rst:0x8 TG1WDT_SYS_RESET, sem Guru
 *   Meditation. Esperar WiFi.STA.started() nao resolve, porque esse bit sobe
 *   antes do callback. Por isso o netBegin() liga direto no modo final.
 */
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <time.h>
#include "net.h"
#include "storage.h"
#include "clock.h"

static NetState      state          = NET_NO_CREDS;
static unsigned long stateMs        = 0;      // quando entrou no estado atual
static unsigned long lastRetryMs    = 0;
static unsigned long lastNtpCopyMs  = 0;
static bool          apOn           = false;
static bool          apManual       = false;  // aberto pelo botao da tela
static unsigned long apSinceMs      = 0;
static unsigned long apIdleSinceMs  = 0;      // desde quando nao ha clientes
static bool          mdnsOn         = false;
static bool          ntpConfigured  = false;
static char          apSsid[24]     = "";

/* ───────────────────────── ponto de acesso ─────────────────────── */

/* Monta "Despertador-XXXX" com os 2 ultimos bytes do MAC do ponto de acesso.
   Le direto do eFuse: WiFi.macAddress() so preenche o buffer com a STA de
   pe e, antes disso, deixaria lixo de pilha no nome da rede. */
static void buildApSsid() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(apSsid, sizeof(apSsid), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
}

static void apStart(bool manual) {
  if (apOn) { apManual = apManual || manual; apSinceMs = millis(); return; }
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, AP_PASSWORD);
  apOn          = true;
  apManual      = manual;
  apSinceMs     = millis();
  apIdleSinceMs = millis();
  Serial.printf("[NET] portal aberto: %s / %s em %s\n",
                apSsid, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
}

static void apStop() {
  if (!apOn) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn     = false;
  apManual = false;
  Serial.println(F("[NET] portal fechado"));
}

/* ─────────────────────────── conexao ───────────────────────────── */

static void setState(NetState s) {
  state   = s;
  stateMs = millis();
}

static void beginConnect() {
  Serial.printf("[NET] conectando em '%s'...\n", netcfg.ssid);
  WiFi.begin(netcfg.ssid, netcfg.pass);
  lastRetryMs = millis();
  setState(NET_CONNECTING);
}

/* Acabou de entrar na rede de casa: hora, nome .local e log. */
static void onConnected() {
  Serial.printf("[NET] conectado: IP %s, RSSI %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());

  if (!ntpConfigured) {
    configTzTime(netcfg.tz, NTP_SERVER_1, NTP_SERVER_2);
    ntpConfigured = true;
  }
  if (!mdnsOn && MDNS.begin(NET_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsOn = true;
    Serial.printf("[NET] http://%s.local\n", NET_HOSTNAME);
  }
  setState(NET_CONNECTED);
}

/* ─────────────────────────── API ───────────────────────────────── */

void netBegin() {
  WiFi.persistent(false);          // credenciais ficam na NOSSA NVS, nao na do core
  WiFi.setAutoReconnect(false);    // ver nota no topo do arquivo
  // Estes dois valem so se vierem ANTES do mode(): o core aplica ao ligar a STA.
  WiFi.setHostname(NET_HOSTNAME);
  WiFi.setSleep(false);            // UDP da lampada e portal respondem melhor
  buildApSsid();

  // Ja liga no modo final, sem passar por WIFI_STA (ver nota no topo do arquivo).
  WiFi.mode(netcfg.ssid[0] == '\0' ? WIFI_AP_STA : WIFI_STA);

  if (netcfg.ssid[0] == '\0') {
    Serial.println(F("[NET] nenhuma rede salva"));
    setState(NET_NO_CREDS);
    apStart(false);
  } else {
    beginConnect();
  }
}

void netUpdate() {
  unsigned long now = millis();
  wl_status_t   st  = WiFi.status();

  switch (state) {
    case NET_NO_CREDS:
      break;

    case NET_CONNECTING:
      if (st == WL_CONNECTED) {
        onConnected();
      } else if (now - stateMs >= NET_CONNECT_TIMEOUT_MS) {
        Serial.println(F("[NET] nao conectou -> portal aberto"));
        WiFi.disconnect(false);
        setState(NET_FAILED);
        apStart(false);
      }
      break;

    case NET_FAILED:
      // Tenta de novo de tempos em tempos, mas so se ninguem estiver
      // usando o portal (a busca derrubaria o celular conectado).
      if (now - lastRetryMs >= NET_RETRY_MS && netApClients() == 0) {
        beginConnect();
      }
      break;

    case NET_CONNECTED:
      if (st != WL_CONNECTED) {
        Serial.println(F("[NET] conexao perdida"));
        beginConnect();
      }
      break;
  }

  /* Fecha o portal sozinho quando ele nao e mais necessario. */
  if (apOn) {
    if (netApClients() > 0) apIdleSinceMs = now;
    bool idle = (now - apIdleSinceMs) >= AP_AUTOCLOSE_MS;
    if (apManual) {
      if (now - apSinceMs >= AP_MANUAL_MS && idle) apStop();
    } else if (state == NET_CONNECTED && idle &&
               now - stateMs >= AP_AUTOCLOSE_MS) {
      apStop();
    }
  }

  /* Copia a hora do sistema (NTP) para o relogio da tela. */
  if (now - lastNtpCopyMs >= (rtc.synced ? NTP_RESYNC_MS : 1000UL)) {
    lastNtpCopyMs = now;
    if (netTimeSynced()) {
#ifndef DEMO_MODE
      struct tm t;
      time_t    epoch = time(nullptr);
      localtime_r(&epoch, &t);
      bool first = !rtc.synced;
      rtc.syncTo(t.tm_hour, t.tm_min, t.tm_sec);
      if (first) Serial.printf("[NET] hora pelo NTP: %02d:%02d:%02d\n",
                               t.tm_hour, t.tm_min, t.tm_sec);
#endif
    }
  }
}

NetState netState()     { return state; }
bool     netConnected() { return state == NET_CONNECTED; }
IPAddress netIp()       { return WiFi.localIP(); }

const char* netStateName() {
  switch (state) {
    case NET_CONNECTED:  return "CONECTADO";
    case NET_CONNECTING: return "CONECTANDO";
    case NET_FAILED:     return "SEM CONEXAO";
    default:             return "CONFIGURAR";
  }
}

bool        netApActive()  { return apOn; }
const char* netApSsid()    { return apSsid; }
IPAddress   netApIp()      { return WiFi.softAPIP(); }
int         netApClients() { return apOn ? WiFi.softAPgetStationNum() : 0; }

void netOpenPortal()  { apStart(true); }
void netClosePortal() { if (state != NET_NO_CREDS) apStop(); }

void netSetCredentials(const char* ssid, const char* pass) {
  strncpy(netcfg.ssid, ssid, sizeof(netcfg.ssid) - 1);
  netcfg.ssid[sizeof(netcfg.ssid) - 1] = '\0';
  strncpy(netcfg.pass, pass, sizeof(netcfg.pass) - 1);
  netcfg.pass[sizeof(netcfg.pass) - 1] = '\0';
  storageSaveNet();

  WiFi.disconnect(false);
  beginConnect();                 // o portal continua aberto ate conectar
}

void netForget() {
  netcfg.ssid[0] = '\0';
  netcfg.pass[0] = '\0';
  storageSaveNet();
  WiFi.disconnect(false);
  setState(NET_NO_CREDS);
  apStart(false);
}

void netApplyTimezone() {
  setenv("TZ", netcfg.tz, 1);
  tzset();
  if (rtc.synced) lastNtpCopyMs = 0;   // reaplica ja no proximo loop
}

/* O relogio do sistema comeca em 1970; depois do NTP passa de 2023. */
bool netTimeSynced() {
  return time(nullptr) > 1700000000L;
}
