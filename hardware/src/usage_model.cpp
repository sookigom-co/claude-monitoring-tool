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

// Reads an increase field: a number in tenths, DELTA_NO_SAMPLE for null, DELTA_MISSING when absent or not a number.
int16_t readDelta(JsonVariantConst v) {
  if (v.isUnbound()) return DELTA_MISSING;
  if (v.isNull()) return DELTA_NO_SAMPLE;
  if (!v.is<float>() && !v.is<int>()) return DELTA_MISSING;
  const float tenths = constrain(v.as<float>() * 10, static_cast<float>(DELTA_NO_SAMPLE + 1), static_cast<float>(INT16_MAX));
  return static_cast<int16_t>(lroundf(tenths));
}

// Fills hourly right-aligned so a short array still ends at the previous hour; returns false when there is none.
bool readHourly(JsonVariantConst v, int16_t *out) {
  for (uint8_t i = 0; i < HOURLY_SLOTS; i++) out[i] = DELTA_NO_SAMPLE;
  JsonArrayConst arr = v.as<JsonArrayConst>();
  if (arr.isNull()) return false;
  const size_t n = arr.size();
  const size_t skip = n > HOURLY_SLOTS ? n - HOURLY_SLOTS : 0;
  uint8_t slot = static_cast<uint8_t>(HOURLY_SLOTS - (n - skip));
  size_t i = 0;
  for (JsonVariantConst value : arr) {
    if (i++ < skip) continue;
    const int16_t d = readDelta(value);
    out[slot++] = d == DELTA_MISSING ? DELTA_NO_SAMPLE : d;
  }
  return true;
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
  out.haveHourly = false;
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
    dst.delta1h = readDelta(item["delta_1h"]);
    dst.delta24h = readDelta(item["delta_24h"]);
    if (readHourly(item["hourly"], dst.hourly)) out.haveHourly = true;
  }
  return true;
}

void deltaText(int16_t tenths, Lang lang, char *out, size_t n) {
  if (tenths == DELTA_MISSING) {
    strlcpy(out, "--", n);
  } else if (tenths == DELTA_NO_SAMPLE) {
    i18n::copy(lang, Msg::NoSample, out, n);
  } else {
    const unsigned mag = static_cast<unsigned>(tenths < 0 ? -tenths : tenths);
    snprintf(out, n, "%c%u.%u%%", tenths < 0 ? '-' : '+', mag / 10, mag % 10);
  }
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
