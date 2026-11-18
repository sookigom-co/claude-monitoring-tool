// Usage: render_screens <out-dir>; renders every language/layout/state via a simulated ST7789, non-zero on failure.
#include <SPI.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "layout_cycle.h"
#include "lcd.h"
#include "ui.h"
#include "usage.h"

SPIClass SPI;

namespace {

constexpr int W = 240, H = 240;
constexpr uint8_t DC_PIN = 0;

uint16_t fb[W * H];
uint8_t ringMask[W * H];
uint8_t otherMask[W * H];
bool dcHigh = true;
uint8_t cmd = 0;
uint8_t args[4];
uint8_t argCount = 0;
uint16_t xs, xe, ys, ye;
int px, py;
bool highByte = true;
uint8_t hi = 0;
bool fullWindow = false;
bool inRing = false;
unsigned long pixelsWritten = 0;
unsigned long nowMs = 1000000;
time_t nowEpoch = 0;
int failures = 0;

void put(int x, int y, uint16_t color) {
  if (x < 0 || x >= W || y < 0 || y >= H) {
    failures++;
    std::printf("  FAIL pixel outside the panel at %d,%d\n", x, y);
    return;
  }
  fb[y * W + x] = color;
  pixelsWritten++;
  if (fullWindow) return;
  (inRing ? ringMask : otherMask)[y * W + x] = 1;
}

}  // namespace

void SPIClass::write(uint8_t b) {
  if (!dcHigh) {
    cmd = b;
    argCount = 0;
    if (cmd == 0x2C) {
      px = xs;
      py = ys;
      highByte = true;
      fullWindow = xs == 0 && ys == 0 && xe == W - 1 && ye == H - 1;
    }
    return;
  }
  if (cmd == 0x2A || cmd == 0x2B) {
    if (argCount < 4) args[argCount++] = b;
    if (argCount == 4) {
      uint16_t a = (args[0] << 8) | args[1], z = (args[2] << 8) | args[3];
      if (cmd == 0x2A) xs = a, xe = z;
      else ys = a, ye = z;
    }
  } else if (cmd == 0x2C) {
    if (highByte) {
      hi = b;
      highByte = false;
      return;
    }
    highByte = true;
    if (py > ye) return;
    put(px, py, (hi << 8) | b);
    if (++px > xe) {
      px = xs;
      py++;
    }
  }
}

void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t value) {
  if (pin == DC_PIN) dcHigh = value != 0;
}
void delay(unsigned long) {}
unsigned long millis() { return nowMs; }
void analogWriteRange(uint32_t) {}
void analogWrite(uint8_t, int) {}

// ui.cpp reads the wall clock through time(); the linker redirects it here (-Wl,--wrap=time).
extern "C" time_t __wrap_time(time_t *out) {
  if (out) *out = nowEpoch;
  return nowEpoch;
}

// Marks pixels written by the ring and bar renderers (-Wl,--wrap on Lcd::drawRing and Lcd::drawBar).
extern "C" void __real__ZN3Lcd8drawRingEffffPK7ArcSpanht(Lcd *, float, float, float, float, const ArcSpan *, uint8_t,
                                                           uint16_t);
extern "C" void __wrap__ZN3Lcd8drawRingEffffPK7ArcSpanht(Lcd *self, float cx, float cy, float r, float hw,
                                                           const ArcSpan *s, uint8_t n, uint16_t bg) {
  inRing = true;
  __real__ZN3Lcd8drawRingEffffPK7ArcSpanht(self, cx, cy, r, hw, s, n, bg);
  inRing = false;
}

extern "C" void __real__ZN3Lcd7drawBarEsssssttt(Lcd *, int16_t, int16_t, int16_t, int16_t, int16_t, uint16_t, uint16_t,
                                                 uint16_t);
extern "C" void __wrap__ZN3Lcd7drawBarEsssssttt(Lcd *self, int16_t x, int16_t y, int16_t w, int16_t h, int16_t fill,
                                                 uint16_t fc, uint16_t tc, uint16_t bg) {
  inRing = true;
  __real__ZN3Lcd7drawBarEsssssttt(self, x, y, w, h, fill, fc, tc, bg);
  inRing = false;
}

int textOverflow = 0;

// Counts text that is wider than its box and so would be clipped (-Wl,--wrap on Lcd::drawText).
extern "C" void __real__ZN3Lcd8drawTextERK4Fontsss5AlignPKctt(Lcd *, const Font &, int16_t, int16_t, int16_t, Align,
                                                         const char *, uint16_t, uint16_t);
extern "C" void __wrap__ZN3Lcd8drawTextERK4Fontsss5AlignPKctt(Lcd *self, const Font &font, int16_t x, int16_t y, int16_t w,
                                                         Align align, const char *text, uint16_t fg, uint16_t bg) {
  const int16_t tw = self->textWidth(font, text);
  if (tw > w) {
    textOverflow += tw - w;
    std::printf("    overflow %dpx: \"%s\" in a %dpx box\n", tw - w, text, w);
  }
  __real__ZN3Lcd8drawTextERK4Fontsss5AlignPKctt(self, font, x, y, w, align, text, fg, bg);
}

namespace {

Lcd lcd;
const char *const LAYOUT_NAMES[] = {"A", "B", "C", "D"};
static_assert(sizeof(LAYOUT_NAMES) / sizeof(LAYOUT_NAMES[0]) == LAYOUT_COUNT, "one name per layout");
const Lang LANGS[] = {Lang::En, Lang::Ko};

void clearMasks() {
  std::memset(ringMask, 0, sizeof(ringMask));
  std::memset(otherMask, 0, sizeof(otherMask));
}

// A text box that repaints a ring pixel (or a ring that repaints a text box) would leave a visible scar.
int overlapCount() {
  int n = 0;
  for (int i = 0; i < W * H; i++) n += ringMask[i] && otherMask[i];
  return n;
}

void check(bool ok, const char *what) {
  if (!ok) {
    failures++;
    std::printf("  FAIL %s\n", what);
  }
}

bool writePpm(const std::string &path) {
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint16_t c = fb[i];
    uint8_t rgb[3] = {uint8_t(((c >> 11) & 0x1F) * 255 / 31), uint8_t(((c >> 5) & 0x3F) * 255 / 63),
                      uint8_t((c & 0x1F) * 255 / 31)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
  return true;
}

std::string relayJson(int claude, int fable, const char *status, const char *error) {
  char buf[512];
  auto item = [](const char *label, int pct, char *out, size_t n) {
    if (pct >= 0)
      std::snprintf(out, n, R"({"label":"%s","value":"%d%%","pct":%d,"resets":"11-18 09:00"})", label, pct, pct);
    else std::snprintf(out, n, R"({"label":"%s","value":"n/a","pct":null})", label);
  };
  char a[128], b[128];
  item("Claude", claude, a, sizeof(a));
  item("Fable", fable, b, sizeof(b));
  std::snprintf(buf, sizeof(buf), R"({"updated":"2026-11-15T14:25:00+09:00","period":"7d","status":"%s",%s%s%s"items":[%s,%s]})",
                status, error ? R"("error":")" : "", error ? error : "", error ? "\"," : "", a, b);
  return buf;
}

UsageState fromRelay(const std::string &json) {
  UsageState st;
  if (!usage::parse(json.c_str(), json.size(), st)) {
    failures++;
    std::printf("  FAIL relay JSON did not parse: %s\n", json.c_str());
  }
  st.haveData = true;
  st.lastOk = true;
  st.lastSuccessMs = nowMs - 2 * 60000;
  st.lastAttemptMs = st.lastSuccessMs;
  return st;
}

struct Scenario {
  const char *name;
  bool timeValid;
  bool wifiUp;
  TokenWarning warning;
  UsageState state;
};

std::vector<Scenario> scenarios() {
  std::vector<Scenario> out;
  const TokenWarning none = TokenWarning::None;
  out.push_back({"normal", true, true, none, fromRelay(relayJson(42, 55, "ok", nullptr))});
  out.push_back({"pct70", true, true, none, fromRelay(relayJson(74, 78, "ok", nullptr))});
  out.push_back({"pct90", true, true, none, fromRelay(relayJson(93, 97, "ok", nullptr))});
  UsageState failed;
  failed.lastAttemptMs = nowMs - 60000;
  out.push_back({"nodata", true, true, none, failed});
  out.push_back({"relay-error", true, true, none, fromRelay(relayJson(42, 76, "error", "usage_http_5"))});
  UsageState stale = fromRelay(relayJson(42, 55, "stale", "refresh_failed"));
  out.push_back({"relay-stale", true, true, none, stale});
  out.push_back({"warn-token", true, true, TokenWarning::Plaintext, fromRelay(relayJson(42, 55, "ok", nullptr))});
  out.push_back({"warn-cert", true, true, TokenWarning::NoCertCheck, fromRelay(relayJson(42, 55, "ok", nullptr))});
  UsageState loading;
  out.push_back({"no-time", false, true, none, loading});
  out.push_back({"wifi-lost", true, false, none, fromRelay(relayJson(100, 0, "ok", nullptr))});
  return out;
}

void renderAll(const std::string &dir) {
  std::printf("render: 2 languages x %d layouts x %zu states\n", LAYOUT_COUNT, scenarios().size());
  for (Lang lang : LANGS) {
    ui::setLang(lang);
    for (uint8_t l = 0; l < LAYOUT_COUNT; l++) {
      for (const Scenario &sc : scenarios()) {
        ui::setLayout(static_cast<Layout>(l));
        ui::invalidate();
        clearMasks();
        textOverflow = 0;
        ui::draw(sc.timeValid, sc.wifiUp, sc.warning, sc.state, true);
        const int overlap = overlapCount();
        const std::string name = std::string(settings::langName(lang)) + "-" + LAYOUT_NAMES[l] + "-" + sc.name;
        std::printf("  %-20s overlap %d px, overflow %d px\n", name.c_str(), overlap, textOverflow);
        check(overlap == 0, "text and ring pixels overlap");
        check(textOverflow == 0, "text wider than its box");
        check(writePpm(dir + "/" + name + ".ppm"), "write ppm");
      }
    }
  }
}

// Design D with the values of the SOO-655 mock-up sheet, for a side-by-side comparison.
void renderMockup(const std::string &dir) {
  std::puts("render: design D with the mock-up values");
  UsageState failed;
  failed.lastAttemptMs = nowMs - 60000;
  const struct {
    const char *name;
    Lang lang;
    bool wifiUp;
    UsageState state;
  } shots[] = {
      {"normal", Lang::Ko, true, fromRelay(relayJson(42, 76, "ok", nullptr))},
      {"low", Lang::Ko, true, fromRelay(relayJson(15, 55, "ok", nullptr))},
      {"pct90", Lang::Ko, true, fromRelay(relayJson(93, 97, "ok", nullptr))},
      {"nodata", Lang::Ko, true, failed},
      {"relay-stale", Lang::Ko, true, fromRelay(relayJson(42, 76, "stale", "refresh_failed"))},
      {"relay-error", Lang::Ko, true, fromRelay(relayJson(42, 76, "error", "usage_http_5"))},
      {"wifi-lost", Lang::Ko, false, fromRelay(relayJson(100, 0, "ok", nullptr))},
      {"english", Lang::En, true, fromRelay(relayJson(42, 76, "ok", nullptr))},
  };
  ui::setLayout(Layout::D);
  for (const auto &shot : shots) {
    ui::setLang(shot.lang);
    ui::invalidate();
    clearMasks();
    textOverflow = 0;
    ui::draw(true, shot.wifiUp, TokenWarning::None, shot.state, true);
    std::printf("  mock-D-%-12s overlap %d px, overflow %d px\n", shot.name, overlapCount(), textOverflow);
    check(overlapCount() == 0, "text and bar pixels overlap");
    check(textOverflow == 0, "text wider than its box");
    check(writePpm(dir + "/mock-D-" + shot.name + ".ppm"), "write ppm");
  }
}

void renderSetup(const std::string &dir) {
  std::puts("render: setup mode and message screens in both languages");
  for (Lang lang : LANGS) {
    const std::string prefix = dir + "/" + settings::langName(lang);
    ui::setLang(lang);
    ui::showSetup("SmartClock-3F2A", "kq7mxw4p", "h3dTnc9wre");
    check(writePpm(prefix + "-setup.ppm"), "write ppm");
    ui::showMessage(Msg::Updating, "firmware.bin");
    check(writePpm(prefix + "-updating.ppm"), "write ppm");
    ui::showMessage(Msg::ConnectingWifi, "home-2.4G");
    check(writePpm(prefix + "-connecting.ppm"), "write ppm");
  }
}

unsigned long frame(const UsageState &st) {
  pixelsWritten = 0;
  ui::draw(true, true, TokenWarning::None, st, true);
  return pixelsWritten;
}

void partialRedraw() {
  std::puts("partial redraw: pixels sent per frame (240x240 = 57600)");
  const int timeBox = lcd.textWidth(FONT_L, "00:00") * Lcd::lineHeight(FONT_L);
  for (uint8_t l = 0; l < LAYOUT_COUNT; l++) {
    const Layout layout = static_cast<Layout>(l);
    UsageState st = fromRelay(relayJson(42, 55, "ok", nullptr));
    nowEpoch = 1794720420;  // 2026-11-15 14:27:00 KST
    ui::setLayout(layout);
    pixelsWritten = 0;
    ui::invalidate();
    clearMasks();
    const unsigned long fill = pixelsWritten;
    const unsigned long first = fill + frame(st);
    unsigned long idle = 0;
    for (int i = 0; i < 590; i++) {  // 59 s of 100 ms frames inside the same minute
      nowMs += 100;
      if (i % 10 == 9) nowEpoch++;
      idle += frame(st);
    }
    nowEpoch = 1794720480;  // 14:28:00
    const unsigned long minute = frame(st);
    UsageState changed = fromRelay(relayJson(47, 55, "ok", nullptr));
    const unsigned long usageChange = frame(changed);
    const unsigned long same = frame(changed);
    const unsigned long minuteBox = layout == Layout::D ? 0 : timeBox;
    std::printf("  %s: first %lu, 590 idle frames %lu, minute change %lu (time box %lu), Claude 42->47 %lu, repeat %lu, overlap %d\n",
                LAYOUT_NAMES[l], first, idle, minute, minuteBox, usageChange, same, overlapCount());
    check(first >= 57600, "first frame paints the whole screen");
    check(idle == 0, "nothing is sent while the minute does not change");
    check(minute == minuteBox, "a minute change repaints only the time box, or nothing without a clock");
    check(usageChange > 0 && usageChange < 57600 / 2, "a usage change repaints only part of the screen");
    if (layout == Layout::D) {
      // Only the Claude digits box and its bar: the "%", the name and the footer stay as they are.
      const unsigned long digitsBox = (122 - lcd.textWidth(FONT_P, "%")) * Lcd::lineHeight(FONT_L);
      check(usageChange == digitsBox + 204 * 8, "D repaints only the changed number and bar");
    }
    check(same == 0, "an unchanged frame sends nothing");
    check(overlapCount() == 0, "updates never touch the other element's pixels");
  }
}

// Partial redraws through 42 -> -- -> 100 -> 7 must end on exactly the pixels of a fresh frame.
void rowTransitions() {
  std::puts("D value changes: partial redraws match a fresh frame");
  ui::setLayout(Layout::D);
  ui::invalidate();
  UsageState failed;
  failed.lastAttemptMs = nowMs - 60000;
  const UsageState steps[] = {fromRelay(relayJson(42, 55, "ok", nullptr)), failed,
                              fromRelay(relayJson(100, 0, "ok", nullptr)), fromRelay(relayJson(7, 100, "ok", nullptr))};
  for (const UsageState &st : steps) frame(st);
  std::vector<uint16_t> partial(fb, fb + W * H);
  ui::invalidate();
  frame(steps[3]);
  int diff = 0;
  for (int i = 0; i < W * H; i++) diff += partial[i] != fb[i];
  std::printf("  pixels differing from a fresh frame: %d\n", diff);
  check(diff == 0, "partial redraws leave no stale pixels");
}

void layoutSwitch() {
  std::puts("layout switch: setLayout repaints once, same layout is a no-op");
  UsageState st = fromRelay(relayJson(42, 55, "ok", nullptr));
  ui::setLayout(Layout::C);
  ui::invalidate();
  frame(st);
  ui::setLayout(Layout::C);
  check(frame(st) == 0, "setting the same layout repaints nothing");
  ui::setLayout(Layout::A);
  const unsigned long sw = frame(st);
  std::printf("  C->A switch sends %lu px\n", sw);
  check(sw >= 57600, "a layout switch repaints the screen");
  check(frame(st) == 0, "the frame after a switch sends nothing");
}

void langSwitch() {
  std::puts("language switch: setLang repaints once, same language is a no-op");
  UsageState st = fromRelay(relayJson(42, 55, "ok", nullptr));
  ui::setLang(Lang::Ko);
  ui::setLayout(Layout::C);
  ui::invalidate();
  frame(st);
  ui::setLang(Lang::Ko);
  check(frame(st) == 0, "setting the same language repaints nothing");
  ui::setLang(Lang::En);
  const unsigned long sw = frame(st);
  std::printf("  ko->en switch sends %lu px\n", sw);
  check(sw >= 57600, "a language switch repaints the screen");
  check(frame(st) == 0, "the frame after a switch sends nothing");
}

void cycle() {
  std::puts("layout cycle: fixed, cycle order, interval, restart, millis wrap");
  Settings s;
  LayoutCycle c;
  c.restart(0);
  check(c.active(s, 999999) == Layout::C, "default is fixed C");
  s.layoutMode = LayoutMode::Cycle;
  s.cycleSec = 15;
  s.layout = Layout::B;
  c.restart(1000);
  check(c.active(s, 1000) == Layout::B, "cycle starts at the chosen design");
  check(c.active(s, 15999) == Layout::B, "no switch before the interval");
  check(c.active(s, 16000) == Layout::C, "switch after 15 s");
  check(c.active(s, 31000) == Layout::D, "C -> D");
  check(c.active(s, 46000) == Layout::A, "wraps D -> A");
  check(c.active(s, 61000) == Layout::B, "back to B after four intervals");
  check(c.active(s, 61000 + 60000 * 3) == Layout::B, "skipped intervals are counted");
  const uint32_t nearWrap = 0xFFFFF000UL;  // 4.1 s before millis() wraps to 0
  c.restart(nearWrap);
  check(c.active(s, static_cast<uint32_t>(nearWrap + 14000u)) == Layout::B, "no switch across the wrap before 15 s");
  check(c.active(s, static_cast<uint32_t>(nearWrap + 16000u)) == Layout::C, "switch after 15 s across the millis wrap");
  s.layoutMode = LayoutMode::Fixed;
  check(c.active(s, 0) == Layout::B, "fixed ignores the cycle");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <out-dir>\n", argv[0]);
    return 2;
  }
  setenv("TZ", "KST-9", 1);
  tzset();
  nowEpoch = 1794720420;  // 2026-11-15 14:27:00 KST, a Sunday
  LcdPins pins{13, 14, -1, DC_PIN, 2, 5, true, 3, true, false, 0};
  lcd.begin(pins);
  ui::begin(lcd);
  renderAll(argv[1]);
  renderMockup(argv[1]);
  renderSetup(argv[1]);
  ui::setLang(Lang::Ko);
  partialRedraw();
  rowTransitions();
  layoutSwitch();
  langSwitch();
  cycle();
  std::printf("%d failures\n", failures);
  return failures ? 1 : 0;
}
