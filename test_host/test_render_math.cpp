// Host-side test for the pure-math parts of ui.cpp (b11 bento layout):
// panel geometry, pixel-digit fit inside the date box, heart-suffix parsing.
// Build & run:  g++ -std=c++11 -o t test_render_math.cpp && ./t
#include <cassert>
#include <cstdio>
#include <string>
#include <algorithm>

struct Rect { int x, y, w, h; };

static void homeLayout(int W, int H, bool big, Rect &tl, Rect &tr, Rect &bot) {
  int m = big ? 8 : 4;
  int rowH = (H - 3 * m) / 2;
  int colW = (W - 3 * m) / 2;
  tl = {m, m, colW, rowH};
  tr = {m + colW + m, m, colW, rowH};
  bot = {m, m + rowH + m, W - 2 * m, rowH};
}

static void testLayout(int W, int H, bool big) {
  Rect tl, tr, bot;
  homeLayout(W, H, big, tl, tr, bot);
  int m = big ? 8 : 4;
  assert(tl.x == m && tl.y == m);
  assert(tr.x == tl.x + tl.w + m);                 // gutter between columns
  assert(tr.x + tr.w <= W - m + 1);                // right edge inside margin
  assert(bot.y == tl.y + tl.h + m);                // gutter between rows
  assert(bot.y + bot.h <= H - m + 1);              // bottom edge inside margin
  assert(tl.w == tr.w && tl.h == tr.h && tl.h == bot.h);
  printf("  layout %dx%d: boxes %dx%d / bottom %dx%d — clean bento\n",
         W, H, tl.w, tl.h, bot.w, bot.h);
}

static void testDigitsFitBox(int W, int H, bool big, const std::string &num) {
  Rect tl, tr, bot;
  homeLayout(W, H, big, tl, tr, bot);
  int capH = big ? 22 : 14;
  int maxW = tl.w - (big ? 28 : 12);
  int maxH = tl.h - capH - (big ? 20 : 8);
  int n = (int)num.size();
  int cell = std::min(maxH / 7, maxW / (6 * n - 1));
  if (cell < 2) cell = 2;
  int totalW = (6 * n - 1) * cell;
  int cx = tl.x + tl.w / 2, cy = tl.y + (tl.h - capH) / 2;
  int x0 = cx - totalW / 2, y0 = cy - (7 * cell) / 2;
  // every block stays inside the top-left card
  assert(x0 >= tl.x);
  assert(x0 + totalW <= tl.x + tl.w);
  assert(y0 >= tl.y);
  assert(y0 + 7 * cell <= tl.y + tl.h - capH + 8);
  printf("  digits '%s' in %dx%d date box: cell=%d (%dx%d px) — fits\n",
         num.c_str(), tl.w, tl.h, cell, 5 * cell, 7 * cell);
}

static void testHeartSuffix() {
  auto parse = [](std::string m, bool &heart) {
    while (!m.empty() && m.back() == ' ') m.pop_back();
    heart = false;
    if (m.size() >= 2 && m.substr(m.size() - 2) == "<3") {
      heart = true;
      m = m.substr(0, m.size() - 2);
      while (!m.empty() && m.back() == ' ') m.pop_back();
    }
    return m;
  };
  bool h;
  assert(parse("i love you sm <3", h) == "i love you sm" && h);
  assert(parse("hi there", h) == "hi there" && !h);
  assert(parse("<3", h) == "" && h);           // heart only — still fine
  assert(parse("miss you<3", h) == "miss you" && h);
  printf("  heart suffix: OK ('...sm <3' -> text + drawn heart)\n");
}

int main() {
  printf("bento layout:\n");
  testLayout(536, 240, true);    // T-Display-S3 AMOLED
  testLayout(240, 135, false);   // classic T-Display
  testLayout(320, 170, true);    // T-Display-S3
  printf("pixel digits in the date box:\n");
  for (const char *n : {"1", "63", "365", "--"}) {
    testDigitsFitBox(536, 240, true, n);
    testDigitsFitBox(240, 135, false, n);
  }
  printf("message parsing:\n");
  testHeartSuffix();
  printf("\nALL BENTO-MATH TESTS PASSED\n");
  return 0;
}
