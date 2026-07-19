#include "ui.h"
#include "config.h"
#include <TFT_eSPI.h>

#ifdef SOON_AMOLED
// LilyGO's official driver (same code their examples use); the 1.91" board
// is initialized EXPLICITLY — no auto-detect guessing.
#include <LilyGo_AMOLED.h>
static LilyGo_AMOLED amoled;
static bool s_beginOk = false;
#endif

#ifdef SOON_LCD_TOUCH
// T-Display-S3 Touch variant: CST816 over I2C (SensorLib driver — the same
// one LilyGO's AMOLED library uses internally). Probed at boot; quietly
// absent on non-touch units, in which case the buttons cover everything.
#include <Wire.h>
#include "TouchDrvCSTXXX.hpp"
static TouchDrvCSTXXX s_touch;
static bool s_touchOk = false;
#endif

// ---- rendering backend -----------------------------------------------
// LilyGO TFT_eSPI_Sprite pattern: compose every frame in a full-screen
// sprite, push it to the panel as one atomic frame. LCD boards draw direct.
static TFT_eSPI tft;
#ifdef SOON_AMOLED
static TFT_eSprite spr(&tft);
static TFT_eSPI *g = &tft;              // repointed to &spr in uiBegin
#else
static TFT_eSPI *g = &tft;
#endif

static void flush() {
#ifdef SOON_AMOLED
  // Push the sprite buffer RAW. This panel latches pixels in native byte
  // order — the extra byte-swap we used to do here inverted every color
  // (the dark card surface came out hot pink).
  uint16_t *cur = (uint16_t *)spr.getPointer();
  if (!cur) return;
  amoled.pushColors(0, 0, spr.width(), spr.height(), cur);
#endif
}

// ---- palette -------------------------------------------------
static uint16_t C_BG, C_CARD, C_LINE, C_ACCENT, C_TEXT, C_DIM, C_MINT;
static uint16_t C_RED, C_YEL, C_GRN;

enum class Screen { NONE, BOOT, HOME, WIFIINFO, UPDATING, NOTICE };
static Screen s_screen = Screen::NONE;
static bool s_backlightOn = true;

static int W() { return g->width(); }
static int H() { return g->height(); }
static bool bigPanel() { return W() >= 300; }

static void clearTo(Screen sc) {
  if (s_screen == sc) return;
  g->fillScreen(C_BG);
  s_screen = sc;
}

// AMOLED: wipe + redraw the whole frame every draw (atomic push — flicker
// free, leftovers impossible). LCDs: clear only on screen change.
static bool beginFrame(Screen sc) {
  static Screen shown = Screen::NONE;
  bool changed = (shown != sc);
  shown = sc;
#ifdef SOON_AMOLED
  s_screen = Screen::NONE;
#endif
  clearTo(sc);
  return changed;
}

void uiForceRedraw() { s_screen = Screen::NONE; }

void uiBacklight(bool on) {
#if defined(SOON_AMOLED)
  amoled.setBrightness(on ? 230 : 0);
#elif defined(TFT_BL)
  digitalWrite(TFT_BL, on ? HIGH : LOW);
#endif
  s_backlightOn = on;
}
bool uiBacklightIsOn() { return s_backlightOn; }

void uiBegin() {
#ifdef SOON_AMOLED
  // Explicit init for the T-Display-S3 AMOLED 1.91". touch=true just probes
  // for the touch chip — the non-touch version works fine too.
  s_beginOk = amoled.beginAMOLED_191(true);
  Serial.printf("[ui] beginAMOLED_191: %s  panel=%ux%u  touch=%s\n",
                s_beginOk ? "OK" : "FAILED", amoled.width(), amoled.height(),
                amoled.hasTouch() ? "yes" : "no");
  spr.createSprite(amoled.width(), amoled.height());          // PSRAM
  if (!spr.getPointer()) {
    Serial.println("[ui] FATAL: framebuffer alloc failed (PSRAM?)");
  }
  g = &spr;
  amoled.setBrightness(230);
#else
#if defined(PIN_LCD_POWER) && (PIN_LCD_POWER >= 0)
  pinMode(PIN_LCD_POWER, OUTPUT);
  digitalWrite(PIN_LCD_POWER, HIGH);
#endif
  tft.init();
  tft.setRotation(1);
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif
#ifdef SOON_LCD_TOUCH
  // The CST816 sits in reset until its RST pin goes HIGH — probing the I2C
  // bus before releasing reset always reads "not found". So: pulse reset,
  // give the chip its boot time, THEN probe.
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(30);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(120);
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.beginTransmission(0x15);  // CST816 I2C address
  if (Wire.endTransmission() == 0) {
    s_touch.setTouchDrvModel(TouchDrv_CST8XX);
    s_touch.setPins(PIN_TOUCH_RST, PIN_TOUCH_INT);
    s_touch.begin(Wire, 0x15, PIN_TOUCH_SDA, PIN_TOUCH_SCL);
    s_touchOk = true;  // the ACK above is the reliable signal, not begin()
    // map native portrait coordinates to our landscape rotation
    s_touch.setMaxCoordinates(tft.width(), tft.height());
    s_touch.setSwapXY(true);
    s_touch.setMirrorXY(true, false);
  }
  Serial.printf("[ui] touch: %s\n", s_touchOk ? "CST816 OK" : "not found");
#endif
#endif

  // Professional dark palette built around the mint brand color #a9ed9d:
  // mint for accents & key data, near-white body text with a faint green
  // cast, muted sage for secondary text, semantic-but-harmonized states.
  C_BG     = TFT_BLACK;
  C_CARD   = g->color565(21, 24, 22);     // card surface
  C_LINE   = g->color565(44, 52, 46);     // card outline
  C_ACCENT = g->color565(169, 237, 157);  // #a9ed9d — THE color
  C_MINT   = C_ACCENT;
  C_TEXT   = g->color565(232, 238, 234);  // primary text
  C_DIM    = g->color565(138, 148, 142);  // secondary text
  C_RED    = g->color565(232, 106, 100);  // offline
  C_YEL    = g->color565(235, 200, 100);  // connecting
  C_GRN    = C_ACCENT;                    // connected = the brand mint
  g->fillScreen(C_BG);
  flush();
}

// ---- bento layout --------------------------------------------
struct Rect { int x, y, w, h; };

static void homeLayout(Rect &tl, Rect &tr, Rect &bot) {
  int m = bigPanel() ? 8 : 4;                 // outer margin & gutter
  int rowH = (H() - 3 * m) / 2;
  int colW = (W() - 3 * m) / 2;
  tl = {m, m, colW, rowH};
  tr = {m + colW + m, m, colW, rowH};
  bot = {m, m + rowH + m, W() - 2 * m, rowH};
}

static void drawCard(const Rect &r) {
  g->fillRoundRect(r.x, r.y, r.w, r.h, 10, C_CARD);
  g->drawRoundRect(r.x, r.y, r.w, r.h, 10, C_LINE);
}

// ---- helpers -------------------------------------------------
static void centerTextIn(const Rect &r, const String &txt, int yOffset, int font,
                         uint16_t color) {
  g->setTextDatum(MC_DATUM);
  g->setTextColor(color, C_CARD);
  g->drawString(txt, r.x + r.w / 2, r.y + r.h / 2 + yOffset, font);
}

static void centerText(const String &txt, int y, int font, uint16_t color) {
  g->setTextDatum(MC_DATUM);
  g->setTextColor(color, C_BG);
  g->setTextPadding(W());
  g->drawString(txt, W() / 2, y, font);
  g->setTextPadding(0);
}

static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  return (uint16_t)(((ar + (int)((br - ar) * t)) << 11) |
                    ((ag + (int)((bg - ag) * t)) << 5) |
                    (ab + (int)((bb - ab) * t)));
}

static void drawHeart(int cx, int cy, int r, uint16_t color) {
  // lobes overlap in the middle so it reads as ONE heart, not two blobs
  int o = (2 * r) / 3;
  g->fillCircle(cx - o, cy, r, color);
  g->fillCircle(cx + o, cy, r, color);
  g->fillTriangle(cx - o - r, cy + r / 3, cx + o + r, cy + r / 3,
                  cx, cy + o + r + 1, color);
}

// Big smooth text (auto-shrinks); used by boot + overlay screens
static void bigText(const String &txt, int y, uint16_t color, bool primary) {
  g->setTextDatum(MC_DATUM);
  g->setTextColor(color);
  if (bigPanel()) {
    g->setFreeFont(primary ? &FreeSansBold24pt7b : &FreeSansBold18pt7b);
    if (g->textWidth(txt) > W() - 10) g->setFreeFont(&FreeSansBold18pt7b);
    if (g->textWidth(txt) > W() - 10) g->setFreeFont(&FreeSansBold12pt7b);
    if (g->textWidth(txt) <= W() - 10) {
      g->drawString(txt, W() / 2, y);
      g->setTextFont(4);
      return;
    }
    g->setTextFont(4);
  }
  int font = (g->textWidth(txt, 4) <= W() - 8) ? 4 : 2;
  g->drawString(txt, W() / 2, y, font);
}

// ---- pixel-art digits (5x7 chunky blocks) --------------------
static const uint8_t DIGIT5x7[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},  // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  // 9
};
static const uint8_t DASH5x7[7] = {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00};

// Draw `numStr` in pixel digits centered at (cx, cy), constrained to maxW/maxH
static void drawPixelNumber(const String &numStr, int cx, int cy, int maxW,
                            int maxH, uint16_t color) {
  int n = numStr.length();
  if (n == 0) return;
  int cell = min(maxH / 7, maxW / (6 * n - 1));
  if (cell < 2) cell = 2;
  int gapPx = max(1, cell / 8);
  int block = cell - gapPx;
  int totalW = (6 * n - 1) * cell;
  int x0 = cx - totalW / 2;
  int y0 = cy - (7 * cell) / 2;
  for (int i = 0; i < n; i++) {
    char c = numStr[i];
    const uint8_t *gl = (c >= '0' && c <= '9') ? DIGIT5x7[c - '0']
                        : (c == '-')           ? DASH5x7
                                               : nullptr;
    if (!gl) continue;
    int gx = x0 + i * 6 * cell;
    for (int r = 0; r < 7; r++) {
      for (int col = 0; col < 5; col++) {
        if (gl[r] & (1 << (4 - col))) {
          g->fillRect(gx + col * cell, y0 + r * cell, block, block, color);
        }
      }
    }
  }
}

// ---- wifi fan icon (dot + two arcs) --------------------------
// Drawn ADDITIVELY: the arcs are laid down as overlapping dots along the
// arc path. No background masking at all, so nothing can ever leak outside
// the card (the old carve-out approach left stray pixels on short cards).
static void drawWifiIcon(int cx, int cy, int size, uint16_t color) {
  int th = max(3, size / 5);
  int rad = th / 2 + 1;
  for (int ring = 0; ring < 2; ring++) {
    int r = (ring == 0) ? size : (size * 2) / 3;
    for (int a = -45; a <= 45; a += 5) {   // fan opens upward, ±45°
      float t = a * 0.0174533f;
      g->fillCircle(cx + (int)(r * sinf(t)), cy - (int)(r * cosf(t)), rad, color);
    }
  }
  g->fillCircle(cx, cy, max(3, th - 1), color);  // the dot
}

// ---- boot ----------------------------------------------------
void uiBoot() {
  beginFrame(Screen::BOOT);
  const String L1 = BOOT_LINE_1;
  int y1 = H() / 2 - (bigPanel() ? 18 : 12);
  int heartY = H() / 2 + (bigPanel() ? 46 : 28);
  int heartR = bigPanel() ? 8 : 5;

#ifdef SOON_AMOLED
  const int FRAMES = 84;
  for (int f = 0; f < FRAMES; f++) {
    g->fillScreen(C_BG);
    float t1 = constrain((f - 6) / 36.0f, 0.0f, 1.0f);
    float th = constrain((f - 40) / 28.0f, 0.0f, 1.0f);
    if (t1 > 0) bigText(L1, y1, lerp565(C_BG, C_TEXT, t1), true);
    if (th > 0) drawHeart(W() / 2, heartY, heartR, lerp565(C_BG, C_ACCENT, th));
    flush();
    delay(24);
  }
  const int beat[] = {1, 2, 1, 0};
  for (int b = 0; b < 2; b++) {
    for (int d : beat) {
      g->fillScreen(C_BG);
      bigText(L1, y1, C_TEXT, true);
      drawHeart(W() / 2, heartY, heartR + d, C_ACCENT);
      flush();
      delay(70);
    }
    delay(260);
  }
  delay(400);
#else
  const int STEPS = 16;
  for (int i = 1; i <= STEPS; i++) {
    bigText(L1, y1, lerp565(C_BG, C_TEXT, i / (float)STEPS), true);
    delay(70);
  }
  drawHeart(W() / 2, heartY, heartR, C_ACCENT);
  delay(1600);
#endif
}

// ---- THE page (bento) ----------------------------------------
void uiHome(bool daysKnown, long daysLeft, const String &dateShort,
            WifiIcon wifi, const String &msg) {
#ifndef SOON_AMOLED
  // LCDs draw direct to the panel (no frame buffer): repaint the WHOLE page
  // only when something actually changed, skip entirely otherwise. That's
  // both flicker-free and ghost-free (a shorter label fully erases a longer
  // one because the repaint starts from a cleared screen).
  {
    static String lastSig;
    String sig = String(daysKnown ? daysLeft : -1) + "|" + dateShort + "|" +
                 String((int)wifi) + "|" + msg;
    if (sig == lastSig && s_screen == Screen::HOME) return;
    lastSig = sig;
    s_screen = Screen::NONE;  // force a full clear + redraw
  }
#endif
  beginFrame(Screen::HOME);
  Rect tl, tr, bot;
  homeLayout(tl, tr, bot);
  drawCard(tl);
  drawCard(tr);
  drawCard(bot);

  // ---- top-left: just the number, as big as the card allows ----
  if (daysKnown && daysLeft <= 0) {
    drawHeart(tl.x + tl.w / 2, tl.y + tl.h / 2 - (bigPanel() ? 14 : 8),
              bigPanel() ? 10 : 6, C_MINT);
    centerTextIn(tl, "it's today!", tl.h / 2 - (bigPanel() ? 22 : 14),
                 bigPanel() ? 4 : 2, C_MINT);
  } else {
    String num = daysKnown ? String(daysLeft) : "--";
    drawPixelNumber(num, tl.x + tl.w / 2, tl.y + tl.h / 2,
                    tl.w - (bigPanel() ? 28 : 12), tl.h - (bigPanel() ? 20 : 8),
                    C_MINT);
  }
  (void)dateShort;  // date caption removed by request

  // ---- top-right: wifi state ----
  int capH = bigPanel() ? 22 : 14;
  uint16_t wcol = (wifi == WifiIcon::GREEN) ? C_GRN
                  : (wifi == WifiIcon::YELLOW) ? C_YEL
                                               : C_RED;
  const char *wlabel = (wifi == WifiIcon::GREEN) ? "connected"
                       : (wifi == WifiIcon::YELLOW) ? "reconnecting"
                                                    : "tap to reconnect";
  int iconSize = bigPanel() ? 30 : 18;
  drawWifiIcon(tr.x + tr.w / 2, tr.y + (tr.h - capH) / 2 + iconSize / 2 + 2,
               iconSize, wcol);
  centerTextIn(tr, wlabel, tr.h / 2 - (bigPanel() ? 14 : 8), 2, wcol);

  // ---- bottom: the message ----
  String m = msg;
  m.trim();
  bool heart = false;
  if (m.endsWith("<3")) {  // a trailing <3 becomes a real heart
    heart = true;
    m = m.substring(0, m.length() - 2);
    m.trim();
  }
  int cy = bot.y + bot.h / 2;
  int hr = bigPanel() ? 7 : 5;
  int hSpace = heart ? (4 * hr + 12) : 0;

  g->setTextDatum(MC_DATUM);
  g->setTextColor(C_TEXT);   // body text near-white; the heart carries the mint
  int tw = 0;
  if (bigPanel()) {
    g->setFreeFont(&FreeSansBold18pt7b);
    if (g->textWidth(m) + hSpace > bot.w - 20) g->setFreeFont(&FreeSansBold12pt7b);
    if (g->textWidth(m) + hSpace <= bot.w - 20) {
      tw = g->textWidth(m);
      g->drawString(m, bot.x + (bot.w - hSpace) / 2, cy);
      g->setTextFont(4);
    } else {
      g->setTextFont(4);
      tw = g->textWidth(m, 2);
      g->drawString(m, bot.x + (bot.w - hSpace) / 2, cy, 2);
    }
  } else {
    int font = (g->textWidth(m, 4) + hSpace <= bot.w - 10) ? 4 : 2;
    tw = g->textWidth(m, font);
    g->drawString(m, bot.x + (bot.w - hSpace) / 2, cy, font);
  }
  if (heart) {
    int hx = bot.x + (bot.w - hSpace) / 2 + tw / 2 + 2 * hr + 8;
    drawHeart(hx, cy - hr / 2, hr, C_ACCENT);
  }

  flush();
}

// ---- wifi info overlay ---------------------------------------
void uiWifiInfo(bool online, const String &ssid, const String &info) {
#ifndef SOON_AMOLED
  {
    static String lastSig;
    String sig = String(online) + "|" + ssid + "|" + info;
    if (sig == lastSig && s_screen == Screen::WIFIINFO) return;
    lastSig = sig;
    s_screen = Screen::NONE;  // full clear + redraw on change (see uiHome)
  }
#endif
  beginFrame(Screen::WIFIINFO);
  int lh = bigPanel() ? 32 : 19;
  int bf = bigPanel() ? 4 : 2;
  int y = bigPanel() ? 24 : 10;

  if (online) {
    centerText("WiFi: connected", y, bf, C_GRN); y += lh + 2;
    centerText(ssid, y, bf, C_TEXT); y += lh;
    centerText("settings: http://" MDNS_NAME ".local", y, bf, C_DIM); y += lh;
    centerText(info, y, 2, C_DIM);
  } else {
    centerText("WiFi pairing", y, bf, C_ACCENT); y += lh + 2;
    centerText("On a phone, join the WiFi:", y, bf, C_TEXT); y += lh;
    centerText(SETUP_AP_SSID, y, bf, C_GRN); y += lh;
    centerText("then the setup page pops up", y, bf, C_TEXT); y += lh;
    centerText("(or go to 192.168.4.1)  ·  " + info, y, 2, C_DIM);
  }
#if defined(SOON_AMOLED)
  bool canTap = amoled.hasTouch();
#elif defined(SOON_LCD_TOUCH)
  bool canTap = s_touchOk;
#else
  bool canTap = false;
#endif
  centerText(canTap ? "tap anywhere to close" : "closes automatically",
             H() - (bigPanel() ? 30 : 20), 2, C_DIM);

  // diagnostics corner
  String tag = String("v") + FW_VERSION + " " + SOON_BUILD;
#if defined(SOON_AMOLED)
  tag = String(amoled.width()) + "x" + String(amoled.height()) +
        (amoled.hasTouch() ? " touch" : " btn") + (s_beginOk ? "" : " INIT-FAIL") +
        " · " + tag;
#elif defined(SOON_LCD_TOUCH)
  tag = String(W()) + "x" + String(H()) + (s_touchOk ? " touch" : " btn") + " · " + tag;
#endif
  g->setTextDatum(BR_DATUM);
  g->setTextColor(C_DIM, C_BG);
  g->drawString(tag, W() - 6, H() - 4, 2);
  flush();
}

// ---- touch: tap detection ------------------------------------
static uint8_t touchRead(int16_t *x, int16_t *y) {
#if defined(SOON_AMOLED)
  return amoled.getPoint(x, y, 1);  // 0 on non-touch boards
#elif defined(SOON_LCD_TOUCH)
  return s_touchOk ? s_touch.getPoint(x, y, 1) : 0;
#else
  (void)x; (void)y;
  return 0;
#endif
}

int uiTapZone() {
#if defined(SOON_AMOLED) || defined(SOON_LCD_TOUCH)
  static uint32_t lastPoll = 0;
  static bool tracking = false;
  static int16_t sx = 0, sy = 0, lx = 0, ly = 0;
  static uint32_t tDown = 0;
  uint32_t now = millis();
  if (now - lastPoll < 30) return -1;
  lastPoll = now;

  int16_t x = 0, y = 0;
  uint8_t n = touchRead(&x, &y);
  if (n > 0) {
    if (!tracking) { tracking = true; sx = x; sy = y; tDown = now; }
    lx = x; ly = y;
    return -1;
  }
  if (tracking) {
    tracking = false;
    if (now - tDown < 800 && abs(lx - sx) < 30 && abs(ly - sy) < 30) {
      Rect tl, tr, bot;
      homeLayout(tl, tr, bot);
      int z = 2;
      if (sx >= tr.x && sy < tr.y + tr.h) z = 1;        // wifi box
      else if (sx < tl.x + tl.w && sy < tl.y + tl.h) z = 0;
      Serial.printf("[touch] tap %d,%d -> zone %d\n", sx, sy, z);
      return z;
    }
  }
#endif
  return -1;
}

// ---- OTA / notices -------------------------------------------
void uiUpdating(int percent, const String &fromV, const String &toV) {
  beginFrame(Screen::UPDATING);
  centerText("Update time!", bigPanel() ? 30 : 20, 4, C_ACCENT);
  centerText("v" + fromV + "  ->  v" + toV, bigPanel() ? 64 : 48, bigPanel() ? 4 : 2, C_TEXT);
  centerText("keep me plugged in", H() - (bigPanel() ? 20 : 14), bigPanel() ? 4 : 2, C_DIM);

  int barW = W() - 60, barH = bigPanel() ? 20 : 14;
  int x = 30, y = H() / 2 + 2;
  g->drawRoundRect(x, y, barW, barH, 4, C_DIM);
  percent = constrain(percent, 0, 100);
  int fill = (barW - 4) * percent / 100;
  if (fill > 0) g->fillRoundRect(x + 2, y + 2, fill, barH - 4, 3, C_ACCENT);
  flush();
}

void uiMessage(const String &line1, const String &line2) {
  beginFrame(Screen::NOTICE);
  centerText(line1, H() / 2 - (bigPanel() ? 18 : 12), 4, C_TEXT);
  centerText(line2, H() / 2 + (bigPanel() ? 24 : 16), bigPanel() ? 4 : 2, C_DIM);
  flush();
}
