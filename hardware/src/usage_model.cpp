#include <ArduinoJson.h>

#include "usage.h"

namespace usage {
namespace {

bool isDigits(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

// Copies a "MM-DD HH:MM" reset time, leaving dst empty for anything else.
void copyResets(JsonVariantConst src, char *dst, size_t n) {
  dst[0] = '\0';
  const char *s = src.as<const char *>();
  if (!s || strlen(s) != 11 || s[2] != '-' || s[5] != ' ' || s[8] != ':') return;
  if (!isDigits(s, 2) || !isDigits(s + 3, 2) || !isDigits(s + 6, 2) || !isDigits(s + 9, 2)) return;
  strlcpy(dst, s, n);
}

RelayStatus toRelayStatus(const char *s) {
  if (!s) return RelayStatus::Unknown;
  if (!strcmp(s, "ok")) return RelayStatus::Ok;
  if (!strcmp(s, "stale")) return RelayStatus::Stale;
  if (!strcmp(s, "error")) return RelayStatus::Error;
  return RelayStatus::Unknown;
}

// Copies printable ASCII only, so the code renders with the bundled glyphs.
void copyPrintable(const char *src, char *dst, size_t n) {
  size_t j = 0;
  for (; src && *src && j + 1 < n; src++) {
    if (*src >= 0x20 && *src < 0x7F) dst[j++] = *src;
  }
  dst[j] = '\0';
}

void formatAge(uint32_t ms, char *out, size_t n) {
  uint32_t min = ms / 60000;
  if (min < 60) snprintf(out, n, "%lu분 전", static_cast<unsigned long>(min));
  else if (min < 48 * 60) snprintf(out, n, "%lu시간 전", static_cast<unsigned long>(min / 60));
  else snprintf(out, n, "%lu일 전", static_cast<unsigned long>(min / 1440));
}

}  // namespace

bool parse(const char *json, size_t len, UsageState &out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  JsonArrayConst items = doc["items"];
  if (items.isNull()) return false;
  strlcpy(out.updated, doc["updated"] | "", sizeof(out.updated));
  strlcpy(out.period, doc["period"] | "", sizeof(out.period));
  out.relayStatus = toRelayStatus(doc["status"].as<const char *>());
  copyPrintable(doc["error"].as<const char *>(), out.relayError, sizeof(out.relayError));
  out.count = 0;
  for (JsonObjectConst item : items) {
    if (out.count >= UsageState::MAX_ITEMS) break;
    UsageItem &dst = out.items[out.count++];
    strlcpy(dst.label, item["label"] | "", sizeof(dst.label));
    strlcpy(dst.value, item["value"] | "", sizeof(dst.value));
    JsonVariantConst pct = item["pct"];
    if (pct.is<float>() || pct.is<int>()) {
      float v = pct.as<float>();
      dst.pct = static_cast<int8_t>(constrain(lroundf(v), 0L, 100L));
    } else {
      dst.pct = -1;
    }
    copyResets(item["resets"], dst.resets, sizeof(dst.resets));
  }
  return true;
}

bool headerText(const UsageState &state, bool configured, uint32_t nowMs, char *out, size_t n) {
  if (!configured) {
    strlcpy(out, "URL 미설정", n);
    return false;
  }
  if (state.waitingForTime) {
    strlcpy(out, "시각 동기화 대기", n);
    return false;
  }
  if (!state.haveData) {
    strlcpy(out, state.lastAttemptMs ? "실패" : "불러오는 중", n);
    return state.lastAttemptMs != 0;
  }
  char age[20];
  formatAge(nowMs - state.lastSuccessMs, age, sizeof(age));
  const bool haveUpdated = strlen(state.updated) >= 16;
  if (!state.lastOk) {
    snprintf(out, n, "실패 · %s", age);
    return true;
  }
  if (state.relayStatus == RelayStatus::Stale) {
    if (haveUpdated) snprintf(out, n, "지연 · %.5s 기준", state.updated + 11);
    else strlcpy(out, "지연", n);
    return true;
  }
  if (state.relayStatus == RelayStatus::Error) {
    if (state.relayError[0]) snprintf(out, n, "서버 오류 %s", state.relayError);
    else strlcpy(out, "서버 오류", n);
    return true;
  }
  if (haveUpdated) snprintf(out, n, "%.5s 기준", state.updated + 11);
  else strlcpy(out, age, n);
  return false;
}

}  // namespace usage
