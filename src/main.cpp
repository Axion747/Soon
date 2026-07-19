// ============================================================
//  Soon — page-centric main
//
//  UI: boot animation, then ONE bento page, always:
//    top-left  = the countdown/date     top-right = wifi state (red/yel/grn)
//    bottom    = her message ("i love you sm <3" by default)
//  Tapping the wifi box (touch) or short-pressing the SIDE button triggers a
//  reconnect in place (icon: yellow "reconnecting"). Short-pressing BOOT
//  shows the pairing instructions for ~12 s. Side button held 6 s forgets
//  WiFi. The message comes from the settings page or message.txt in the
//  GitHub repo (pulled every 30 min whenever the device has WiFi).
//
//  WiFi runs entirely in the background and never blocks the pages:
//    - tries the saved network, else her hotspot (baked-in default)
//    - if nothing works, quietly opens the Soon-Setup portal (instructions
//      live on the wifi page) and keeps retrying known networks
//    - once online: NTP time, then OTA updates from GitHub releases
//
//  Buttons:  side button short = next page, hold 6s = forget wifi + restart
//            BOOT button short = screen on/off
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "esp_sntp.h"

#include "config.h"
#include "portal.h"
#include "settings.h"
#include "ui.h"
#include "updater.h"

// ---------- background network manager ----------
enum class NetState { TRYING, AP_WAIT, ONLINE };
static NetState s_net = NetState::TRYING;

static String s_pendingSsid, s_pendingPass;
static bool s_credsArePending = false;  // save these to flash on success
static bool s_fromPortal = false;       // attempt came from the setup page
static uint32_t s_tryStart = 0;
static uint32_t s_connectedAt = 0;
static bool s_apShutdownDone = false;

static uint32_t s_lastUpdateCheck = 0;
static bool s_forceUpdateCheck = false;
static bool s_firstCheckDone = false;

// ---------- display ----------
static uint32_t s_lastDraw = 0;
static uint32_t s_wifiInfoUntil = 0;    // >now = pairing overlay is showing

// ---------- countdown ----------
static time_t s_targetEpoch = 0;
static String s_dateShort;              // "Sep 20"

static void applyTimezone() {
  configTzTime(tzPosix(settings().tzKey), "pool.ntp.org", "time.google.com", "time.nist.gov");
}

static bool clockIsSet() { return time(nullptr) > 1609459200; /* 2021-01-01 */ }

// ---------- offline timekeeping ----------
// The countdown must work with NO WiFi, ever. Time sources, best wins:
//   NTP (when WiFi happens to exist)  >  "set from phone" on the setup page
//   >  last time we saved to flash    >  the firmware's build timestamp
static volatile bool s_ntpSynced = false;

static time_t buildEpoch() {
  // __DATE__ is like "Sep 20 2026", __TIME__ like "12:34:56"
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mstr[4] = {0};
  int d = 1, y = 2026, hh = 0, mm = 0, ss = 0;
  sscanf(__DATE__, "%3s %d %d", mstr, &d, &y);
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  const char *p = strstr(months, mstr);
  struct tm t = {};
  t.tm_mon = p ? (int)(p - months) / 3 : 0;
  t.tm_mday = d;
  t.tm_year = y - 1900;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  t.tm_isdst = -1;
  return mktime(&t);
}

static void seedClock() {
  if (clockIsSet()) return;
  time_t saved = settingsLoadEpoch();
  time_t build = buildEpoch();
  time_t seed = saved > build ? saved : build;
  if (seed > 1609459200) {
    struct timeval tv = {seed, 0};
    settimeofday(&tv, nullptr);
    Serial.printf("[time] clock seeded %s\n", saved > build ? "from flash" : "from build time");
  }
}

static void persistClockTick() {
  static uint32_t lastSave = 0;
  if (millis() - lastSave > 6UL * 3600UL * 1000UL) {  // every 6h
    lastSave = millis();
    if (clockIsSet()) settingsSaveEpoch(time(nullptr));
  }
}

static void recomputeTarget() {
  int y = 0, m = 0, d = 0;
  if (sscanf(settings().dateStr.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || y < 2000) {
    y = 2026; m = 9; d = 20;
  }
  struct tm tmv = {};
  tmv.tm_year = y - 1900;
  tmv.tm_mon = m - 1;
  tmv.tm_mday = d;
  tmv.tm_isdst = -1;
  s_targetEpoch = mktime(&tmv);  // local midnight of the big day

  char buf[16];
  strftime(buf, sizeof(buf), "%b %e", &tmv);
  s_dateShort = String(buf);
  s_dateShort.replace("  ", " ");
  uiForceRedraw();
  s_lastDraw = 0;
}

// ---------- net transitions ----------
static void netTry(const String &ssid, const String &pass, bool pending, bool fromPortal) {
  s_net = NetState::TRYING;
  s_pendingSsid = ssid;
  s_pendingPass = pass;
  s_credsArePending = pending;
  s_fromPortal = fromPortal;
  s_tryStart = millis();
  if (fromPortal) portalState().conn = ConnState::CONNECTING;
  Serial.printf("[net] trying '%s'\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pass.c_str());
}

static void netEnterApWait() {
  s_net = NetState::AP_WAIT;
  if (!(WiFi.getMode() & WIFI_MODE_AP)) {
    portalStartAP();  // AP + captive portal; instructions live on the wifi page
    Serial.println("[net] setup AP up (Soon-Setup)");
  }
}

static void netOnline() {
  s_net = NetState::ONLINE;
  s_connectedAt = millis();
  s_apShutdownDone = !(WiFi.getMode() & WIFI_MODE_AP);
  portalState().conn = ConnState::OK;
  if (s_credsArePending) {
    settingsSaveWifi(s_pendingSsid, s_pendingPass);
    s_credsArePending = false;
  }
  if (s_apShutdownDone) {
    portalState().apMode = false;
    portalStartMDNS();
  }
  applyTimezone();
  recomputeTarget();
  s_lastUpdateCheck = millis();
  s_firstCheckDone = false;
  Serial.printf("[net] online, ip=%s\n", WiFi.localIP().toString().c_str());
}

static void netTick() {
  uint32_t now = millis();
  switch (s_net) {
    case NetState::TRYING: {
      if (WiFi.status() == WL_CONNECTED) { netOnline(); break; }
      if (now - s_tryStart > 25000) {
        Serial.println("[net] connect attempt timed out");
        if (s_fromPortal) portalState().conn = ConnState::FAIL;
        s_fromPortal = false;
        s_credsArePending = false;
        netEnterApWait();
      }
      break;
    }
    case NetState::AP_WAIT: {
      if (WiFi.status() == WL_CONNECTED) { netOnline(); break; }
      // Periodically retry a known network (saved one first, else the
      // hotspot default) — but not while someone's using the setup page.
      static uint32_t lastAutoTry = 0;
      bool haveKnown = settings().ssid.length() || strlen(DEFAULT_WIFI_SSID) > 0;
      if (haveKnown && now - lastAutoTry > 45000 && portalMsSinceActivity() > 60000) {
        lastAutoTry = now;
        bool useSaved = settings().ssid.length() > 0;
        netTry(useSaved ? settings().ssid : DEFAULT_WIFI_SSID,
               useSaved ? settings().pass : DEFAULT_WIFI_PASS,
               /*pending=*/!useSaved, /*fromPortal=*/false);
      }
      break;
    }
    case NetState::ONLINE: {
      // drop the setup AP a few seconds after success (portal page shows 🎉)
      if (!s_apShutdownDone && now - s_connectedAt > 8000) {
        s_apShutdownDone = true;
        portalStopAP();
        portalStartMDNS();
      }
      if (WiFi.status() != WL_CONNECTED) {
        // the stack auto-reconnects on its own; this is only a slow backstop
        // (nudging every few seconds fights the in-progress attempt and
        // spams "sta is connecting, return error")
        static uint32_t lastRetry = 0;
        if (now - lastRetry > 30000) {
          lastRetry = now;
          Serial.println("[net] wifi still down, nudging reconnect");
          WiFi.reconnect();
        }
      }
      break;
    }
  }
}

// ---------- update checking ----------
static void runUpdateCheck() {
  s_forceUpdateCheck = false;
  s_lastUpdateCheck = millis();
  s_firstCheckDone = true;
  portalState().upd = UpdState::CHECKING;

  UpdateOutcome out = updaterCheckAndApply([](int pct, const String &fromV, const String &toV) {
    portalState().upd = UpdState::UPDATING;
    portalState().updateVer = toV;
    uiUpdating(pct, fromV, toV);
  });

  switch (out) {
    case UpdateOutcome::APPLIED:
      uiMessage("All updated!", "restarting...");
      delay(800);
      ESP.restart();
      break;
    case UpdateOutcome::UP_TO_DATE:
      portalState().upd = UpdState::NONE;
      portalState().updateVer = updaterLatestSeen();
      break;
    case UpdateOutcome::NOT_CONFIGURED:
      portalState().upd = UpdState::IDLE;  // nothing to check yet — stay quiet
      break;
    default:
      portalState().upd = UpdState::ERR;
      break;
  }
  uiForceRedraw();
  s_lastDraw = 0;
}

// ---------- portal callbacks ----------
void appOnWifiSubmit(const String &ssid, const String &pass) {
  netTry(ssid, pass, /*pending=*/true, /*fromPortal=*/true);
}

void appOnSettingsSaved() {
  applyTimezone();
  recomputeTarget();
}

void appOnCheckUpdate() { s_forceUpdateCheck = true; }

void appOnForget() {
  settingsForgetWifi();
  delay(400);
  ESP.restart();
}

void appOnTimeSet(time_t epoch) {
  struct timeval tv = {epoch, 0};
  settimeofday(&tv, nullptr);
  settingsSaveEpoch(epoch);
  Serial.println("[time] clock set from phone");
  s_lastDraw = 0;
}

// ---------- buttons ----------
struct Btn {
  int pin;
  bool wasDown;
  uint32_t downAt;
  bool longFired;
  explicit Btn(int p) : pin(p), wasDown(false), downAt(0), longFired(false) {}
};
static Btn s_btnA(PIN_BUTTON_A);
static Btn s_btnB(PIN_BUTTON_B);

static void pollButton(Btn &b, void (*onShort)(), void (*onLong)()) {
  if (b.pin < 0) return;
  bool down = (digitalRead(b.pin) == LOW);
  uint32_t now = millis();
  if (down && !b.wasDown) {
    b.wasDown = true;
    b.downAt = now;
    b.longFired = false;
  } else if (down && b.wasDown && !b.longFired && now - b.downAt > 6000) {
    b.longFired = true;
    if (onLong) onLong();
  } else if (!down && b.wasDown) {
    b.wasDown = false;
    if (!b.longFired && now - b.downAt > 40 && now - b.downAt < 1500) {
      if (onShort) onShort();
    }
  }
}

// Reconnect in place — used by the wifi-box tap AND the side button, so the
// non-touch LCD board has the exact same ability.
static void tryReconnect() {
  if (s_net == NetState::TRYING) return;
  if (s_net == NetState::ONLINE && WiFi.status() == WL_CONNECTED) return;
  bool useSaved = settings().ssid.length() > 0;
  if (!useSaved && strlen(DEFAULT_WIFI_SSID) == 0) return;
  netTry(useSaved ? settings().ssid : DEFAULT_WIFI_SSID,
         useSaved ? settings().pass : DEFAULT_WIFI_PASS,
         /*pending=*/!useSaved, /*fromPortal=*/false);
  s_lastDraw = 0;
}

static void btnAShort() {  // side button = same as tapping the wifi box
  tryReconnect();
}

static void btnALong() {
  uiMessage("WiFi cleared", "restarting...");
  settingsForgetWifi();
  delay(1200);
  ESP.restart();
}

static void btnBShort() {  // BOOT button = pairing/info overlay
  s_wifiInfoUntil = (s_wifiInfoUntil > millis()) ? 0 : millis() + 12000;
  s_lastDraw = 0;
}

// ---------- drawing ----------
static WifiIcon wifiIconState() {
  if (s_net == NetState::ONLINE && WiFi.status() == WL_CONNECTED) return WifiIcon::GREEN;
  if (s_net == NetState::TRYING) return WifiIcon::YELLOW;
  return WifiIcon::RED;
}

static void drawCurrentPage() {
  if (s_wifiInfoUntil > millis()) {
    bool online = (s_net == NetState::ONLINE && WiFi.status() == WL_CONNECTED);
    String info = online ? WiFi.localIP().toString()
                         : (String("clock: ") + (s_ntpSynced ? "synced" : "on its own"));
    uiWifiInfo(online, WiFi.SSID(), info);
    return;
  }
  bool ready = clockIsSet();
  long daysLeft = 0;
  if (ready) {
    long secondsLeft = (long)(s_targetEpoch - time(nullptr));
    daysLeft = secondsLeft <= 0 ? 0 : (secondsLeft + 86399L) / 86400L;
  }
  uiHome(ready, daysLeft, s_dateShort, wifiIconState(), settings().message);
}

// ---------- arduino ----------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n[soon] v%s %s board=%s\n", FW_VERSION, SOON_BUILD, SOON_BOARD_ID);

  settingsBegin();
  uiBegin();

  pinMode(PIN_BUTTON_A, PIN_BUTTON_A == 35 ? INPUT : INPUT_PULLUP);
  if (PIN_BUTTON_B >= 0) pinMode(PIN_BUTTON_B, INPUT_PULLUP);

  portalBegin();       // routes only; server starts once the stack exists
  applyTimezone();     // sets TZ rules so the target date is right pre-sync
  seedClock();         // countdown works offline: flash-saved or build time
  recomputeTarget();

  // know when real internet time arrives (it silently corrects the seed)
  sntp_set_time_sync_notification_cb([](struct timeval *) {
    s_ntpSynced = true;
  });

  // Boot animation removed for now — straight to the bento page.
  // (To bring it back: call uiBoot(); right here.)
  drawCurrentPage();

  // One good blocking scan for the setup page's network list, done before
  // any AP/connection exists (most reliable moment), then start connecting.
  portalPreScan();
  WiFi.setSleep(false);        // CRITICAL for iPhone hotspots: with modem
                               // sleep on, the hotspot decides we vanished
                               // and drops us a minute after connecting
  WiFi.setAutoReconnect(true); // let the stack heal drops on its own
  portalStartServer();

  if (settings().ssid.length()) {
    netTry(settings().ssid, settings().pass, false, false);
  } else if (strlen(DEFAULT_WIFI_SSID) > 0) {
    netTry(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, /*pending=*/true, false);
  } else {
    netEnterApWait();
  }
}

void loop() {
  portalHandle();
  pollButton(s_btnA, btnAShort, btnALong);
  pollButton(s_btnB, btnBShort, nullptr);
  netTick();
  persistClockTick();

  // reflect NTP status to the portal page; save a good fix right away
  static bool ntpNoted = false;
  if (s_ntpSynced && !ntpNoted) {
    ntpNoted = true;
    portalState().ntpSynced = true;
    settingsSaveEpoch(time(nullptr));
    Serial.println("[time] internet time sync complete");
  }

  // periodic / forced OTA check (only when online)
  uint32_t now = millis();
  if (s_net == NetState::ONLINE && WiFi.status() == WL_CONNECTED) {
    bool due = (now - s_lastUpdateCheck > UPDATE_CHECK_INTERVAL_MS) ||
               (!s_firstCheckDone && now - s_lastUpdateCheck > 4000);
    if (due || s_forceUpdateCheck) runUpdateCheck();

    // pull the latest message from the repo every so often
    static uint32_t lastMsgFetch = 0;
    if ((lastMsgFetch == 0 && s_firstCheckDone) ||
        (lastMsgFetch != 0 && now - lastMsgFetch > MESSAGE_FETCH_INTERVAL_MS)) {
      lastMsgFetch = now;
      String m;
      if (updaterFetchMessage(m) && m != settings().message) {
        settingsSaveMessage(m);
        Serial.printf("[msg] new message synced: %s\n", m.c_str());
        s_lastDraw = 0;
      }
    }
  }

  // taps: the wifi box triggers a reconnect attempt IN PLACE (icon goes
  // yellow / "reconnecting..."). No screen change. A tap only closes the
  // pairing overlay if the side button opened it.
  int zone = uiTapZone();
  if (zone >= 0) {
    if (s_wifiInfoUntil > now) {
      s_wifiInfoUntil = 0;             // dismiss the (BOOT-opened) overlay
    } else if (zone == 1) {
      tryReconnect();
    }
    s_lastDraw = 0;
  }
  // overlay timed out? snap back to the bento page
  static bool wasOverlay = false;
  bool isOverlay = (s_wifiInfoUntil > now);
  if (wasOverlay && !isOverlay) s_lastDraw = 0;
  wasOverlay = isOverlay;

  // redraw ~1Hz (or immediately after any interaction)
  if (s_lastDraw == 0 || now - s_lastDraw > 1000) {
    s_lastDraw = now;
    drawCurrentPage();
  }
}
