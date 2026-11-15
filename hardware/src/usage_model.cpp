#include <ArduinoJson.h>

#include "i18n.h"
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

void formatAge(uint32_t ms, Lang lang, char *out, size_t n) {
  const unsigned long min = ms / 60000;
  if (min < 60) snprintf_P(out, n, i18n::text(lang, Msg::MinutesAgo), min);
  else if (min < 48 * 60) snprintf_P(out, n, i18n::text(lang, Msg::HoursAgo), min / 60);
  else snprintf_P(out, n, i18n::text(lang, Msg::DaysAgo), min / 1440);
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

bool headerText(const UsageState &state, bool configured, uint32_t nowMs, Lang lang, char *out, size_t n) {
  if (!configured) {
    i18n::copy(lang, Msg::NoUrl, out, n);
    return false;
  }
  if (state.waitingForTime) {
    i18n::copy(lang, Msg::WaitingForTime, out, n);
    return false;
  }
  if (!state.haveData) {
    i18n::copy(lang, state.lastAttemptMs ? Msg::Failed : Msg::Loading, out, n);
    return state.lastAttemptMs != 0;
  }
  char age[20];
  formatAge(nowMs - state.lastSuccessMs, lang, age, sizeof(age));
  const char *hhmm = strlen(state.updated) >= 16 ? state.updated + 11 : nullptr;
  if (!state.lastOk) {
    snprintf_P(out, n, i18n::text(lang, Msg::FailedAge), age);
    return true;
  }
  if (state.relayStatus == RelayStatus::Stale) {
    if (hhmm) snprintf_P(out, n, i18n::text(lang, Msg::StaleAsOf), hhmm);
    else i18n::copy(lang, Msg::Stale, out, n);
    return true;
  }
  if (state.relayStatus == RelayStatus::Error) {
    if (state.relayError[0]) snprintf_P(out, n, i18n::text(lang, Msg::ServerErrorCode), state.relayError);
    else i18n::copy(lang, Msg::ServerError, out, n);
    return true;
  }
  if (hhmm) snprintf_P(out, n, i18n::text(lang, Msg::AsOf), hhmm);
  else strlcpy(out, age, n);
  return false;
}

}  // namespace usage
