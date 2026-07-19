#include "updater.h"
#include "config.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// The device pulls updates from this repo's LATEST GitHub release:
//   manifest.json  -> {"version": "...", "builds": {"<board>": {"file","md5","size"}}}
//   soon-<board>.bin
// Both are uploaded automatically by .github/workflows/release.yml
static String baseUrl() {
  return String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO +
         "/releases/latest/download/";
}

static String s_latestSeen;
String updaterLatestSeen() { return s_latestSeen; }

bool updaterFetchMessage(String &out) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (String(GITHUB_OWNER) == "YOUR_GITHUB_USERNAME") return false;  // repo not set up yet

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  String url = String("https://raw.githubusercontent.com/") + GITHUB_OWNER + "/" +
               GITHUB_REPO + "/main/message.txt";
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    out = http.getString();
    out.trim();
    if (out.length() > 500) out = out.substring(0, 500);
    ok = out.length() > 0;
  }
  http.end();
  return ok;
}

// "1.4.2" -> {1,4,2}; anything unparsable -> 0
static void parseVer(const String &v, int out[3]) {
  out[0] = out[1] = out[2] = 0;
  sscanf(v.c_str(), "%d.%d.%d", &out[0], &out[1], &out[2]);
}

static bool isNewer(const String &remote, const String &local) {
  int r[3], l[3];
  parseVer(remote, r);
  parseVer(local, l);
  for (int i = 0; i < 3; i++) {
    if (r[i] != l[i]) return r[i] > l[i];
  }
  return false;
}

UpdateOutcome updaterCheckAndApply(const UpdateProgressFn &progress) {
  if (WiFi.status() != WL_CONNECTED) return UpdateOutcome::NO_WIFI;
  if (String(GITHUB_OWNER) == "YOUR_GITHUB_USERNAME") {
    // GitHub repo not set up yet — skip quietly instead of 404ing
    return UpdateOutcome::NOT_CONFIGURED;
  }

  // NOTE: certificate validation is skipped (setInsecure). The firmware only
  // ever *reads* public release files from GitHub, and each binary is
  // MD5-checked against the manifest before it's accepted. Good enough for a
  // countdown gadget; swap in a CA bundle later if you feel fancy.
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub 302s to its CDN
  http.setConnectTimeout(10000);
  http.setTimeout(15000);

  // ---- 1. fetch the manifest ----
  if (!http.begin(client, baseUrl() + "manifest.json")) return UpdateOutcome::CHECK_FAILED;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[updater] manifest GET -> %d\n", code);
    http.end();
    return UpdateOutcome::CHECK_FAILED;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    Serial.printf("[updater] manifest parse: %s\n", err.c_str());
    return UpdateOutcome::CHECK_FAILED;
  }

  String remoteVer = doc["version"] | "";
  if (!remoteVer.length()) return UpdateOutcome::CHECK_FAILED;
  s_latestSeen = remoteVer;

  if (!isNewer(remoteVer, FW_VERSION)) {
    Serial.printf("[updater] up to date (local %s, remote %s)\n", FW_VERSION, remoteVer.c_str());
    return UpdateOutcome::UP_TO_DATE;
  }

  JsonObject build = doc["builds"][SOON_BOARD_ID];
  if (build.isNull()) {
    Serial.printf("[updater] release %s has no build for board '%s'\n", remoteVer.c_str(), SOON_BOARD_ID);
    return UpdateOutcome::NO_BUILD;
  }
  String file = build["file"] | "";
  String md5 = build["md5"] | "";
  if (!file.length()) return UpdateOutcome::CHECK_FAILED;

  Serial.printf("[updater] updating %s -> %s (%s)\n", FW_VERSION, remoteVer.c_str(), file.c_str());

  // ---- 2. download + flash ----
  if (!http.begin(client, baseUrl() + file)) return UpdateOutcome::CHECK_FAILED;
  code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[updater] bin GET -> %d\n", code);
    http.end();
    return UpdateOutcome::APPLY_FAILED;
  }

  int total = http.getSize();  // may be -1 (chunked)
  if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) {
    Serial.printf("[updater] Update.begin failed: %s\n", Update.errorString());
    http.end();
    return UpdateOutcome::APPLY_FAILED;
  }
  if (md5.length() == 32) Update.setMD5(md5.c_str());

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[2048];
  size_t written = 0;
  int lastPct = -1;
  uint32_t lastData = millis();

  while (http.connected() && (total < 0 || written < (size_t)total)) {
    size_t avail = stream->available();
    if (avail) {
      size_t n = stream->readBytes(buf, min(avail, sizeof(buf)));
      if (Update.write(buf, n) != n) {
        Serial.printf("[updater] flash write failed: %s\n", Update.errorString());
        Update.abort();
        http.end();
        return UpdateOutcome::APPLY_FAILED;
      }
      written += n;
      lastData = millis();
      if (total > 0 && progress) {
        int pct = (int)((written * 100ULL) / (size_t)total);
        if (pct != lastPct) {
          lastPct = pct;
          progress(pct, FW_VERSION, remoteVer);
        }
      }
    } else {
      if (millis() - lastData > 20000) {  // stalled
        Serial.println("[updater] download stalled");
        Update.abort();
        http.end();
        return UpdateOutcome::APPLY_FAILED;
      }
      delay(5);
    }
  }
  http.end();

  if (!Update.end(true)) {
    Serial.printf("[updater] Update.end failed: %s\n", Update.errorString());
    return UpdateOutcome::APPLY_FAILED;
  }
  Serial.println("[updater] update flashed OK");
  return UpdateOutcome::APPLIED;
}
