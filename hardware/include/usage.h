#pragma once
#include <Arduino.h>
#include <time.h>

#include "settings.h"

// Increase values in tenths of a percentage point, with two markers for "no value".
constexpr int16_t DELTA_MISSING = INT16_MIN;     // the relay did not send the field (1.2.0 or older)
constexpr int16_t DELTA_NO_SAMPLE = INT16_MIN + 1;  // the relay sent null: no comparison sample yet
constexpr uint8_t HOURLY_SLOTS = 24;

struct UsageItem {
  char label[17];
  char value[21];
  int8_t pct;
  char resets[12];
  int16_t delta1h;
  int16_t delta24h;
  int16_t hourly[HOURLY_SLOTS];  // oldest first, the last slot is the previous clock hour
};

enum class RelayStatus : uint8_t { Unknown, Ok, Stale, Error };

// Heap figures around the last HTTPS fetch; zero until the first one.
struct TlsStats {
  uint32_t heapBefore = 0;
  uint32_t maxBlockBefore = 0;
  uint32_t heapConnected = 0;
  uint32_t heapAfter = 0;
  int8_t mfln = -1;
  int sslError = 0;
};

struct UsageState {
  static constexpr uint8_t MAX_ITEMS = 3;
  bool haveData = false;
  bool lastOk = false;
  bool waitingForTime = false;
  uint32_t lastSuccessMs = 0;
  uint32_t lastAttemptMs = 0;
  char updated[32] = "";
  char period[8] = "";
  uint8_t count = 0;
  UsageItem items[MAX_ITEMS] = {};
  bool haveHourly = false;  // the last response carried hourly values
  RelayStatus relayStatus = RelayStatus::Unknown;
  char relayError[13] = "";
  String lastError;
  TlsStats tls;
};

namespace usage {

constexpr size_t MAX_BODY = 4096;
constexpr time_t VALID_EPOCH = 1700000000;

bool parse(const char *json, size_t len, UsageState &out);
// Writes "+1.4%", "-0.3%", the no-sample text or "--" for a DELTA_* value.
void deltaText(int16_t tenths, Lang lang, char *out, size_t n);
// Formats the usage header status into out; returns true when it should be drawn as a warning.
bool headerText(const UsageState &state, bool configured, uint32_t nowMs, Lang lang, char *out, size_t n);
// Fetches the relay response, adding ?hourly=1 when withHourly is set.
bool fetch(const Settings &s, UsageState &state, bool withHourly);

}  // namespace usage
