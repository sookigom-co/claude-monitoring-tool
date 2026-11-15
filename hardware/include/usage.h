#pragma once
#include <Arduino.h>
#include <time.h>

#include "settings.h"

struct UsageItem {
  char label[17];
  char value[21];
  int8_t pct;
  char resets[12];
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
  RelayStatus relayStatus = RelayStatus::Unknown;
  char relayError[13] = "";
  String lastError;
  TlsStats tls;
};

namespace usage {

constexpr size_t MAX_BODY = 4096;
constexpr time_t VALID_EPOCH = 1700000000;

bool parse(const char *json, size_t len, UsageState &out);
// Formats the usage header status into out; returns true when it should be drawn as a warning.
bool headerText(const UsageState &state, bool configured, uint32_t nowMs, Lang lang, char *out, size_t n);
bool fetch(const Settings &s, UsageState &state);

}  // namespace usage
