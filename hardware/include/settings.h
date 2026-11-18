#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class TlsMode : uint8_t { Ca, Fingerprint, Insecure };

enum class TokenWarning : uint8_t { None, NoCertCheck, Plaintext };

// Screen designs (SOO-618, SOO-655): A concentric rings, B clock over gauges, C half rings, D usage-only bars.
enum class Layout : uint8_t { A, B, C, D };
constexpr uint8_t LAYOUT_COUNT = 4;

enum class LayoutMode : uint8_t { Fixed, Cycle };

// Language of the web pages and the clock screen.
enum class Lang : uint8_t { En, Ko };
constexpr uint8_t LANG_COUNT = 2;

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
  Layout layout = Layout::C;
  LayoutMode layoutMode = LayoutMode::Fixed;
  uint16_t cycleSec = 15;
  Lang lang = Lang::Ko;
};

namespace settings {

constexpr size_t MIN_ADMIN_PASS = 8;
constexpr uint16_t MIN_CYCLE_SEC = 5;
constexpr uint16_t MAX_CYCLE_SEC = 300;
constexpr uint16_t DEFAULT_CYCLE_SEC = 15;

bool begin(Settings &s);
bool save(const Settings &s);
void factoryReset();
String randomPassword(size_t len);
bool validTz(const String &tz);
bool validFingerprint(const String &fp);

const char *tlsModeName(TlsMode mode);
bool parseTlsMode(const char *name, TlsMode &out);
const char *layoutName(Layout layout);
bool parseLayout(const char *name, Layout &out);
const char *layoutModeName(LayoutMode mode);
bool parseLayoutMode(const char *name, LayoutMode &out);
bool validCycleSec(long sec);
const char *langName(Lang lang);
bool parseLang(const char *name, Lang &out);
void fromJson(JsonVariantConst doc, Settings &s);
void toJson(const Settings &s, JsonVariant doc);
bool isHttps(const Settings &s);
// True when the usage fetch validates against the built-in roots and so needs a synced clock.
bool needsValidTime(const Settings &s);
TokenWarning tokenWarning(const Settings &s);

}  // namespace settings
