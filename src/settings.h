#pragma once
#include <Arduino.h>
#include <time.h>

struct Settings {
  String ssid;      // saved WiFi network ("" = not configured yet)
  String pass;
  String title;     // what we're counting down to
  String dateStr;   // target date, YYYY-MM-DD
  String tzKey;     // key into TZ_TABLE ("central", "eastern", ...)
  String message;   // the message page (Ben-settable, cached forever)
};

Settings &settings();

void settingsBegin();                 // load everything from flash (NVS)
void settingsSaveWifi(const String &ssid, const String &pass);
void settingsSaveCountdown(const String &title, const String &dateStr, const String &tzKey);
void settingsSaveMessage(const String &msg);
void settingsForgetWifi();            // wipe WiFi creds only

// Offline timekeeping: the device periodically remembers "now" in flash so a
// power cycle resumes from roughly the right date even with no WiFi ever.
time_t settingsLoadEpoch();
void settingsSaveEpoch(time_t t);

// Timezone helpers
const char *tzPosix(const String &key);       // key -> POSIX TZ string
const char *tzLabel(const String &key);       // key -> human label
size_t tzCount();
void tzAt(size_t i, const char **key, const char **label);
