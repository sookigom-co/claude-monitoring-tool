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
constexpr int16_t CHART_GAP = 2;
constexpr int16_t CHART_ZERO_H = 2;
constexpr int16_t CHART_MIN_SCALE = 20;  // tenths of a percentage point at full column height

// Three bytes per entry: English abbreviations and Korean UTF-8 syllables alike.
const char WEEKDAYS_EN[] PROGMEM = "SunMonTueWedThuFriSat";
const char WEEKDAYS_KO[] PROGMEM = "일월화수목금토";
const char MONTHS_EN[] PROGMEM = "JanFebMarAprMayJunJulAugSepOctNovDec";
// Gauge 0 is the outer/left/first gauge, gauge 1 the inner/right/second, matching the relay item order.
const char *const DEFAULT_LABELS[GAUGES] = {"Claude", "Fable"};

// A horizontal round-ended bar filling from the left: left edge, top edge, width and height.
struct BarSpec {
  int16_t x, y, w, h;
};

struct GaugeSpec {
  float cx, cy, rMid, halfW;
  float start, sweep;
  bool fromEnd;  // the fill grows back from the end of the track
  BarSpec bar;   // drawn instead of the ring when bar.w > 0
};

// A text box: left edge, top edge and width. The height is the font's line height.
struct Box {
  int16_t x, y, w;
};

enum class Labels : uint8_t {
  Stacked,  // label in FONT_S above or below a FONT_P percentage, each centred in its box
  Inline,   // "label pct" on one line in FONT_S + FONT_PS; label[i].y is the top of the percentage
  Row,      // FONT_M label on the left, FONT_L number with a FONT_P "%" right-aligned in pct[i]
  Compact,  // FONT_M label on the left, FONT_P percentage right-aligned in pct[i]
};

enum class FooterStatus : uint8_t {
  None,         // the reset time alone when there is one
  Age,          // the reset time followed by the data age
  AgeAndStale,  // as Age, and a stale relay keeps the reset time in front of the warning
};

struct LayoutSpec {
  GaugeSpec gauges[GAUGES];
  int16_t timeY;  // negative: no clock and no date
  Box date;
  Labels labels;
  Box label[GAUGES];
  Box pct[GAUGES];
  Box footer;
  FooterStatus footerStatus;
  Box header;  // FONT_XS title when w > 0
  Msg title;
  BarSpec chart[GAUGES];  // hourly increase columns drawn instead of the gauge when w > 0
  Box delta[GAUGES];      // FONT_XS "1h +1.4% · 24h +11.2%" line when w > 0
};

// Geometry follows the SOO-618 and SOO-655 mock-ups at 1:1 scale.
const LayoutSpec LAYOUTS[LAYOUT_COUNT] = {
    // A: concentric rings, Claude outside, Fable inside, both filling clockwise from 12 o'clock.
    {{{120, 120, 110, 6, 0, 360, false, {}}, {120, 120, 93, 6, 0, 360, false, {}}},
     80,
     {45, 128, 150},
     Labels::Inline,
     {{64, 58, 112}, {64, 152, 112}},
     {},
     {66, 169, 108},
     FooterStatus::None,
     {},
     Msg::Last7Days,
     {},
     {}},
    // B: clock on top, two 270-degree gauges below, open at the bottom.
    {{{62, 162, 41, 3.5f, 225, 270, false, {}}, {178, 162, 41, 3.5f, 225, 270, false, {}}},
     19,
     {45, 70, 150},
     Labels::Stacked,
     {{30, 196, 64}, {146, 196, 64}},
     {{30, 154, 64}, {146, 154, 64}},
     {MARGIN, 221, INNER_W},
     FooterStatus::Age,
     {},
     Msg::Last7Days,
     {},
     {}},
    // C: left half ring for Claude, right half ring for Fable, both filling upwards from the bottom.
    {{{120, 120, 108, 7, 213.69f, 112.62f, false, {}}, {120, 120, 108, 7, 33.69f, 112.62f, true, {}}},
     75,
     {45, 125, 150},
     Labels::Stacked,
     {{56, 160, 60}, {124, 160, 60}},
     {{56, 179, 60}, {124, 179, 60}},
     {50, 218, 140},
     FooterStatus::None,
     {},
     Msg::Last7Days,
     {},
     {}},
    // D: usage only, a name and a large number over a full-width bar for Claude, then the same for Fable.
    {{{0, 0, 0, 0, 0, 0, false, {18, 100, 204, 8}}, {0, 0, 0, 0, 0, 0, false, {18, 190, 204, 8}}},
     -1,
     {},
     Labels::Row,
     {{18, 53, 76}, {18, 143, 76}},
     {{100, 46, 122}, {100, 136, 122}},
     {MARGIN, 218, INNER_W},
     FooterStatus::AgeAndStale,
     {MARGIN, 8, INNER_W},
     Msg::Last7Days,
     {},
     {}},
    // E: usage only, a name and the current percentage over 24 hourly increase columns and the 1h/24h increase.
    {{},
     -1,
     {},
     Labels::Compact,
     {{18, 22, 100}, {18, 120, 100}},
     {{122, 26, 100}, {122, 124, 100}},
     {MARGIN, 220, INNER_W},
     FooterStatus::AgeAndStale,
     {MARGIN, 4, INNER_W},
     Msg::Last24Hours,
     {{25, 53, 190, 38}, {25, 151, 190, 38}},
     {{18, 95, 204}, {18, 193, 204}}},
};

struct GaugeView {
  char label[17];
  int8_t pct;  // -1 when unknown
  int16_t delta1h;
  int16_t delta24h;
  int16_t hourly[HOURLY_SLOTS];
  int16_t scale;  // tenths of a percentage point at full column height, shared by both charts
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
bool headerDrawn;
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
  headerDrawn = false;
  for (uint8_t i = 0; i < GAUGES; i++) gaugeDrawn[i] = false;
}

void drawTrack(const GaugeSpec &g, int8_t pct) {
  if (g.bar.w > 0) {
    const BarSpec &b = g.bar;
    const int16_t fill = pct > 0 ? static_cast<int16_t>(static_cast<int32_t>(b.w) * pct / 100) : 0;
    lcd->drawBar(b.x, b.y, b.w, b.h, fill, barColor(pct), TRACK, BG);
    return;
  }
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

// Draws a large number with a smaller "%" on the same baseline at the right end of the box, or "--" alone.
void drawRowValue(const Box &box, int8_t pct, bool signShown) {
  const int16_t signW = lcd->textWidth(FONT_P, "%");
  const int16_t signX = box.x + box.w - signW;
  if (pct < 0) {
    lcd->drawText(FONT_L, box.x, box.y, box.w, ALIGN_RIGHT, "--", FG, BG);
    return;
  }
  char digits[4];
  snprintf(digits, sizeof(digits), "%d", pct);
  lcd->drawText(FONT_L, box.x, box.y, signX - box.x, ALIGN_RIGHT, digits, FG, BG);
  if (signShown) return;
  const int16_t signY = box.y + FONT_L.ascent - FONT_P.ascent;
  const int16_t signBottom = signY + Lcd::lineHeight(FONT_P);
  const int16_t bottom = box.y + Lcd::lineHeight(FONT_L);
  lcd->fillRect(signX, box.y, signW, signY - box.y, BG);
  lcd->drawText(FONT_P, signX, signY, signW, ALIGN_LEFT, "%", DIM, BG);
  lcd->fillRect(signX, signBottom, signW, bottom - signBottom, BG);
}

// Draws hourly increases as columns: track only for no sample, a thin floor line for zero or less, else a fill.
void drawChart(const BarSpec &c, const int16_t *hourly, int16_t scale) {
  const int16_t colW = (c.w + CHART_GAP) / HOURLY_SLOTS - CHART_GAP;
  for (uint8_t i = 0; i < HOURLY_SLOTS; i++) {
    const int16_t x = c.x + i * (colW + CHART_GAP);
    const int16_t v = hourly[i];
    int16_t fillH = 0;
    uint16_t fillColor = ACCENT;
    if (v != DELTA_NO_SAMPLE && v <= 0) {
      fillH = CHART_ZERO_H;
      fillColor = DIM;
    } else if (v != DELTA_NO_SAMPLE) {
      fillH = static_cast<int16_t>((static_cast<int32_t>(v) * c.h + scale / 2) / scale);
      fillH = constrain(fillH, CHART_ZERO_H, c.h);
    }
    lcd->fillRect(x, c.y, colW, c.h - fillH, TRACK);
    if (fillH) lcd->fillRect(x, c.y + c.h - fillH, colW, fillH, fillColor);
  }
}

uint16_t deltaColor(int16_t tenths) { return tenths == DELTA_MISSING || tenths == DELTA_NO_SAMPLE ? DIM : FG; }

// Draws "1h <value> · 24h <value>" with dim names and bright known values, repainting the whole box.
void drawDeltas(const Box &box, int16_t delta1h, int16_t delta24h) {
  char v1[24], v24[24];
  usage::deltaText(delta1h, lang, v1, sizeof(v1));
  usage::deltaText(delta24h, lang, v24, sizeof(v24));
  const struct {
    const char *text;
    uint16_t color;
  } parts[] = {
      {"1h ", DIM}, {v1, deltaColor(delta1h)}, {" \xC2\xB7 ", DIM}, {"24h ", DIM}, {v24, deltaColor(delta24h)},
  };
  const int16_t right = box.x + box.w;
  int16_t x = box.x;
  for (const auto &part : parts) {
    const int16_t tw = lcd->textWidth(FONT_XS, part.text);
    const int16_t w = tw < right - x ? tw : right - x;
    if (w <= 0) break;
    lcd->drawText(FONT_XS, x, box.y, w, ALIGN_LEFT, part.text, part.color, BG);
    x += w;
  }
  if (x < right) lcd->fillRect(x, box.y, right - x, Lcd::lineHeight(FONT_XS), BG);
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
  const bool chart = spec.chart[i].w > 0;
  const bool chartChanged =
      chart && (fresh || view.scale != last.scale || memcmp(view.hourly, last.hourly, sizeof(view.hourly)));
  const bool deltaChanged =
      spec.delta[i].w > 0 && (fresh || view.delta1h != last.delta1h || view.delta24h != last.delta24h);
  if (!pctChanged && !labelChanged && !chartChanged && !deltaChanged) return;

  if (chartChanged) drawChart(spec.chart[i], view.hourly, view.scale);
  if (deltaChanged) drawDeltas(spec.delta[i], view.delta1h, view.delta24h);
  if (pctChanged && !chart) drawTrack(spec.gauges[i], view.pct);
  char pct[8];
  pctText(view.pct, pct, sizeof(pct));
  if (spec.labels == Labels::Inline) {
    drawInline(spec.label[i], view.label, pct);
  } else if (spec.labels == Labels::Row) {
    if (labelChanged) {
      const Box &b = spec.label[i];
      lcd->drawText(FONT_M, b.x, b.y, b.w, ALIGN_LEFT, view.label, DIM, BG);
    }
    if (pctChanged) drawRowValue(spec.pct[i], view.pct, !fresh && last.pct >= 0);
  } else if (spec.labels == Labels::Compact) {
    if (labelChanged) {
      const Box &b = spec.label[i];
      lcd->drawText(FONT_M, b.x, b.y, b.w, ALIGN_LEFT, view.label, DIM, BG);
    }
    if (pctChanged) {
      const Box &b = spec.pct[i];
      lcd->drawText(FONT_P, b.x, b.y, b.w, ALIGN_RIGHT, pct, FG, BG);
    }
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

// A relay error or a missing response leaves the increases unknown ("--") rather than "no sample".
void gaugeView(const UsageState &state, uint8_t i, int16_t scale, GaugeView &out) {
  const bool have = state.haveData && i < state.count;
  const UsageItem *item = have ? &state.items[i] : nullptr;
  const bool deltas = have && state.relayStatus != RelayStatus::Error;
  strlcpy(out.label, have ? item->label : DEFAULT_LABELS[i], sizeof(out.label));
  out.pct = have ? item->pct : -1;
  out.delta1h = deltas ? item->delta1h : DELTA_MISSING;
  out.delta24h = deltas ? item->delta24h : DELTA_MISSING;
  for (uint8_t h = 0; h < HOURLY_SLOTS; h++) out.hourly[h] = deltas && state.haveHourly ? item->hourly[h] : DELTA_NO_SAMPLE;
  out.scale = scale;
}

// The larger of CHART_MIN_SCALE and the biggest hourly increase of the shown items, so both charts share one scale.
int16_t chartScale(const UsageState &state) {
  int16_t scale = CHART_MIN_SCALE;
  for (uint8_t i = 0; state.haveData && state.haveHourly && i < state.count && i < GAUGES; i++) {
    for (uint8_t h = 0; h < HOURLY_SLOTS; h++) {
      if (state.items[i].hourly[h] > scale) scale = state.items[i].hourly[h];
    }
  }
  return scale;
}

Msg warningText(TokenWarning warning) {
  return warning == TokenWarning::NoCertCheck ? Msg::NoCertCheck : Msg::TokenPlaintext;
}

// One status line at the bottom: time sync on clockless designs, then warnings, the reset time, the data age.
uint16_t footerText(const LayoutSpec &spec, bool timeValid, bool wifiUp, TokenWarning warning,
                    const UsageState &state, bool configured, char *out, size_t n) {
  char header[40];
  const bool usageWarn = usage::headerText(state, configured, millis(), lang, header, sizeof(header));
  if (!timeValid && spec.timeY < 0) {
    i18n::copy(lang, wifiUp ? Msg::WaitingForTime : Msg::ConnectingWifi, out, n);
    return WARN;
  }
  if (timeValid && !wifiUp) {
    i18n::copy(lang, Msg::WifiLost, out, n);
    return WARN;
  }
  const char *resets = "";
  for (uint8_t i = 0; state.haveData && i < state.count && !resets[0]; i++) resets = state.items[i].resets;
  if (usageWarn) {
    if (resets[0] && spec.footerStatus == FooterStatus::AgeAndStale && state.relayStatus == RelayStatus::Stale) {
      snprintf_P(out, n, i18n::text(lang, Msg::ResetsAndStatus), resets, header);
    } else {
      strlcpy(out, header, n);
    }
    return WARN;
  }
  if (warning != TokenWarning::None) {
    i18n::copy(lang, warningText(warning), out, n);
    return WARN;
  }
  const bool withAge = spec.footerStatus != FooterStatus::None;
  if (resets[0] && withAge) snprintf_P(out, n, i18n::text(lang, Msg::ResetsAndStatus), resets, header);
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

void drawClock(const LayoutSpec &spec, bool timeValid, bool wifiUp) {
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

  if (spec.header.w > 0 && !headerDrawn) {
    char title[32];
    i18n::copy(lang, spec.title, title, sizeof(title));
    lcd->drawText(FONT_XS, spec.header.x, spec.header.y, spec.header.w, ALIGN_CENTER, title, DIM, BG);
    headerDrawn = true;
  }
  if (spec.timeY >= 0) drawClock(spec, timeValid, wifiUp);

  const int16_t scale = spec.chart[0].w > 0 ? chartScale(state) : CHART_MIN_SCALE;
  for (uint8_t i = 0; i < GAUGES; i++) {
    GaugeView view;
    gaugeView(state, i, scale, view);
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
