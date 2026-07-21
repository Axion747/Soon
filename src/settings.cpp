#include "settings.h"
#include "config.h"
#include <Preferences.h>

static Settings s_settings;
static Preferences s_prefs;

struct TzEntry {
  const char *key;
  const char *label;
  const char *posix;
};

// POSIX TZ strings (with US DST rules where applicable)
static const TzEntry TZ_TABLE[] = {
    {"eastern",  "US Eastern",           "EST5EDT,M3.2.0,M11.1.0"},
    {"central",  "US Central",           "CST6CDT,M3.2.0,M11.1.0"},
    {"mountain", "US Mountain",          "MST7MDT,M3.2.0,M11.1.0"},
    {"arizona",  "Arizona (no DST)",     "MST7"},
    {"pacific",  "US Pacific",           "PST8PDT,M3.2.0,M11.1.0"},
    {"alaska",   "Alaska",               "AKST9AKDT,M3.2.0,M11.1.0"},
    {"hawaii",   "Hawaii",               "HST10"},
};

static const TzEntry *tzFind(const String &key) {
  for (const auto &e : TZ_TABLE) {
    if (key == e.key) return &e;
  }
  return &TZ_TABLE[1];  // default: central
}

const char *tzPosix(const String &key) { return tzFind(key)->posix; }
const char *tzLabel(const String &key) { return tzFind(key)->label; }
size_t tzCount() { return sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]); }
void tzAt(size_t i, const char **key, const char **label) {
  *key = TZ_TABLE[i].key;
  *label = TZ_TABLE[i].label;
}

Settings &settings() { return s_settings; }

// ---- multi-network store ----
static WifiCred s_creds[MAX_WIFI_CREDS];
static int s_credCount = 0;

static void credsPersist() {
  s_prefs.putInt("wifin", s_credCount);
  for (int i = 0; i < s_credCount; i++) {
    s_prefs.putString((String("wssid") + i).c_str(), s_creds[i].ssid);
    s_prefs.putString((String("wpass") + i).c_str(), s_creds[i].pass);
  }
}

int settingsWifiCount() { return s_credCount; }
WifiCred settingsWifiAt(int i) {
  return (i >= 0 && i < s_credCount) ? s_creds[i] : WifiCred{"", ""};
}

void settingsAddWifi(const String &ssid, const String &pass) {
  if (!ssid.length()) return;
  // already known? bump to front (and refresh the password)
  for (int i = 0; i < s_credCount; i++) {
    if (s_creds[i].ssid == ssid) {
      WifiCred c{ssid, pass};
      for (int j = i; j > 0; j--) s_creds[j] = s_creds[j - 1];
      s_creds[0] = c;
      credsPersist();
      return;
    }
  }
  if (s_credCount < MAX_WIFI_CREDS) s_credCount++;
  for (int j = s_credCount - 1; j > 0; j--) s_creds[j] = s_creds[j - 1];
  s_creds[0] = {ssid, pass};
  credsPersist();
}

void settingsRemoveWifi(const String &ssid) {
  for (int i = 0; i < s_credCount; i++) {
    if (s_creds[i].ssid == ssid) {
      for (int j = i; j < s_credCount - 1; j++) s_creds[j] = s_creds[j + 1];
      s_credCount--;
      credsPersist();
      return;
    }
  }
}

void settingsBegin() {
  s_prefs.begin("soon", false);
  // isKey() first: getString() on a missing key spams scary-looking
  // NOT_FOUND errors to serial on the very first boot
  s_settings.title   = s_prefs.isKey("title") ? s_prefs.getString("title") : DEFAULT_TITLE;
  s_settings.dateStr = s_prefs.isKey("date")  ? s_prefs.getString("date")  : DEFAULT_DATE;
  s_settings.tzKey   = s_prefs.isKey("tz")    ? s_prefs.getString("tz")    : DEFAULT_TZ_KEY;
  s_settings.message = s_prefs.isKey("msg")   ? s_prefs.getString("msg")   : DEFAULT_MESSAGE;
  if (!s_settings.message.length()) s_settings.message = DEFAULT_MESSAGE;

  // load the network list
  s_credCount = s_prefs.isKey("wifin") ? s_prefs.getInt("wifin") : 0;
  if (s_credCount > MAX_WIFI_CREDS) s_credCount = MAX_WIFI_CREDS;
  for (int i = 0; i < s_credCount; i++) {
    s_creds[i].ssid = s_prefs.getString((String("wssid") + i).c_str(), "");
    s_creds[i].pass = s_prefs.getString((String("wpass") + i).c_str(), "");
  }
  // migrate the old single-network keys, if this device has them
  if (s_prefs.isKey("ssid")) {
    String oldSsid = s_prefs.getString("ssid", "");
    String oldPass = s_prefs.getString("pass", "");
    if (oldSsid.length()) settingsAddWifi(oldSsid, oldPass);
    s_prefs.remove("ssid");
    s_prefs.remove("pass");
  }
}

void settingsSaveMessage(const String &msg) {
  String m = msg;
  m.trim();
  if (!m.length() || m == s_settings.message) return;
  if (m.length() > 500) m = m.substring(0, 500);
  s_settings.message = m;
  s_prefs.putString("msg", m);
}


void settingsSaveCountdown(const String &title, const String &dateStr, const String &tzKey) {
  if (title.length()) s_settings.title = title;
  if (dateStr.length()) s_settings.dateStr = dateStr;
  if (tzKey.length()) s_settings.tzKey = tzKey;
  s_prefs.putString("title", s_settings.title);
  s_prefs.putString("date", s_settings.dateStr);
  s_prefs.putString("tz", s_settings.tzKey);
}

void settingsForgetWifi() {
  for (int i = 0; i < s_credCount; i++) {
    s_prefs.remove((String("wssid") + i).c_str());
    s_prefs.remove((String("wpass") + i).c_str());
  }
  s_credCount = 0;
  s_prefs.putInt("wifin", 0);
}

time_t settingsLoadEpoch() { return (time_t)s_prefs.getULong64("epoch", 0); }
void settingsSaveEpoch(time_t t) { s_prefs.putULong64("epoch", (uint64_t)t); }
