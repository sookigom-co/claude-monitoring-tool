#include "settings.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace settings {
namespace {

const char *PATH = "/settings.json";
const char *TMP_PATH = "/settings.tmp";

bool mount() {
  if (LittleFS.begin()) return true;
  LittleFS.format();
  return LittleFS.begin();
}

}  // namespace

String randomPassword(size_t len) {
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyzACDEFGHJKLMNPQRTUVWXY34679";
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++) out += alphabet[ESP.random() % (sizeof(alphabet) - 1)];
  return out;
}

bool validTz(const String &tz) {
  if (tz.length() == 0 || tz.length() > 48) return false;
  for (char c : tz) {
    if (!isalnum(static_cast<unsigned char>(c)) && !strchr("+-,./:<>", c)) return false;
  }
  return true;
}

bool validFingerprint(const String &fp) {
  if (fp.length() == 0) return true;
  int hex = 0;
  for (char c : fp) {
    if (isxdigit(static_cast<unsigned char>(c))) hex++;
    else if (c != ':' && c != ' ') return false;
  }
  return hex == 40;
}

bool begin(Settings &s) {
  bool ok = mount();
  if (ok) {
    File f = LittleFS.open(PATH, "r");
    if (f) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) fromJson(doc.as<JsonVariantConst>(), s);
      f.close();
    }
  }
  bool changed = false;
  if (s.adminPass.length() < MIN_ADMIN_PASS) {
    s.adminPass = randomPassword(10);
    changed = true;
  }
  if (s.apPass.length() < 8) {
    s.apPass = randomPassword(8);
    changed = true;
  }
  if (!validTz(s.tz)) s.tz = "KST-9";
  if (s.brightness > 100) s.brightness = 100;
  if (s.refreshMin < 1 || s.refreshMin > 1440) s.refreshMin = 10;
  if (changed && ok) save(s);
  return ok;
}

bool save(const Settings &s) {
  JsonDocument doc;
  toJson(s, doc.to<JsonVariant>());
  File f = LittleFS.open(TMP_PATH, "w");
  if (!f) return false;
  size_t written = serializeJson(doc, f);
  f.close();
  if (!written) return false;
  LittleFS.remove(PATH);
  return LittleFS.rename(TMP_PATH, PATH);
}

void factoryReset() {
  LittleFS.end();
  LittleFS.format();
}

}  // namespace settings
