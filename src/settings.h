#pragma once
#include <Arduino.h>
#include <time.h>

struct Settings {
  String title;     // what we're counting down to
  String dateStr;   // target date, YYYY-MM-DD
  String tzKey;     // key into TZ_TABLE ("central", "eastern", ...)
  String message;   // the message page (Ben-settable, cached forever)
};

// ---- saved WiFi networks (up to 5, most-recently-used first) ----
#define MAX_WIFI_CREDS 5
struct WifiCred { String ssid; String pass; };
int settingsWifiCount();
WifiCred settingsWifiAt(int i);
void settingsAddWifi(const String &ssid, const String &pass);   // add / bump to front
void settingsRemoveWifi(const String &ssid);

Settings &settings();

void settingsBegin();                 // load everything from flash (NVS)
void settingsSaveCountdown(const String &title, const String &dateStr, const String &tzKey);
void settingsSaveMessage(const String &msg);
void settingsForgetWifi();            // wipe ALL saved WiFi networks

// Offline timekeeping: the device periodically remembers "now" in flash so a
// power cycle resumes from roughly the right date even with no WiFi ever.
time_t settingsLoadEpoch();
void settingsSaveEpoch(time_t t);

// Timezone helpers
const char *tzPosix(const String &key);       // key -> POSIX TZ string
const char *tzLabel(const String &key);       // key -> human label
size_t tzCount();
void tzAt(size_t i, const char **key, const char **label);
