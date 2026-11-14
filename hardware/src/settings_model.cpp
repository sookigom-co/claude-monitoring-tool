#include "settings.h"

namespace settings {
namespace {

const char *const TLS_MODE_NAMES[] = {"ca", "fingerprint", "insecure"};

bool startsWith(const String &s, const char *prefix) { return !strncmp(s.c_str(), prefix, strlen(prefix)); }

}  // namespace

const char *tlsModeName(TlsMode mode) { return TLS_MODE_NAMES[static_cast<uint8_t>(mode)]; }

bool parseTlsMode(const char *name, TlsMode &out) {
  if (!name) return false;
  for (uint8_t i = 0; i < sizeof(TLS_MODE_NAMES) / sizeof(TLS_MODE_NAMES[0]); i++) {
    if (!strcmp(name, TLS_MODE_NAMES[i])) {
      out = static_cast<TlsMode>(i);
      return true;
    }
  }
  return false;
}

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
