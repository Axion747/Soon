#pragma once
#include <Arduino.h>

// Connection-attempt state (shown live on the setup page while it polls)
enum class ConnState { IDLE, CONNECTING, OK, FAIL };
// Update-check state (shown on the settings page)
enum class UpdState { IDLE, CHECKING, NONE, FOUND, UPDATING, ERR };

struct PortalState {
  bool apMode = true;
  ConnState conn = ConnState::IDLE;
  UpdState upd = UpdState::IDLE;
  String updateVer;
  bool ntpSynced = false;  // clock corrected by internet time at least once
};

PortalState &portalState();

void portalBegin();       // register routes ONLY (safe before WiFi exists)
void portalStartServer(); // start listening — call only AFTER WiFi.mode()
void portalPreScan();     // blocking WiFi scan BEFORE the AP starts (reliable),
                          // results are cached for the setup page
void portalStartAP();     // open the Soon-Setup network + captive DNS
void portalStopAP();    // tear down AP once we're happily online
void portalStartMDNS(); // http://soon.local when on the home network
void portalHandle();    // call every loop()
uint32_t portalMsSinceActivity();  // ms since the setup page last talked to us

// Implemented in main.cpp — the portal calls these when the user acts
void appOnWifiSubmit(const String &ssid, const String &pass);
void appOnSettingsSaved();
void appOnCheckUpdate();
void appOnForget();
void appOnTimeSet(time_t epoch);  // "set clock from this phone" button
