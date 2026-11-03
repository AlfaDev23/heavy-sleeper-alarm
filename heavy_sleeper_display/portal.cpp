/*
 * portal.cpp — Servidor HTTP do portal + DNS do captive portal.
 *
 * O WebServer e atendido de dentro do loop() (portalUpdate), no mesmo
 * contexto que a UI: nenhuma rota precisa de mutex para mexer em settings.
 *
 * Trocar de rede derruba a conexao de quem esta na pagina. Por isso
 * /api/wifi e /api/wifi/forget respondem primeiro e aplicam a mudanca
 * ~600 ms depois (pendingNetAction).
 */
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "portal.h"
#include "portal_page.h"
#include "net.h"
#include "wiz.h"
#include "storage.h"
#include "clock.h"
#include "night.h"
#include "app.h"

static WebServer server(80);
static DNSServer dns;
static bool      dnsOn = false;

enum PendingNet { PN_NONE = 0, PN_SET_WIFI, PN_FORGET };
static PendingNet    pendingNet   = PN_NONE;
static unsigned long pendingAtMs  = 0;
static char          pendSsid[33] = "";
static char          pendPass[65] = "";

static bool scanRunning = false;

/* ═══════════════════════════════════════════════════════════════════
   Utilidades
   ═══════════════════════════════════════════════════════════════════ */

/* Acrescenta uma string JSON (com aspas e escapes) ao buffer. */
static void jsonStr(String& out, const char* s) {
  out += '"';
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((uint8_t)c < 0x20) {
      char u[8]; snprintf(u, sizeof(u), "\\u%04x", (uint8_t)c); out += u;
    } else out += c;
  }
  out += '"';
}

static void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void sendError(int code, const char* msg) {
  String b = "{\"ok\":false,\"error\":";
  jsonStr(b, msg);
  b += '}';
  sendJson(code, b);
}

static void sendOk() { sendJson(200, "{\"ok\":true}"); }

/* "HH:MM" -> minutos do dia; -1 se invalido. */
static int parseHm(const String& s) {
  int c = s.indexOf(':');
  if (c < 1) return -1;
  int h = s.substring(0, c).toInt();
  int m = s.substring(c + 1, c + 3).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

/* Le um inteiro do formulario e valida a faixa. */
static bool argInt(const char* name, int lo, int hi, int& out) {
  if (!server.hasArg(name)) return false;
  String v = server.arg(name);
  if (v.length() == 0) return false;
  int x = v.toInt();
  if (x < lo || x > hi) return false;
  out = x;
  return true;
}

static bool validIp(const String& s) {
  IPAddress ip;
  return ip.fromString(s);
}

/* ═══════════════════════════════════════════════════════════════════
   Rotas
   ═══════════════════════════════════════════════════════════════════ */

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PORTAL_HTML);
}

/* Estado completo em JSON (sem a senha do Wi-Fi). */
static void handleState() {
  String o;
  o.reserve(900);
  char buf[48];

  o += "{\"fw\":"; jsonStr(o, FW_VERSION);
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", rtc.hour, rtc.minute, rtc.second);
  o += ",\"time\":"; jsonStr(o, buf);
  o += ",\"ntp\":"; o += rtc.synced ? "true" : "false";
  o += ",\"sunrise\":"; o += appSunriseActive() ? "true" : "false";

  o += ",\"net\":{\"state\":"; jsonStr(o, netStateName());
  o += ",\"ssid\":"; jsonStr(o, netcfg.ssid);
  o += ",\"ip\":";   jsonStr(o, netIp().toString().c_str());
  o += ",\"rssi\":"; o += netConnected() ? WiFi.RSSI() : 0;
  o += ",\"ap\":";   o += netApActive() ? "true" : "false";
  o += ",\"apSsid\":"; jsonStr(o, netApSsid());
  o += '}';

  o += ",\"wiz\":{\"status\":"; jsonStr(o, wizStatusName());
  o += ",\"ip\":";    jsonStr(o, wizTargetIp());
  o += ",\"fixed\":"; o += wizTargetIsFixed() ? "true" : "false";
  o += ",\"cfgIp\":"; jsonStr(o, netcfg.wizIp);
  o += ",\"discovering\":"; o += wizDiscovering() ? "true" : "false";
  o += ",\"found\":[";
  for (int i = 0; i < wizFoundCount(); i++) {
    if (i) o += ',';
    o += "{\"ip\":";  jsonStr(o, wizFoundIp(i));
    o += ",\"mac\":"; jsonStr(o, wizFoundMac(i));
    o += '}';
  }
  o += "]}";

  snprintf(buf, sizeof(buf),
           ",\"alarm\":{\"h\":%d,\"m\":%d,\"en\":%s,", settings.almHour,
           settings.almMin, settings.almEnabled ? "true" : "false");
  o += buf;
  snprintf(buf, sizeof(buf), "\"diff\":%d,\"type\":%d,\"chal\":%d,",
           settings.diffLevel, settings.diffType, settings.chalTimeS);
  o += buf;
  snprintf(buf, sizeof(buf), "\"ramp\":%d,\"light\":%d,\"vib\":%d,",
           settings.rampMin, settings.lightPct, settings.vibMode);
  o += buf;
  o += "\"busy\":"; o += appAlarmBusy() ? "true" : "false";
  o += '}';

  snprintf(buf, sizeof(buf), ",\"night\":{\"mode\":%d,\"start\":%d,\"end\":%d,",
           settings.nightMode, settings.nightStart, settings.nightEnd);
  o += buf;
  o += "\"active\":"; o += nightShouldBeActive() ? "true" : "false";
  o += '}';

  o += ",\"tz\":"; jsonStr(o, netcfg.tz);
  o += '}';
  sendJson(200, o);
}

/* Grava alarme, luz, modo noturno, IP da lampada e fuso. */
static void handleSave() {
  if (appAlarmBusy()) {
    sendError(409, "Alarme tocando: resolva o desafio primeiro");
    return;
  }

  Settings s = settings;          // so aplica se tudo validar
  int v;

  if (server.hasArg("almTime")) {
    v = parseHm(server.arg("almTime"));
    if (v < 0) { sendError(400, "Horario do alarme invalido"); return; }
    s.almHour = v / 60; s.almMin = v % 60;
  }
  if (server.hasArg("almEn")) s.almEnabled = server.arg("almEn") == "1";
  if (argInt("diff",  0, 3, v))   s.diffLevel = v;
  if (argInt("type",  0, 3, v))   s.diffType  = v;
  if (argInt("vib",   0, 2, v))   s.vibMode   = v;
  if (argInt("light", WIZ_DIM_MIN, 100, v)) s.lightPct = v;
  if (argInt("chal",  30, 120, v) && v % 30 == 0) s.chalTimeS = v;
  if (argInt("ramp",  5, 20, v)   && v % 5 == 0)  s.rampMin   = v;
  if (argInt("nightMode", 0, 2, v)) s.nightMode = v;
  if (server.hasArg("nightStart")) {
    v = parseHm(server.arg("nightStart"));
    if (v < 0) { sendError(400, "Inicio da noite invalido"); return; }
    s.nightStart = v;
  }
  if (server.hasArg("nightEnd")) {
    v = parseHm(server.arg("nightEnd"));
    if (v < 0) { sendError(400, "Fim da noite invalido"); return; }
    s.nightEnd = v;
  }

  bool netChanged = false;
  if (server.hasArg("wizIp")) {
    String ip = server.arg("wizIp");
    ip.trim();
    if (ip.length() && !validIp(ip)) { sendError(400, "IP da lampada invalido"); return; }
    if (strcmp(ip.c_str(), netcfg.wizIp) != 0) {
      strncpy(netcfg.wizIp, ip.c_str(), sizeof(netcfg.wizIp) - 1);
      netcfg.wizIp[sizeof(netcfg.wizIp) - 1] = '\0';
      netChanged = true;
    }
  }
  if (server.hasArg("tz")) {
    String tz = server.arg("tz");
    if (tz.length() == 0 || tz.length() >= sizeof(netcfg.tz)) {
      sendError(400, "Fuso invalido"); return;
    }
    if (strcmp(tz.c_str(), netcfg.tz) != 0) {
      strncpy(netcfg.tz, tz.c_str(), sizeof(netcfg.tz) - 1);
      netcfg.tz[sizeof(netcfg.tz) - 1] = '\0';
      netChanged = true;
      netApplyTimezone();
    }
  }

  settings = s;
  storageSave();
  if (netChanged) storageSaveNet();
  appSettingsChanged();
  sendOk();
}

/* Busca assincrona de redes: a primeira chamada dispara, as proximas leem. */
static void handleScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { sendJson(200, "{\"scanning\":true}"); return; }
  if (n < 0 || !scanRunning) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true /*async*/, false /*sem ocultas*/);
    scanRunning = true;
    sendJson(200, "{\"scanning\":true}");
    return;
  }

  String o = "{\"scanning\":false,\"nets\":[";
  int shown = 0;
  for (int i = 0; i < n && shown < 20; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    bool dup = false;                       // mesma rede em varios APs
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == ssid) { dup = true; break; }
    if (dup) continue;
    if (shown++) o += ',';
    o += "{\"ssid\":"; jsonStr(o, ssid.c_str());
    o += ",\"rssi\":"; o += WiFi.RSSI(i);
    o += ",\"open\":"; o += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "true" : "false";
    o += '}';
  }
  o += "]}";
  WiFi.scanDelete();
  scanRunning = false;
  sendJson(200, o);
}

static void handleWifi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32) { sendError(400, "Nome da rede invalido"); return; }
  if (pass.length() > 64 || (pass.length() > 0 && pass.length() < 8)) {
    sendError(400, "Senha deve ter de 8 a 64 caracteres (ou vazia para rede aberta)");
    return;
  }
  strncpy(pendSsid, ssid.c_str(), sizeof(pendSsid) - 1); pendSsid[sizeof(pendSsid) - 1] = '\0';
  strncpy(pendPass, pass.c_str(), sizeof(pendPass) - 1); pendPass[sizeof(pendPass) - 1] = '\0';
  pendingNet  = PN_SET_WIFI;
  pendingAtMs = millis() + 600;
  sendOk();
}

static void handleForget() {
  pendingNet  = PN_FORGET;
  pendingAtMs = millis() + 600;
  sendOk();
}

static void handleWizDiscover() {
  if (!netConnected()) { sendError(409, "Despertador sem Wi-Fi"); return; }
  wizDiscover();
  sendOk();
}

static void handleWizTest() {
  if (appAlarmBusy())       { sendError(409, "Alarme tocando"); return; }
  if (!wizTargetIp()[0])    { sendError(409, "Nenhuma lampada encontrada"); return; }
  wizTestBlink();
  sendOk();
}

/*
 * Qualquer outro endereco: com o portal (AP) aberto, redireciona para a
 * pagina — e isso que faz o celular abrir o portal sozinho ao conectar
 * (Android usa /generate_204, iPhone /hotspot-detect.html etc.).
 */
static void handleNotFound() {
  if (netApActive()) {
    String host = server.hostHeader();
    String ap   = netApIp().toString();
    if (host != ap) {
      server.sendHeader("Location", String("http://") + ap + "/", true);
      server.send(302, "text/plain", "");
      return;
    }
  }
  server.send(404, "text/plain", "nao encontrado");
}

/* ═══════════════════════════════════════════════════════════════════
   API publica
   ═══════════════════════════════════════════════════════════════════ */

void portalBegin() {
  server.on("/",                  HTTP_GET,  handleRoot);
  server.on("/api/state",         HTTP_GET,  handleState);
  server.on("/api/save",          HTTP_POST, handleSave);
  server.on("/api/scan",          HTTP_GET,  handleScan);
  server.on("/api/wifi",          HTTP_POST, handleWifi);
  server.on("/api/wifi/forget",   HTTP_POST, handleForget);
  server.on("/api/wiz/discover",  HTTP_POST, handleWizDiscover);
  server.on("/api/wiz/test",      HTTP_POST, handleWizTest);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("[PORTAL] servidor HTTP na porta 80"));
}

void portalUpdate() {
  /* DNS curinga so enquanto o ponto de acesso esta no ar. */
  if (netApActive() && !dnsOn) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dnsOn = dns.start(53, "*", netApIp());
  } else if (!netApActive() && dnsOn) {
    dns.stop();
    dnsOn = false;
  }
  if (dnsOn) dns.processNextRequest();

  server.handleClient();

  if (pendingNet != PN_NONE && (long)(millis() - pendingAtMs) >= 0) {
    if (pendingNet == PN_SET_WIFI) netSetCredentials(pendSsid, pendPass);
    else                           netForget();
    pendingNet = PN_NONE;
    pendPass[0] = '\0';
    appSettingsChanged();
  }
}
