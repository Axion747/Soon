#pragma once
#include <Arduino.h>

// ONE page, bento style (screen split hotdog-fold along the long side):
//   ┌────────────┬────────────┐
//   │  the date  │ wifi state │   top half, split again
//   ├────────────┴────────────┤
//   │    i love you sm <3     │   bottom half
//   └─────────────────────────┘
// Tapping the wifi box (or a short press of the side button) shows the
// pairing instructions for ~10 s, then the bento page returns.

enum class WifiIcon { RED, YELLOW, GREEN };  // offline / connecting / connected

void uiBegin();
void uiBoot();   // "wanga" fade-in + heart (blocking, a few seconds)

void uiHome(bool daysKnown, long daysLeft, const String &dateShort,
            WifiIcon wifi, const String &msg);
void uiWifiInfo(bool online, const String &ssid, const String &info);  // overlay

void uiUpdating(int percent, const String &fromV, const String &toV);
void uiMessage(const String &line1, const String &line2);
void uiForceRedraw();

// Completed tap since last call: -1 = none, 0 = date box, 1 = wifi box,
// 2 = message area. Always -1 on boards without touch.
int uiTapZone();

void uiBacklight(bool on);
bool uiBacklightIsOn();
