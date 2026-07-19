#pragma once

// ============================================================
//  EDIT THIS ONE SECTION after you create your GitHub repo.
//  The device checks this repo's latest release for updates.
//  The repo must be PUBLIC (release downloads need no login).
// ============================================================
#define GITHUB_OWNER "Axion747"   // github username (no underscore!)
#define GITHUB_REPO  "Soon"       // repo NAME only — never the full URL
// ============================================================

// Branding / identity
#define PRODUCT_NAME   "Soon"
#define SETUP_AP_SSID  "Soon-Setup"   // WiFi network the device creates during setup
#define MDNS_NAME      "soon"         // once online, settings live at http://soon.local

// Default WiFi — her iPhone hotspot. Tried automatically whenever no other
// network has been saved, and retried in the background from the setup
// screen. The SSID must match her phone's name EXACTLY (check on her phone:
// Settings > General > About > Name — Apple's default is like "Ana's iPhone").
// A network she saves through the setup page always takes priority over this.
#define DEFAULT_WIFI_SSID "Eyephone"
#define DEFAULT_WIFI_PASS "xis23s1qo3rr2"

// Countdown defaults (all changeable later from the settings page — these
// are just what the device shows before anyone configures it)
#define DEFAULT_TITLE    "Something wonderful"
#define DEFAULT_DATE     "2026-09-20"                 // YYYY-MM-DD

// Boot message (fades in at power-on, heart beneath)
#define BOOT_LINE_1      "wanga"

// Page 2: the message page. Ben can change this any time from the settings
// page (soon.local or the Soon-Setup portal), and — once the GitHub repo
// exists — by editing message.txt in the repo: whenever the device has WiFi
// it pulls the latest message every 30 minutes and remembers it forever.
#define DEFAULT_MESSAGE  "i love you sm <3"   // a trailing <3 becomes a drawn heart
#define MESSAGE_FETCH_INTERVAL_MS (30UL * 60UL * 1000UL)
#define DEFAULT_TZ_KEY   "central"                    // see TZ_TABLE in settings.cpp

// How often to look for a new firmware release (also checked at boot,
// and on demand via the button / settings page)
#define UPDATE_CHECK_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL)   // 6 hours

// Version string is injected by scripts/version.py at build time
#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif

// Hand-bumped build tag, shown small on the WiFi page and printed to serial —
// so you can always tell WHICH build is actually running on the device.
#define SOON_BUILD "b25"

#ifndef SOON_BOARD_ID
#define SOON_BOARD_ID "unknown"
#endif
