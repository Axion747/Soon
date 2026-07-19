#pragma once
#include <Arduino.h>
#include <functional>

enum class UpdateOutcome {
  NO_WIFI,        // not connected, didn't try
  NOT_CONFIGURED, // GITHUB_OWNER still the placeholder — no repo to check
  CHECK_FAILED,   // couldn't reach or parse the release manifest
  UP_TO_DATE,     // remote version <= ours
  NO_BUILD,       // release has no binary for this board
  APPLY_FAILED,   // download/flash error
  APPLIED         // success — caller should reboot
};

// progress(percent, fromVersion, toVersion) fires during download/flash
using UpdateProgressFn = std::function<void(int, const String &, const String &)>;

UpdateOutcome updaterCheckAndApply(const UpdateProgressFn &progress);

// Version the last successful check discovered (for the settings page)
String updaterLatestSeen();

// Pull the latest message from the repo (message.txt on the main branch).
// Returns true when a non-empty message landed in `out`. Quietly false when
// offline, the repo isn't configured yet, or the file doesn't exist.
bool updaterFetchMessage(String &out);
