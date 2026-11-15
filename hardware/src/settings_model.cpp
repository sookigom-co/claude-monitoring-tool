#include "settings.h"

namespace settings {
namespace {

const char *const TLS_MODE_NAMES[] = {"ca", "fingerprint", "insecure"};
const char *const LAYOUT_NAMES[] = {"A", "B", "C"};
const char *const LAYOUT_MODE_NAMES[] = {"fixed", "cycle"};

template <typename E, size_t N>
bool parseName(const char *name, const char *const (&names)[N], E &out) {
  if (!name) return false;
  for (uint8_t i = 0; i < N; i++) {
    if (!strcmp(name, names[i])) {
      out = static_cast<E>(i);
      return true;
    }
  }
  return false;
}

bool startsWith(const String &s, const char *prefix) { return !strncmp(s.c_str(), prefix, strlen(prefix)); }

}  // namespace

const char *tlsModeName(TlsMode mode) { return TLS_MODE_NAMES[static_cast<uint8_t>(mode)]; }

bool parseTlsMode(const char *name, TlsMode &out) { return parseName(name, TLS_MODE_NAMES, out); }

const char *layoutName(Layout layout) { return LAYOUT_NAMES[static_cast<uint8_t>(layout)]; }

bool parseLayout(const char *name, Layout &out) { return parseName(name, LAYOUT_NAMES, out); }

const char *layoutModeName(LayoutMode mode) { return LAYOUT_MODE_NAMES[static_cast<uint8_t>(mode)]; }

bool parseLayoutMode(const char *name, LayoutMode &out) { return parseName(name, LAYOUT_MODE_NAMES, out); }

bool validCycleSec(long sec) { return sec >= MIN_CYCLE_SEC && sec <= MAX_CYCLE_SEC; }

void fromJson(JsonVariantConst doc, Settings &s) {
  s.adminPass = doc["admin_pass"] | "";
  s.apPass = doc["ap_pass"] | "";
  s.usageUrl = doc["usage_url"] | "";
  s.usageToken = doc["usage_token"] | "";
  s.tlsFingerprint = doc["tls_fp"] | "";
  // Settings saved before tls_mode existed keep their pinned fingerprint; everything else validates against the CA roots.
  if (!parseTlsMode(doc["tls_mode"].as<const char *>(), s.tlsMode)) {
    s.tlsMode = s.tlsFingerprint.length() ? TlsMode::Fingerprint : TlsMode::Ca;
  }
  s.tz = doc["tz"] | "KST-9";
  s.brightness = doc["brightness"] | 80;
  s.refreshMin = doc["refresh_min"] | 10;
  // Files from 0.1.x have no display keys; missing or invalid values fall back to the defaults.
  if (!parseLayout(doc["layout"].as<const char *>(), s.layout)) s.layout = Layout::C;
  if (!parseLayoutMode(doc["layout_mode"].as<const char *>(), s.layoutMode)) s.layoutMode = LayoutMode::Fixed;
  const long cycle = doc["cycle_sec"] | static_cast<long>(DEFAULT_CYCLE_SEC);
  s.cycleSec = validCycleSec(cycle) ? static_cast<uint16_t>(cycle) : DEFAULT_CYCLE_SEC;
}

void toJson(const Settings &s, JsonVariant doc) {
  doc["admin_pass"] = s.adminPass.c_str();
  doc["ap_pass"] = s.apPass.c_str();
  doc["usage_url"] = s.usageUrl.c_str();
  doc["usage_token"] = s.usageToken.c_str();
  doc["tls_fp"] = s.tlsFingerprint.c_str();
  doc["tls_mode"] = tlsModeName(s.tlsMode);
  doc["tz"] = s.tz.c_str();
  doc["brightness"] = s.brightness;
  doc["refresh_min"] = s.refreshMin;
  doc["layout"] = layoutName(s.layout);
  doc["layout_mode"] = layoutModeName(s.layoutMode);
  doc["cycle_sec"] = s.cycleSec;
}

bool isHttps(const Settings &s) { return startsWith(s.usageUrl, "https://"); }

bool needsValidTime(const Settings &s) { return isHttps(s) && s.tlsMode == TlsMode::Ca; }

TokenWarning tokenWarning(const Settings &s) {
  if (!s.usageToken.length()) return TokenWarning::None;
  if (startsWith(s.usageUrl, "http://")) return TokenWarning::Plaintext;
  if (isHttps(s) && s.tlsMode == TlsMode::Insecure) return TokenWarning::NoCertCheck;
  return TokenWarning::None;
}

}  // namespace settings
