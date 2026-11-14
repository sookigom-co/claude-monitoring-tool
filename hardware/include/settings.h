#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class TlsMode : uint8_t { Ca, Fingerprint, Insecure };

enum class TokenWarning : uint8_t { None, NoCertCheck, Plaintext };

struct Settings {
  String adminPass;
  String apPass;
  String usageUrl;
  String usageToken;
  String tlsFingerprint;
  TlsMode tlsMode = TlsMode::Ca;
  String tz = "KST-9";
  uint8_t brightness = 80;
  uint16_t refreshMin = 10;
};

namespace settings {

constexpr size_t MIN_ADMIN_PASS = 8;

bool begin(Settings &s);
bool save(const Settings &s);
void factoryReset();
String randomPassword(size_t len);
bool validTz(const String &tz);
bool validFingerprint(const String &fp);

const char *tlsModeName(TlsMode mode);
bool parseTlsMode(const char *name, TlsMode &out);
void fromJson(JsonVariantConst doc, Settings &s);
void toJson(const Settings &s, JsonVariant doc);
bool isHttps(const Settings &s);
// True when the usage fetch validates against the built-in roots and so needs a synced clock.
bool needsValidTime(const Settings &s);
TokenWarning tokenWarning(const Settings &s);

}  // namespace settings
