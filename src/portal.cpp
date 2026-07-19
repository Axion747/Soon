#include "portal.h"
#include "config.h"
#include "portal_html.h"
#include "settings.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

static WebServer server(80);
static DNSServer dns;
static bool s_dnsRunning = false;
static bool s_serverStarted = false;
static PortalState s_state;

PortalState &portalState() { return s_state; }

// ---------- helpers ----------
static uint32_t s_lastHttpMs = 0;  // anyone using the setup page right now?
uint32_t portalMsSinceActivity() { return millis() - s_lastHttpMs; }

static void sendJson(const String &json) {
  s_lastHttpMs = millis();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void redirectToPortal() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

static const char *connStr(ConnState c) {
  switch (c) {
    case ConnState::CONNECTING: return "connecting";
    case ConnState::OK: return "ok";
    case ConnState::FAIL: return "fail";
    default: return "idle";
  }
}

static const char *updStr(UpdState u) {
  switch (u) {
    case UpdState::CHECKING: return "checking";
    case UpdState::NONE: return "none";
    case UpdState::FOUND: return "found";
    case UpdState::UPDATING: return "updating";
    case UpdState::ERR: return "error";
    default: return "idle";
  }
}

// ---------- route handlers ----------
static void handleRoot() {
  s_lastHttpMs = millis();
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", PORTAL_HTML);
}

static void handleStatus() {
  JsonDocument doc;
  doc["mode"] = s_state.apMode ? "ap" : "sta";
  doc["connected"] = (WiFi.status() == WL_CONNECTED);
  doc["ssid"] = s_state.apMode ? settings().ssid : WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  doc["version"] = FW_VERSION;
  doc["board"] = SOON_BOARD_ID;
  doc["title"] = settings().title;
  doc["date"] = settings().dateStr;
  doc["tz"] = settings().tzKey;
  doc["msg"] = settings().message;
  doc["connstate"] = connStr(s_state.conn);
  doc["update"] = updStr(s_state.upd);
  doc["updateVer"] = s_state.updateVer;
  doc["build"] = SOON_BUILD;
  // device clock, for the "Device clock" row on the page
  time_t nowT = time(nullptr);
  if (nowT > 1609459200) {
    struct tm lt;
    localtime_r(&nowT, &lt);
    char buf[40];
    strftime(buf, sizeof(buf), "%b %e, %I:%M %p", &lt);
    doc["time"] = String(buf);
  } else {
    doc["time"] = "not set";
  }
  doc["timeSynced"] = s_state.ntpSynced;
  JsonArray tzs = doc["tzs"].to<JsonArray>();
  for (size_t i = 0; i < tzCount(); i++) {
    const char *k, *label;
    tzAt(i, &k, &label);
    JsonObject o = tzs.add<JsonObject>();
    o["k"] = k;
    o["label"] = label;
  }
  String out;
  serializeJson(doc, out);
  sendJson(out);
}

// Scanning while our own AP is broadcasting is unreliable on the ESP32 (the
// radio can't leave the AP channel for long, so scans often come back empty).
// Strategy: do one solid BLOCKING scan before the AP exists (portalPreScan),
// cache it, and serve the cache instantly. "Scan again" kicks off a
// best-effort async rescan; if it finds anything, the cache is refreshed.
static String s_scanCache;

static String buildScanJson(int n) {
  JsonDocument doc;
  JsonArray nets = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool dup = false;  // hide duplicate SSIDs (mesh APs etc.)
    for (JsonObject o : nets) {
      if (ssid == o["ssid"].as<const char *>()) { dup = true; break; }
    }
    if (dup) continue;
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

void portalPreScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  // stop any in-flight connect attempt; it blocks scans
  delay(120);
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false, /*passive=*/false,
                            /*ms per channel=*/360);
  Serial.printf("[portal] pre-scan found %d networks\n", n);
  if (n > 0) s_scanCache = buildScanJson(n);
  WiFi.scanDelete();
}

static void handleScan() {
  bool wantFresh = (server.arg("fresh") == "1");
  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    sendJson("{\"status\":\"scanning\"}");
    return;
  }
  if (n >= 0) {
    if (n > 0) s_scanCache = buildScanJson(n);  // keep old cache on empty result
    WiFi.scanDelete();
  } else if (wantFresh || !s_scanCache.length()) {
    // explicit rescan (or nothing cached yet): start an async scan and let the
    // page poll until it finishes
    WiFi.scanNetworks(true, false, false, 360);
    sendJson("{\"status\":\"scanning\"}");
    return;
  }
  sendJson(s_scanCache.length() ? s_scanCache : "{\"networks\":[]}");
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  settingsSaveCountdown(server.arg("title"), server.arg("date"), server.arg("tz"));
  settingsSaveMessage(server.arg("msg"));
  sendJson("{\"ok\":true}");
  if (ssid.length()) appOnWifiSubmit(ssid, pass);
  appOnSettingsSaved();
}

static void handleSettings() {
  settingsSaveCountdown(server.arg("title"), server.arg("date"), server.arg("tz"));
  settingsSaveMessage(server.arg("msg"));
  sendJson("{\"ok\":true}");
  appOnSettingsSaved();
}

static void handleCheckUpdate() {
  sendJson("{\"ok\":true}");
  appOnCheckUpdate();
}

static void handleForget() {
  sendJson("{\"ok\":true}");
  appOnForget();
}

static void handleTime() {
  long epoch = server.arg("epoch").toInt();
  sendJson("{\"ok\":true}");
  if (epoch > 1609459200L) appOnTimeSet((time_t)epoch);
}

static void handleNotFound() {
  if (s_state.apMode) {
    // Captive portal: any unknown URL bounces to the setup page
    redirectToPortal();
  } else {
    server.send(404, "text/plain", "not found");
  }
}

// ---------- lifecycle ----------
void portalBegin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/checkupdate", HTTP_POST, handleCheckUpdate);
  server.on("/api/forget", HTTP_POST, handleForget);
  server.on("/api/time", HTTP_POST, handleTime);

  // OS connectivity probes -> redirect, which makes phones pop the portal open
  const char *probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                          "/library/test/success.html", "/connecttest.txt",
                          "/ncsi.txt", "/success.txt", "/canonical.html", "/fwlink"};
  for (const char *p : probes) server.on(p, [] { handleNotFound(); });

  server.onNotFound(handleNotFound);
  // NOTE: server.begin() must NOT happen here — opening a socket before
  // WiFi.mode() initializes the TCP/IP stack panics LwIP ("Invalid mbox").
}

void portalStartServer() {
  if (s_serverStarted) return;
  server.begin();
  s_serverStarted = true;
}

void portalStartAP() {
  s_state.apMode = true;
  WiFi.mode(WIFI_AP_STA);  // AP for the phone + STA so we can scan/join
  WiFi.setSleep(false);    // keep the setup page snappy
  WiFi.softAP(SETUP_AP_SSID);
  delay(100);
  dns.start(53, "*", WiFi.softAPIP());
  s_dnsRunning = true;
  portalStartServer();     // network stack exists now — safe to listen
}

void portalStopAP() {
  if (s_dnsRunning) {
    dns.stop();
    s_dnsRunning = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  s_state.apMode = false;
}

void portalStartMDNS() {
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
  }
}

void portalHandle() {
  if (s_dnsRunning) dns.processNextRequest();
  server.handleClient();
}
