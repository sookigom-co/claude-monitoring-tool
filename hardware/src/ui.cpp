#include "ui.h"

#include <time.h>

namespace ui {
namespace {

constexpr uint16_t BG = rgb565(0, 0, 0);
constexpr uint16_t FG = rgb565(255, 255, 255);
constexpr uint16_t DIM = rgb565(140, 140, 150);
constexpr uint16_t ACCENT = rgb565(120, 200, 255);
constexpr uint16_t WARN = rgb565(255, 170, 40);
constexpr uint16_t TRACK = rgb565(45, 45, 55);
constexpr uint16_t BAR_OK = rgb565(80, 210, 120);
constexpr uint16_t BAR_HIGH = rgb565(250, 190, 50);
constexpr uint16_t BAR_FULL = rgb565(240, 80, 70);

constexpr int16_t MARGIN = 8;
constexpr int16_t INNER_W = Lcd::W - 2 * MARGIN;
constexpr int16_t DATE_Y = 4;
constexpr int16_t TIME_Y = 36;
constexpr int16_t STATUS_Y = 92;
constexpr int16_t DIVIDER_Y = 114;
constexpr int16_t HEADER_Y = 118;
constexpr int16_t ITEMS_Y = 142;
constexpr int16_t ITEMS_H = Lcd::H - ITEMS_Y - 2;
constexpr int16_t HEADER_GAP = 8;
constexpr int16_t RESETS_GAP = 8;

const char *const WEEKDAYS[7] = {"일", "월", "화", "수", "목", "금", "토"};

Lcd *lcd = nullptr;
char lastDate[40];
char lastHm[8];
char lastSec[4];
char lastStatus[40];
char lastUsageKey[256];

uint16_t barColor(int8_t pct) {
  if (pct >= 90) return BAR_FULL;
  if (pct >= 70) return BAR_HIGH;
  return BAR_OK;
}

void drawBar(int16_t y, int16_t w, int16_t h, int8_t pct) {
  int16_t filled = static_cast<int32_t>(w) * pct / 100;
  if (filled > 0) lcd->fillRect(MARGIN, y, filled, h, barColor(pct));
  if (filled < w) lcd->fillRect(MARGIN + filled, y, w - filled, h, TRACK);
}

void drawStatus(const char *text, uint16_t color) {
  if (!strcmp(text, lastStatus)) return;
  strlcpy(lastStatus, text, sizeof(lastStatus));
  lcd->drawText(FONT_S, MARGIN, STATUS_Y, INNER_W, ALIGN_CENTER, text, color, BG);
}

}  // namespace

void begin(Lcd &display) {
  lcd = &display;
  invalidate();
}

void invalidate() {
  lastDate[0] = lastHm[0] = lastSec[0] = lastStatus[0] = lastUsageKey[0] = '\0';
  if (!lcd) return;
  lcd->fillScreen(BG);
  lcd->fillRect(MARGIN, DIVIDER_Y, INNER_W, 1, TRACK);
}

void showMessage(const char *title, const char *line1, const char *line2, const char *line3) {
  lcd->fillScreen(BG);
  lcd->drawText(FONT_M, MARGIN, 40, INNER_W, ALIGN_CENTER, title, ACCENT, BG);
  lcd->drawText(FONT_S, MARGIN, 96, INNER_W, ALIGN_CENTER, line1, FG, BG);
  lcd->drawText(FONT_S, MARGIN, 124, INNER_W, ALIGN_CENTER, line2, FG, BG);
  lcd->drawText(FONT_S, MARGIN, 152, INNER_W, ALIGN_CENTER, line3, DIM, BG);
  lastDate[0] = '\0';
}

void showSetup(const char *apName, const char *apPass, const char *adminPass) {
  char buf[64];
  lcd->fillScreen(BG);
  lcd->drawText(FONT_M, MARGIN, 6, INNER_W, ALIGN_CENTER, "설정 모드", ACCENT, BG);
  lcd->drawText(FONT_S, MARGIN, 44, INNER_W, ALIGN_LEFT, "Wi-Fi", DIM, BG);
  lcd->drawText(FONT_M, MARGIN, 64, INNER_W, ALIGN_LEFT, apName, FG, BG);
  lcd->drawText(FONT_S, MARGIN, 96, INNER_W, ALIGN_LEFT, "비밀번호", DIM, BG);
  lcd->drawText(FONT_M, MARGIN, 116, INNER_W, ALIGN_LEFT, apPass, FG, BG);
  lcd->drawText(FONT_S, MARGIN, 150, INNER_W, ALIGN_LEFT, "접속 주소  http://192.168.4.1", DIM, BG);
  snprintf(buf, sizeof(buf), "관리자  admin / %s", adminPass);
  lcd->drawText(FONT_S, MARGIN, 176, INNER_W, ALIGN_LEFT, buf, WARN, BG);
  lastDate[0] = '\0';
}

void drawClock(bool timeValid, bool wifiUp, const char *notice) {
  if (!timeValid) {
    if (strcmp(lastDate, "-")) {
      strlcpy(lastDate, "-", sizeof(lastDate));
      lcd->drawText(FONT_M, MARGIN, DATE_Y, INNER_W, ALIGN_CENTER, "----.--.--", DIM, BG);
      lcd->drawText(FONT_L, MARGIN, TIME_Y, INNER_W, ALIGN_CENTER, "--:--", DIM, BG);
      lastHm[0] = lastSec[0] = '\0';
    }
    drawStatus(wifiUp ? "시간 동기화 중" : "Wi-Fi 연결 중", WARN);
    return;
  }

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  char date[40];
  snprintf(date, sizeof(date), "%04d.%02d.%02d %s요일", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           WEEKDAYS[t.tm_wday % 7]);
  if (strcmp(date, lastDate)) {
    strlcpy(lastDate, date, sizeof(lastDate));
    lcd->drawText(FONT_M, MARGIN, DATE_Y, INNER_W, ALIGN_CENTER, date, ACCENT, BG);
    lastHm[0] = lastSec[0] = '\0';
  }

  char hm[8], sec[4];
  snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
  snprintf(sec, sizeof(sec), "%02d", t.tm_sec);
  const int16_t hmW = lcd->textWidth(FONT_L, "00:00");
  const int16_t secW = lcd->textWidth(FONT_M, "00") + 2;
  const int16_t x = (Lcd::W - hmW - 6 - secW) / 2;
  if (strcmp(hm, lastHm)) {
    strlcpy(lastHm, hm, sizeof(lastHm));
    lcd->drawText(FONT_L, x, TIME_Y, hmW, ALIGN_LEFT, hm, FG, BG);
  }
  if (strcmp(sec, lastSec)) {
    strlcpy(lastSec, sec, sizeof(lastSec));
    const int16_t secY = TIME_Y + FONT_L.ascent - FONT_M.ascent;
    lcd->drawText(FONT_M, x + hmW + 6, secY, secW, ALIGN_LEFT, sec, DIM, BG);
  }
  drawStatus(wifiUp ? notice : "Wi-Fi 연결 끊김", WARN);
}

void drawUsage(const UsageState &state, bool configured) {
  char status[40];
  const bool warn = usage::headerText(state, configured, millis(), status, sizeof(status));
  const uint16_t statusColor = warn ? WARN : DIM;

  char key[sizeof(lastUsageKey)];
  int len = snprintf(key, sizeof(key), "%s|%u|%u", status, statusColor, state.count);
  for (uint8_t i = 0; i < state.count && len > 0 && len < static_cast<int>(sizeof(key)); i++) {
    len += snprintf(key + len, sizeof(key) - len, "|%s|%s|%d|%s", state.items[i].label, state.items[i].value,
                    state.items[i].pct, state.items[i].resets);
  }
  if (!strcmp(key, lastUsageKey)) return;
  strlcpy(lastUsageKey, key, sizeof(lastUsageKey));

  const char *title = "최근 7일 사용량";
  const int16_t titleW = lcd->textWidth(FONT_S, title);
  const bool showTitle = titleW + HEADER_GAP + lcd->textWidth(FONT_S, status) <= INNER_W;
  const int16_t leftW = showTitle ? titleW : 0;
  if (showTitle) lcd->drawText(FONT_S, MARGIN, HEADER_Y, leftW, ALIGN_LEFT, title, DIM, BG);
  lcd->drawText(FONT_S, MARGIN + leftW, HEADER_Y, INNER_W - leftW, ALIGN_RIGHT, status, statusColor, BG);

  lcd->fillRect(0, ITEMS_Y, Lcd::W, ITEMS_H, BG);
  if (!state.haveData || !state.count) return;

  const bool compact = state.count > 2;
  const Font &font = compact ? FONT_S : FONT_M;
  const int16_t slotH = ITEMS_H / (compact ? 3 : 2);
  const int16_t labelW = INNER_W * 2 / 5;
  for (uint8_t i = 0; i < state.count; i++) {
    const UsageItem &item = state.items[i];
    int16_t y = ITEMS_Y + i * slotH;
    lcd->drawText(font, MARGIN, y, labelW, ALIGN_LEFT, item.label, FG, BG);
    lcd->drawText(font, MARGIN + labelW, y, INNER_W - labelW, ALIGN_RIGHT, item.value, FG, BG);
    if (compact) {
      if (item.pct >= 0) drawBar(y + Lcd::lineHeight(font) + 3, INNER_W, 6, item.pct);
      continue;
    }
    // Second row: bar on the left, reset time right-aligned in small text.
    const int16_t rowY = y + Lcd::lineHeight(font) + 2;
    int16_t resetsW = 0;
    if (item.resets[0]) {
      char resets[32];
      snprintf(resets, sizeof(resets), "초기화 %s", item.resets);
      resetsW = lcd->textWidth(FONT_S, resets);
      lcd->drawText(FONT_S, MARGIN + INNER_W - resetsW, rowY, resetsW, ALIGN_RIGHT, resets, DIM, BG);
    }
    const int16_t barH = 8;
    const int16_t barW = resetsW ? INNER_W - resetsW - RESETS_GAP : INNER_W;
    if (item.pct >= 0) drawBar(rowY + (Lcd::lineHeight(FONT_S) - barH) / 2, barW, barH, item.pct);
  }
}

}  // namespace ui
