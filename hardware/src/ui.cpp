#include "ui.h"

#include <time.h>

namespace ui {
namespace {

constexpr uint16_t BG = rgb565(0, 0, 0);
constexpr uint16_t FG = rgb565(255, 255, 255);
constexpr uint16_t DIM = rgb565(160, 166, 180);
constexpr uint16_t FOOT = rgb565(118, 122, 134);
constexpr uint16_t ACCENT = rgb565(120, 200, 255);
constexpr uint16_t WARN = rgb565(255, 170, 40);
constexpr uint16_t TRACK = rgb565(36, 38, 46);
constexpr uint16_t BAR_OK = rgb565(80, 210, 120);
constexpr uint16_t BAR_HIGH = rgb565(250, 190, 50);
constexpr uint16_t BAR_FULL = rgb565(240, 80, 70);

constexpr int16_t MARGIN = 8;
constexpr int16_t INNER_W = Lcd::W - 2 * MARGIN;
constexpr uint8_t GAUGES = 2;
constexpr int16_t INLINE_GAP = 5;

// Three bytes per entry: English abbreviations and Korean UTF-8 syllables alike.
const char WEEKDAYS_EN[] PROGMEM = "SunMonTueWedThuFriSat";
const char WEEKDAYS_KO[] PROGMEM = "일월화수목금토";
const char MONTHS_EN[] PROGMEM = "JanFebMarAprMayJunJulAugSepOctNovDec";
// Gauge 0 is the outer/left/first gauge, gauge 1 the inner/right/second, matching the relay item order.
const char *const DEFAULT_LABELS[GAUGES] = {"Claude", "Fable"};

struct GaugeSpec {
  float cx, cy, rMid, halfW;
  float start, sweep;
  bool fromEnd;  // the fill grows back from the end of the track
};

// A text box: left edge, top edge and width. The height is the font's line height.
struct Box {
  int16_t x, y, w;
};

struct LayoutSpec {
  GaugeSpec gauges[GAUGES];
  int16_t timeY;
  Box date;
  bool inlineLabels;  // "label pct" on one line in FONT_S + FONT_PS; label[i].y is the top of the percentage
  Box label[GAUGES];
  Box pct[GAUGES];
  Box footer;
  bool footerShowsAge;
};

// Geometry follows the SOO-618 mock-ups at 1:1 scale.
const LayoutSpec LAYOUTS[LAYOUT_COUNT] = {
    // A: concentric rings, Claude outside, Fable inside, both filling clockwise from 12 o'clock.
    {{{120, 120, 110, 6, 0, 360, false}, {120, 120, 93, 6, 0, 360, false}},
     80,
     {45, 128, 150},
     true,
     {{64, 58, 112}, {64, 152, 112}},
     {},
     {66, 169, 108},
     false},
    // B: clock on top, two 270-degree gauges below, open at the bottom.
    {{{62, 162, 41, 3.5f, 225, 270, false}, {178, 162, 41, 3.5f, 225, 270, false}},
     19,
     {45, 70, 150},
     false,
     {{30, 196, 64}, {146, 196, 64}},
     {{30, 154, 64}, {146, 154, 64}},
     {MARGIN, 221, INNER_W},
     true},
    // C: left half ring for Claude, right half ring for Fable, both filling upwards from the bottom.
    {{{120, 120, 108, 7, 213.69f, 112.62f, false}, {120, 120, 108, 7, 33.69f, 112.62f, true}},
     75,
     {45, 125, 150},
     false,
     {{56, 160, 60}, {124, 160, 60}},
     {{56, 179, 60}, {124, 179, 60}},
     {50, 218, 140},
     false},
};

struct GaugeView {
  char label[17];
  int8_t pct;  // -1 when unknown
};

Lcd *lcd = nullptr;
Layout current = Layout::C;
Lang lang = Lang::Ko;
bool dirty = true;
int16_t timeW = 0;
char lastHm[8];
char lastDate[40];
uint16_t lastDateColor;
char lastFooter[64];
uint16_t lastFooterColor;
GaugeView lastGauge[GAUGES];
bool gaugeDrawn[GAUGES];

uint16_t barColor(int8_t pct) {
  if (pct >= 90) return BAR_FULL;
  if (pct >= 70) return BAR_HIGH;
  return BAR_OK;
}

void resetCache() {
  lastHm[0] = lastDate[0] = lastFooter[0] = '\0';
  lastDateColor = lastFooterColor = 0;
  for (uint8_t i = 0; i < GAUGES; i++) gaugeDrawn[i] = false;
}

void drawRing(const GaugeSpec &g, int8_t pct) {
  ArcSpan spans[2] = {{g.start, g.sweep, TRACK}, {0, 0, 0}};
  if (pct > 0) {
    const float fill = g.sweep * pct / 100;
    spans[1] = {g.fromEnd ? g.start + g.sweep - fill : g.start, fill, barColor(pct)};
  }
  lcd->drawRing(g.cx, g.cy, g.rMid, g.halfW, spans, 2, BG);
}

void pctText(int8_t pct, char *out, size_t n) {
  if (pct >= 0) snprintf(out, n, "%d%%", pct);
  else strlcpy(out, "--", n);
}

// Draws a label and a bold percentage on one baseline, repainting the whole box.
void drawInline(const Box &box, const char *label, const char *pct) {
  const int16_t lw = lcd->textWidth(FONT_S, label);
  const int16_t pw = lcd->textWidth(FONT_PS, pct);
  int16_t x = box.x + (box.w - lw - INLINE_GAP - pw) / 2;
  if (x < box.x) x = box.x;
  const int16_t right = box.x + box.w;
  const int16_t labelEnd = x + lw < right ? x + lw : right;
  const int16_t pctStart = labelEnd + INLINE_GAP < right ? labelEnd + INLINE_GAP : right;
  const int16_t sTop = box.y + FONT_PS.ascent - FONT_S.ascent;
  const int16_t sBottom = sTop + Lcd::lineHeight(FONT_S);
  const int16_t pBottom = box.y + Lcd::lineHeight(FONT_PS);
  const int16_t bottom = sBottom > pBottom ? sBottom : pBottom;

  if (labelEnd > box.x) {
    lcd->fillRect(box.x, box.y, labelEnd - box.x, sTop - box.y, BG);
    lcd->drawText(FONT_S, box.x, sTop, labelEnd - box.x, ALIGN_RIGHT, label, DIM, BG);
    lcd->fillRect(box.x, sBottom, labelEnd - box.x, bottom - sBottom, BG);
  }
  lcd->fillRect(labelEnd, box.y, pctStart - labelEnd, bottom - box.y, BG);
  if (right > pctStart) {
    lcd->drawText(FONT_PS, pctStart, box.y, right - pctStart, ALIGN_LEFT, pct, FG, BG);
    lcd->fillRect(pctStart, pBottom, right - pctStart, bottom - pBottom, BG);
  }
}

// Drops trailing words until text fits w, so "서버 오류 usage_http_5" becomes "서버 오류" rather than being cut mid-word.
void fitText(const Font &font, int16_t w, char *text) {
  while (lcd->textWidth(font, text) > w) {
    char *space = strrchr(text, ' ');
    if (!space) return;
    *space = '\0';
    // "a · b" loses the separator together with the dropped part.
    const size_t len = strlen(text);
    if (len >= 3 && !strcmp(text + len - 3, " \xC2\xB7")) text[len - 3] = '\0';
  }
}

void drawGauge(const LayoutSpec &spec, uint8_t i, const GaugeView &view) {
  const GaugeView &last = lastGauge[i];
  const bool fresh = !gaugeDrawn[i];
  const bool pctChanged = fresh || view.pct != last.pct;
  const bool labelChanged = fresh || strcmp(view.label, last.label);
  if (!pctChanged && !labelChanged) return;

  if (pctChanged) drawRing(spec.gauges[i], view.pct);
  char pct[8];
  pctText(view.pct, pct, sizeof(pct));
  if (spec.inlineLabels) {
    drawInline(spec.label[i], view.label, pct);
  } else {
    if (labelChanged) {
      const Box &b = spec.label[i];
      lcd->drawText(FONT_S, b.x, b.y, b.w, ALIGN_CENTER, view.label, DIM, BG);
    }
    if (pctChanged) {
      const Box &b = spec.pct[i];
      lcd->drawText(FONT_P, b.x, b.y, b.w, ALIGN_CENTER, pct, FG, BG);
    }
  }
  lastGauge[i] = view;
  gaugeDrawn[i] = true;
}

void gaugeView(const UsageState &state, uint8_t i, GaugeView &out) {
  if (state.haveData && i < state.count) {
    strlcpy(out.label, state.items[i].label, sizeof(out.label));
    out.pct = state.items[i].pct;
  } else {
    strlcpy(out.label, DEFAULT_LABELS[i], sizeof(out.label));
    out.pct = -1;
  }
}

Msg warningText(TokenWarning warning) {
  return warning == TokenWarning::NoCertCheck ? Msg::NoCertCheck : Msg::TokenPlaintext;
}

// One status line at the bottom. Warnings win over the reset time, which wins over the data age.
uint16_t footerText(const LayoutSpec &spec, bool timeValid, bool wifiUp, TokenWarning warning,
                    const UsageState &state, bool configured, char *out, size_t n) {
  char header[40];
  const bool usageWarn = usage::headerText(state, configured, millis(), lang, header, sizeof(header));
  if (timeValid && !wifiUp) {
    i18n::copy(lang, Msg::WifiLost, out, n);
    return WARN;
  }
  if (usageWarn) {
    strlcpy(out, header, n);
    return WARN;
  }
  if (warning != TokenWarning::None) {
    i18n::copy(lang, warningText(warning), out, n);
    return WARN;
  }
  const char *resets = "";
  for (uint8_t i = 0; state.haveData && i < state.count && !resets[0]; i++) resets = state.items[i].resets;
  if (resets[0] && spec.footerShowsAge) snprintf_P(out, n, i18n::text(lang, Msg::ResetsAndStatus), resets, header);
  else if (resets[0]) snprintf_P(out, n, i18n::text(lang, Msg::Resets), resets);
  else strlcpy(out, header, n);
  return FOOT;
}

void formatDate(const struct tm &t, char *out, size_t n) {
  char wday[4], month[4];
  const uint8_t w = t.tm_wday % 7;
  if (lang == Lang::En) {
    memcpy_P(wday, WEEKDAYS_EN + 3 * w, 3);
    memcpy_P(month, MONTHS_EN + 3 * (t.tm_mon % 12), 3);
    wday[3] = month[3] = '\0';
    snprintf_P(out, n, PSTR("%s, %s %d"), wday, month, t.tm_mday);
  } else {
    memcpy_P(wday, WEEKDAYS_KO + 3 * w, 3);
    wday[3] = '\0';
    snprintf_P(out, n, PSTR("%d월 %d일 %s요일"), t.tm_mon + 1, t.tm_mday, wday);
  }
}

}  // namespace

void begin(Lcd &display) {
  lcd = &display;
  timeW = lcd->textWidth(FONT_L, "00:00");
  invalidate();
}

void invalidate() {
  resetCache();
  if (lcd) lcd->fillScreen(BG);
  dirty = false;
}

void setLayout(Layout layout) {
  if (layout == current) return;
  current = layout;
  dirty = true;
}

void setLang(Lang next) {
  if (next == lang) return;
  lang = next;
  dirty = true;
}

void showMessage(const char *title, const char *line1, const char *line2, const char *line3) {
  lcd->fillScreen(BG);
  lcd->drawText(FONT_M, MARGIN, 40, INNER_W, ALIGN_CENTER, title, ACCENT, BG);
  lcd->drawText(FONT_S, MARGIN, 96, INNER_W, ALIGN_CENTER, line1, FG, BG);
  lcd->drawText(FONT_S, MARGIN, 124, INNER_W, ALIGN_CENTER, line2, FG, BG);
  lcd->drawText(FONT_S, MARGIN, 152, INNER_W, ALIGN_CENTER, line3, DIM, BG);
  dirty = true;
}

void showMessage(Msg title, const char *line1) {
  char t[32];
  i18n::copy(lang, title, t, sizeof(t));
  showMessage(t, line1);
}

void showMessage(Msg title, Msg line1) {
  char l[32];
  i18n::copy(lang, line1, l, sizeof(l));
  showMessage(title, l);
}

void showSetup(const char *apName, const char *apPass, const char *adminPass) {
  char buf[64];
  lcd->fillScreen(BG);
  i18n::copy(lang, Msg::SetupMode, buf, sizeof(buf));
  lcd->drawText(FONT_M, MARGIN, 6, INNER_W, ALIGN_CENTER, buf, ACCENT, BG);
  lcd->drawText(FONT_S, MARGIN, 44, INNER_W, ALIGN_LEFT, "Wi-Fi", DIM, BG);
  lcd->drawText(FONT_M, MARGIN, 64, INNER_W, ALIGN_LEFT, apName, FG, BG);
  i18n::copy(lang, Msg::Password, buf, sizeof(buf));
  lcd->drawText(FONT_S, MARGIN, 96, INNER_W, ALIGN_LEFT, buf, DIM, BG);
  lcd->drawText(FONT_M, MARGIN, 116, INNER_W, ALIGN_LEFT, apPass, FG, BG);
  i18n::copy(lang, Msg::OpenAddress, buf, sizeof(buf));
  lcd->drawText(FONT_S, MARGIN, 150, INNER_W, ALIGN_LEFT, buf, DIM, BG);
  snprintf_P(buf, sizeof(buf), i18n::text(lang, Msg::AdminLogin), adminPass);
  lcd->drawText(FONT_S, MARGIN, 176, INNER_W, ALIGN_LEFT, buf, WARN, BG);
  dirty = true;
}

void draw(bool timeValid, bool wifiUp, TokenWarning warning, const UsageState &state, bool configured) {
  if (dirty) invalidate();
  const LayoutSpec &spec = LAYOUTS[static_cast<uint8_t>(current)];

  char hm[8], date[40];
  uint16_t dateColor = DIM;
  if (timeValid) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
    formatDate(t, date, sizeof(date));
  } else {
    strlcpy(hm, "--:--", sizeof(hm));
    i18n::copy(lang, wifiUp ? Msg::SyncingTime : Msg::ConnectingWifi, date, sizeof(date));
    dateColor = WARN;
  }
  if (strcmp(hm, lastHm)) {
    strlcpy(lastHm, hm, sizeof(lastHm));
    lcd->drawText(FONT_L, (Lcd::W - timeW) / 2, spec.timeY, timeW, ALIGN_CENTER, hm, timeValid ? FG : DIM, BG);
  }
  if (strcmp(date, lastDate) || dateColor != lastDateColor) {
    strlcpy(lastDate, date, sizeof(lastDate));
    lastDateColor = dateColor;
    lcd->drawText(FONT_S, spec.date.x, spec.date.y, spec.date.w, ALIGN_CENTER, date, dateColor, BG);
  }

  for (uint8_t i = 0; i < GAUGES; i++) {
    GaugeView view;
    gaugeView(state, i, view);
    drawGauge(spec, i, view);
  }

  char footer[sizeof(lastFooter)];
  const uint16_t footerColor = footerText(spec, timeValid, wifiUp, warning, state, configured, footer, sizeof(footer));
  fitText(FONT_XS, spec.footer.w, footer);
  if (strcmp(footer, lastFooter) || footerColor != lastFooterColor) {
    strlcpy(lastFooter, footer, sizeof(lastFooter));
    lastFooterColor = footerColor;
    lcd->drawText(FONT_XS, spec.footer.x, spec.footer.y, spec.footer.w, ALIGN_CENTER, footer, footerColor, BG);
  }
}

}  // namespace ui
