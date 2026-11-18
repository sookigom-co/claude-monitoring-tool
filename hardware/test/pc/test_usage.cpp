// Host tests for usage::parse, usage::headerText, the settings model, the screen text tables and the layout widths.
#include <cstdio>
#include <cstring>

#include "fonts.h"
#include "i18n.h"
#include "settings.h"
#include "usage.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    checks++;                                                          \
    if (!(cond)) {                                                     \
      failures++;                                                      \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                  \
  } while (0)

#define CHECK_STR(actual, expected)                                                                    \
  do {                                                                                                 \
    checks++;                                                                                          \
    if (std::strcmp((actual), (expected))) {                                                           \
      failures++;                                                                                      \
      std::printf("  FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (actual), (expected));       \
    }                                                                                                  \
  } while (0)

static bool parseStr(const char *json, UsageState &st) {
  bool ok = usage::parse(json, std::strlen(json), st);
  if (ok) {
    st.haveData = true;
    st.lastOk = true;
    st.lastSuccessMs = 0;
    st.lastAttemptMs = 1;
  }
  return ok;
}

static bool header(const UsageState &st, char *out, size_t n) {
  return usage::headerText(st, true, 5 * 60000, Lang::Ko, out, n);
}

static void caseDraft() {
  std::puts("draft contract: values, pct null, clamp");
  UsageState st;
  CHECK(parseStr(R"({"updated":"2026-11-13T10:00:00+09:00","period":"7d","items":[
      {"label":"Claude","value":"1.23M tok","pct":42},{"label":"Fable","value":"$12.40","pct":null}]})", st));
  CHECK(st.count == 2);
  CHECK_STR(st.items[0].value, "1.23M tok");
  CHECK(st.items[0].pct == 42);
  CHECK(st.items[1].pct == -1);
  CHECK_STR(st.items[0].resets, "");
  CHECK(st.relayStatus == RelayStatus::Unknown);
  char h[40];
  CHECK(!header(st, h, sizeof(h)));
  CHECK_STR(h, "10:00 기준");

  CHECK(parseStr(R"({"items":[{"label":"A","value":"x","pct":140.6},{"label":"B","value":"y","pct":-3}]})", st));
  CHECK(st.items[0].pct == 100);
  CHECK(st.items[1].pct == 0);
  CHECK(!header(st, h, sizeof(h)));
  CHECK_STR(h, "5분 전");
}

static void caseRejectsAndLimits() {
  std::puts("invalid JSON, missing items, more than 3 items, long strings");
  UsageState st;
  CHECK(!parseStr("{not json", st));
  CHECK(!parseStr(R"({"updated":"x"})", st));
  CHECK(parseStr(R"({"items":[{"label":"1","value":"a"},{"label":"2","value":"b"},{"label":"3","value":"c"},
      {"label":"4","value":"d"}]})", st));
  CHECK(st.count == 3);
  CHECK(parseStr(R"({"items":[{"label":"ABCDEFGHIJKLMNOPQRSTUVWXYZ","value":"0123456789012345678901234"}]})", st));
  CHECK_STR(st.items[0].label, "ABCDEFGHIJKLMNOP");
  CHECK_STR(st.items[0].value, "01234567890123456789");
}

static void caseRelayOk() {
  std::puts("relay status ok with resets");
  UsageState st;
  CHECK(parseStr(R"({"updated": "2026-11-13T19:30:16+09:00", "period": "7d", "items": [
      {"label": "Claude", "value": "42%", "pct": 42, "resets": "11-18 09:00"},
      {"label": "Fable", "value": "17%", "pct": 17, "resets": "11-18 09:00"}], "status": "ok"})", st));
  CHECK(st.relayStatus == RelayStatus::Ok);
  CHECK(st.count == 2);
  CHECK(st.items[0].pct == 42);
  CHECK(st.items[1].pct == 17);
  CHECK_STR(st.items[0].resets, "11-18 09:00");
  CHECK_STR(st.items[1].resets, "11-18 09:00");
  CHECK_STR(st.relayError, "");
  char h[40];
  CHECK(!header(st, h, sizeof(h)));
  CHECK_STR(h, "19:30 기준");
}

static void caseRelayStale() {
  std::puts("relay status stale keeps previous values");
  UsageState st;
  CHECK(parseStr(R"({"updated": "2026-11-13T19:30:16+09:00", "period": "7d", "items": [
      {"label": "Claude", "value": "42%", "pct": 42, "resets": "11-18 09:00"},
      {"label": "Fable", "value": "17%", "pct": 17, "resets": "11-18 09:00"}],
      "status": "stale", "error": "refresh_failed"})", st));
  CHECK(st.relayStatus == RelayStatus::Stale);
  CHECK_STR(st.relayError, "refresh_fail");
  CHECK(st.items[0].pct == 42);
  char h[40];
  CHECK(header(st, h, sizeof(h)));
  CHECK_STR(h, "지연 · 19:30 기준");

  st.updated[0] = '\0';
  CHECK(header(st, h, sizeof(h)));
  CHECK_STR(h, "지연");

  st.lastOk = false;
  CHECK(header(st, h, sizeof(h)));
  CHECK_STR(h, "실패 · 5분 전");
}

static void caseRelayError() {
  std::puts("relay status error with updated null");
  UsageState st;
  std::strcpy(st.updated, "2026-11-13T08:00:00+09:00");
  CHECK(parseStr(R"({"updated": null, "period": "7d", "items": [
      {"label": "Claude", "value": "n/a", "pct": null},
      {"label": "Fable", "value": "n/a", "pct": null}],
      "status": "error", "error": "usage_http_500"})", st));
  CHECK(st.relayStatus == RelayStatus::Error);
  CHECK_STR(st.updated, "");
  CHECK_STR(st.relayError, "usage_http_5");
  CHECK(st.count == 2);
  CHECK_STR(st.items[0].value, "n/a");
  CHECK(st.items[0].pct == -1);
  CHECK(st.items[1].pct == -1);
  CHECK_STR(st.items[0].resets, "");
  char h[40];
  CHECK(header(st, h, sizeof(h)));
  CHECK_STR(h, "서버 오류 usage_http_5");

  CHECK(parseStr(R"({"updated":null,"items":[],"status":"error","error":"éx\u0001y"})", st));
  CHECK_STR(st.relayError, "xy");
  CHECK(parseStr(R"({"updated":null,"items":[],"status":"error"})", st));
  CHECK(header(st, h, sizeof(h)));
  CHECK_STR(h, "서버 오류");
}

static void caseResetsAbsentOrMalformed() {
  std::puts("resets absent or malformed is omitted");
  UsageState st;
  CHECK(parseStr(R"({"updated": "2026-11-13T19:30:16+09:00", "period": "7d", "items": [
      {"label": "Claude", "value": "5%", "pct": 5},
      {"label": "Fable", "value": "n/a", "pct": null}], "status": "ok"})", st));
  CHECK_STR(st.items[0].resets, "");
  CHECK_STR(st.items[1].resets, "");
  CHECK(parseStr(R"({"items":[{"label":"A","value":"1","resets":"2026-11-18T09:00"},
      {"label":"B","value":"2","resets":12},{"label":"C","value":"3","resets":"1a-18 09:00"}]})", st));
  CHECK_STR(st.items[0].resets, "");
  CHECK_STR(st.items[1].resets, "");
  CHECK_STR(st.items[2].resets, "");
  CHECK(parseStr(R"({"status":"weird","items":[{"label":"A","value":"1"}]})", st));
  CHECK(st.relayStatus == RelayStatus::Unknown);
}

static void caseTlsModeNames() {
  std::puts("TLS mode names round-trip, unknown names rejected");
  const TlsMode modes[] = {TlsMode::Ca, TlsMode::Fingerprint, TlsMode::Insecure};
  const char *names[] = {"ca", "fingerprint", "insecure"};
  for (int i = 0; i < 3; i++) {
    CHECK_STR(settings::tlsModeName(modes[i]), names[i]);
    TlsMode m = TlsMode::Insecure;
    CHECK(settings::parseTlsMode(names[i], m));
    CHECK(m == modes[i]);
  }
  TlsMode m = TlsMode::Fingerprint;
  CHECK(!settings::parseTlsMode("CA", m));
  CHECK(!settings::parseTlsMode("", m));
  CHECK(!settings::parseTlsMode(nullptr, m));
  CHECK(m == TlsMode::Fingerprint);
}

static Settings loadSettings(const char *json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  Settings s;
  settings::fromJson(doc.as<JsonVariantConst>(), s);
  return s;
}

static void caseSettingsSerialization() {
  std::puts("settings JSON: tls_mode saved, 0.1.1 files migrated");
  Settings s = loadSettings(R"({"usage_url":"https://clock.example.com/clock/usage","usage_token":"t0k",
      "tls_fp":"","tz":"KST-9","brightness":80,"refresh_min":10})");
  CHECK(s.tlsMode == TlsMode::Ca);
  s = loadSettings(R"({"usage_url":"https://x/clock/usage","tls_fp":"AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD"})");
  CHECK(s.tlsMode == TlsMode::Fingerprint);
  s = loadSettings(R"({"tls_mode":"bogus","tls_fp":""})");
  CHECK(s.tlsMode == TlsMode::Ca);
  s = loadSettings(R"({"tls_mode":"insecure","tls_fp":"AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD"})");
  CHECK(s.tlsMode == TlsMode::Insecure);
  s = loadSettings("{}");
  CHECK(s.tlsMode == TlsMode::Ca);
  CHECK_STR(s.tz.c_str(), "KST-9");
  CHECK(s.brightness == 80);
  CHECK(s.refreshMin == 10);

  const TlsMode modes[] = {TlsMode::Ca, TlsMode::Fingerprint, TlsMode::Insecure};
  for (TlsMode mode : modes) {
    Settings in;
    in.adminPass = "admin-pass";
    in.apPass = "ap-pass1";
    in.usageUrl = "https://clock.example.com/clock/usage";
    in.usageToken = "secret-token";
    in.tlsFingerprint = mode == TlsMode::Fingerprint ? "0123456789abcdef0123456789abcdef01234567" : "";
    in.tlsMode = mode;
    in.tz = "JST-9";
    in.brightness = 55;
    in.refreshMin = 7;
    JsonDocument doc;
    settings::toJson(in, doc.to<JsonVariant>());
    std::string raw;
    serializeJson(doc, raw);
    CHECK_STR(doc["tls_mode"].as<const char *>(), settings::tlsModeName(mode));
    Settings out = loadSettings(raw.c_str());
    CHECK(out.tlsMode == mode);
    CHECK_STR(out.usageUrl.c_str(), in.usageUrl.c_str());
    CHECK_STR(out.usageToken.c_str(), in.usageToken.c_str());
    CHECK_STR(out.tlsFingerprint.c_str(), in.tlsFingerprint.c_str());
    CHECK_STR(out.tz.c_str(), "JST-9");
    CHECK(out.brightness == 55 && out.refreshMin == 7);
  }
}

static void caseTlsPolicy() {
  std::puts("TLS policy: clock wait and token warnings");
  Settings s;
  s.usageUrl = "https://clock.example.com/clock/usage";
  s.tlsMode = TlsMode::Ca;
  CHECK(settings::isHttps(s));
  CHECK(settings::needsValidTime(s));
  CHECK(settings::tokenWarning(s) == TokenWarning::None);
  s.usageToken = "secret-token";
  CHECK(settings::tokenWarning(s) == TokenWarning::None);
  s.tlsMode = TlsMode::Fingerprint;
  CHECK(!settings::needsValidTime(s));
  CHECK(settings::tokenWarning(s) == TokenWarning::None);
  s.tlsMode = TlsMode::Insecure;
  CHECK(!settings::needsValidTime(s));
  CHECK(settings::tokenWarning(s) == TokenWarning::NoCertCheck);
  s.usageToken = "";
  CHECK(settings::tokenWarning(s) == TokenWarning::None);
  s.usageUrl = "http://10.0.0.5:8787/clock/usage";
  s.tlsMode = TlsMode::Ca;
  CHECK(!settings::isHttps(s));
  CHECK(!settings::needsValidTime(s));
  CHECK(settings::tokenWarning(s) == TokenWarning::None);
  s.usageToken = "secret-token";
  CHECK(settings::tokenWarning(s) == TokenWarning::Plaintext);

  UsageState st;
  st.waitingForTime = true;
  char h[40];
  CHECK(!usage::headerText(st, true, 0, Lang::Ko, h, sizeof(h)));
  CHECK_STR(h, "시각 동기화 대기");
  CHECK(!usage::headerText(st, false, 0, Lang::Ko, h, sizeof(h)));
  CHECK_STR(h, "URL 미설정");
}

static uint16_t nextCp(const char *&s) {
  uint8_t c = *s++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) return ((c & 0x1F) << 6) | (*s++ & 0x3F);
  uint16_t cp = (c & 0x0F) << 12;
  cp |= (*s++ & 0x3F) << 6;
  cp |= (*s++ & 0x3F);
  return cp;
}

static int textWidth(const Font &f, const char *s, int *missing = nullptr) {
  int w = 0;
  while (*s) {
    uint16_t cp = nextCp(s);
    bool found = false;
    for (uint16_t i = 0; i < f.count; i++) {
      if (f.glyphs[i].code == cp) {
        w += f.glyphs[i].adv;
        found = true;
        break;
      }
    }
    if (!found && missing) (*missing)++;
  }
  return w;
}

static void caseLayout() {
  std::puts("0.2.0 screens: glyph coverage and widths");
  int missing = 0;
  const char *weekdays[] = {"일", "월", "화", "수", "목", "금", "토"};
  int widest = 0;
  for (int m = 1; m <= 12; m++) {
    for (const char *wd : weekdays) {
      char date[40];
      std::snprintf(date, sizeof(date), "%d월 %d일 %s요일", m, 28, wd);
      int w = textWidth(FONT_S, date, &missing);
      if (w > widest) widest = w;
    }
  }
  std::printf("    widest date %dpx (box 150)\n", widest);
  CHECK(widest <= 150);
  const char *statuses[] = {"초기화 12-31 23:59", "초기화 12-31 23:59 · 23:59 기준", "지연 · 19:30 기준", "서버 오류 credentials_",
                            "실패 · 47시간 전", "불러오는 중", "시각 동기화 대기", "인증서 검증 안 함", "토큰 평문 전송",
                            "Wi-Fi 연결 끊김", "URL 미설정", "시간 동기화 중", "Wi-Fi 연결 중"};
  for (const char *t : statuses) textWidth(FONT_XS, t, &missing);
  for (const char *t : {"Wi-Fi 연결 중", "시간 동기화 중", "Claude", "Fable"}) textWidth(FONT_S, t, &missing);
  for (const char *t : {"0%", "100%", "--"}) {
    textWidth(FONT_P, t, &missing);
    textWidth(FONT_PS, t, &missing);
  }
  textWidth(FONT_L, "00:00--:--", &missing);
  CHECK(missing == 0);
  // A's reset line sits in a 108px box inside the inner ring; B's combined line in 224px.
  CHECK(textWidth(FONT_XS, "초기화 12-31 23:59") <= 108);
  CHECK(textWidth(FONT_XS, "초기화 12-31 23:59 · 23:59 기준") <= 224);
  CHECK(textWidth(FONT_P, "100%") <= 60);
}

static void caseDisplaySettings() {
  std::puts("settings JSON: layout keys, defaults, migration from 0.1.x");
  // Exactly the keys 0.1.2 writes (settings::toJson in that release).
  Settings s = loadSettings(R"({"admin_pass":"admin-pass","ap_pass":"ap-pass1","usage_url":"https://x/clock/usage",
      "usage_token":"t","tls_fp":"","tls_mode":"ca","tz":"KST-9","brightness":55,"refresh_min":7})");
  CHECK(s.layout == Layout::C);
  CHECK(s.layoutMode == LayoutMode::Fixed);
  CHECK(s.cycleSec == 15);
  CHECK(s.brightness == 55 && s.refreshMin == 7);
  CHECK_STR(s.usageUrl.c_str(), "https://x/clock/usage");

  s = loadSettings(R"({"layout":"Z","layout_mode":"spin","cycle_sec":4})");
  CHECK(s.layout == Layout::C && s.layoutMode == LayoutMode::Fixed && s.cycleSec == 15);
  s = loadSettings(R"({"layout":"a","cycle_sec":301})");
  CHECK(s.layout == Layout::C && s.cycleSec == 15);
  s = loadSettings(R"({"layout":"B","cycle_sec":"20"})");
  CHECK(s.layout == Layout::B && s.cycleSec == 15);
  s = loadSettings(R"({"layout":"A","layout_mode":"cycle","cycle_sec":5})");
  CHECK(s.layout == Layout::A && s.layoutMode == LayoutMode::Cycle && s.cycleSec == 5);
  s = loadSettings(R"({"cycle_sec":300})");
  CHECK(s.cycleSec == 300);

  CHECK(!settings::validCycleSec(4) && settings::validCycleSec(5) && settings::validCycleSec(300) &&
        !settings::validCycleSec(301));
  Layout l;
  CHECK(!settings::parseLayout(nullptr, l) && !settings::parseLayout("", l) && !settings::parseLayout("F", l));
  CHECK(settings::parseLayout("D", l) && l == Layout::D);
  CHECK(settings::parseLayout("E", l) && l == Layout::E);
  for (Layout layout : {Layout::A, Layout::B, Layout::C, Layout::D, Layout::E}) {
    for (LayoutMode mode : {LayoutMode::Fixed, LayoutMode::Cycle}) {
      Settings in;
      in.layout = layout;
      in.layoutMode = mode;
      in.cycleSec = 42;
      JsonDocument doc;
      settings::toJson(in, doc.to<JsonVariant>());
      std::string raw;
      serializeJson(doc, raw);
      Settings out = loadSettings(raw.c_str());
      CHECK(out.layout == layout && out.layoutMode == mode && out.cycleSec == 42);
    }
  }
}

static void caseLayoutDSettings() {
  std::puts("settings JSON: layout D, 0.3.0 files, invalid values");
  // Exactly the keys 0.3.0 writes (settings::toJson in that release).
  for (const char *layout : {"A", "B", "C"}) {
    char json[400];
    std::snprintf(json, sizeof(json), R"({"admin_pass":"admin-pass","ap_pass":"ap-pass1","usage_url":"https://x/clock/usage",
        "usage_token":"t","tls_fp":"","tls_mode":"ca","tz":"KST-9","brightness":55,"refresh_min":7,
        "layout":"%s","layout_mode":"cycle","cycle_sec":20,"lang":"en"})", layout);
    Settings s = loadSettings(json);
    CHECK_STR(settings::layoutName(s.layout), layout);
    CHECK(s.layoutMode == LayoutMode::Cycle && s.cycleSec == 20 && s.lang == Lang::En && s.brightness == 55);
  }
  CHECK(loadSettings(R"({"layout":"D"})").layout == Layout::D);
  CHECK(loadSettings(R"({"layout":"D","layout_mode":"cycle"})").layoutMode == LayoutMode::Cycle);
  for (const char *bad : {R"({"layout":"d"})", R"({"layout":"F"})", R"({"layout":"DD"})", R"({"layout":3})", R"({"layout":null})"}) {
    CHECK(loadSettings(bad).layout == Layout::C);
  }
  CHECK(LAYOUT_COUNT == 5);
  CHECK_STR(settings::layoutName(Layout::D), "D");
  Settings in;
  in.layout = Layout::D;
  JsonDocument doc;
  settings::toJson(in, doc.to<JsonVariant>());
  CHECK_STR(doc["layout"].as<const char *>(), "D");
}

static void caseLangSettings() {
  std::puts("settings JSON: lang key, default ko, migration from 0.2.0");
  // Exactly the keys 0.2.0 writes (settings::toJson in that release).
  Settings s = loadSettings(R"({"admin_pass":"admin-pass","ap_pass":"ap-pass1","usage_url":"https://x/clock/usage",
      "usage_token":"t","tls_fp":"","tls_mode":"ca","tz":"KST-9","brightness":55,"refresh_min":7,
      "layout":"B","layout_mode":"cycle","cycle_sec":20})");
  CHECK(s.lang == Lang::Ko);
  CHECK(s.layout == Layout::B && s.layoutMode == LayoutMode::Cycle && s.cycleSec == 20);
  CHECK(loadSettings("{}").lang == Lang::Ko);
  CHECK(loadSettings(R"({"lang":"en"})").lang == Lang::En);
  CHECK(loadSettings(R"({"lang":"ko"})").lang == Lang::Ko);
  for (const char *bad : {R"({"lang":"EN"})", R"({"lang":"fr"})", R"({"lang":""})", R"({"lang":1})", R"({"lang":null})"}) {
    CHECK(loadSettings(bad).lang == Lang::Ko);
  }
  Lang l = Lang::En;
  CHECK(!settings::parseLang(nullptr, l) && !settings::parseLang("english", l) && l == Lang::En);
  CHECK_STR(settings::langName(Lang::En), "en");
  CHECK_STR(settings::langName(Lang::Ko), "ko");
  for (Lang lang : {Lang::En, Lang::Ko}) {
    Settings in;
    in.lang = lang;
    in.layout = Layout::A;
    in.tz = "UTC0";
    JsonDocument doc;
    settings::toJson(in, doc.to<JsonVariant>());
    std::string raw;
    serializeJson(doc, raw);
    CHECK_STR(doc["lang"].as<const char *>(), settings::langName(lang));
    Settings out = loadSettings(raw.c_str());
    CHECK(out.lang == lang && out.layout == Layout::A);
    CHECK_STR(out.tz.c_str(), "UTC0");
  }
}

static void caseScreenTextTable() {
  std::puts("screen text table: every entry present in both languages");
  for (uint8_t i = 0; i < static_cast<uint8_t>(Msg::Count); i++) {
    CHECK(std::strlen(i18n::text(Lang::En, static_cast<Msg>(i))) > 0);
    CHECK(std::strlen(i18n::text(Lang::Ko, static_cast<Msg>(i))) > 0);
  }
  CHECK_STR(i18n::text(Lang::En, Msg::SetupMode), "Setup mode");
  CHECK_STR(i18n::text(Lang::Ko, Msg::SetupMode), "설정 모드");
  char buf[8];
  i18n::copy(Lang::En, Msg::WifiLost, buf, sizeof(buf));
  CHECK_STR(buf, "Wi-Fi o");
}

static void caseEnglishHeader() {
  std::puts("English header and age text");
  auto en = [](const UsageState &st, uint32_t nowMs, char *out, size_t n) {
    return usage::headerText(st, true, nowMs, Lang::En, out, n);
  };
  char h[40];
  UsageState st;
  CHECK(!usage::headerText(st, false, 0, Lang::En, h, sizeof(h)));
  CHECK_STR(h, "No URL set");
  CHECK(!en(st, 0, h, sizeof(h)));
  CHECK_STR(h, "Loading");
  st.lastAttemptMs = 1;
  CHECK(en(st, 0, h, sizeof(h)));
  CHECK_STR(h, "Failed");
  st.waitingForTime = true;
  CHECK(!en(st, 0, h, sizeof(h)));
  CHECK_STR(h, "Waiting for time");

  CHECK(parseStr(R"({"updated":"2026-11-13T19:30:16+09:00","items":[{"label":"Claude","value":"1","pct":1}],"status":"ok"})", st));
  st.waitingForTime = false;
  CHECK(!en(st, 0, h, sizeof(h)));
  CHECK_STR(h, "as of 19:30");
  st.relayStatus = RelayStatus::Stale;
  CHECK(en(st, 0, h, sizeof(h)));
  CHECK_STR(h, "Stale \xC2\xB7 as of 19:30");
  st.updated[0] = '\0';
  CHECK(en(st, 0, h, sizeof(h)));
  CHECK_STR(h, "Stale");
  st.relayStatus = RelayStatus::Error;
  std::strcpy(st.relayError, "usage_http_5");
  CHECK(en(st, 0, h, sizeof(h)));
  CHECK_STR(h, "Server error usage_http_5");
  st.relayError[0] = '\0';
  CHECK(en(st, 0, h, sizeof(h)));
  CHECK_STR(h, "Server error");

  st.relayStatus = RelayStatus::Ok;
  const struct {
    uint32_t ms;
    const char *en, *ko;
  } ages[] = {{0, "0m ago", "0분 전"},           {59 * 60000, "59m ago", "59분 전"},
              {60 * 60000, "1h ago", "1시간 전"}, {47 * 3600000u + 59 * 60000, "47h ago", "47시간 전"},
              {48 * 3600000u, "2d ago", "2일 전"}, {10 * 86400000u, "10d ago", "10일 전"}};
  for (const auto &a : ages) {
    CHECK(!en(st, a.ms, h, sizeof(h)));
    CHECK_STR(h, a.en);
    CHECK(!usage::headerText(st, true, a.ms, Lang::Ko, h, sizeof(h)));
    CHECK_STR(h, a.ko);
  }
  st.lastOk = false;
  CHECK(en(st, 3 * 3600000u, h, sizeof(h)));
  CHECK_STR(h, "Failed \xC2\xB7 3h ago");
}

static void caseEnglishLayout() {
  std::puts("0.3.0 English screens: glyph coverage and widths");
  int missing = 0;
  const char *wdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int widest = 0;
  for (const char *m : months) {
    for (const char *wd : wdays) {
      char date[40];
      std::snprintf(date, sizeof(date), "%s, %s %d", wd, m, 28);
      int w = textWidth(FONT_S, date, &missing);
      if (w > widest) widest = w;
    }
  }
  std::printf("    widest date %dpx (box 150)\n", widest);
  CHECK(widest <= 150);
  for (Msg m : {Msg::SyncingTime, Msg::ConnectingWifi}) {
    const int w = textWidth(FONT_S, i18n::text(Lang::En, m), &missing);
    std::printf("    date box %-20s %3dpx (box 150)\n", i18n::text(Lang::En, m), w);
    CHECK(w <= 150);
  }
  // Footer boxes: A 108px, B 224px, C 140px in FONT_XS. Longest realistic value of every footer text.
  const char *footers[] = {"Reset 12-31 23:59", "Reset 12-31 23:59 \xC2\xB7 as of 23:59", "Stale \xC2\xB7 as of 19:30",
                           "Server error credentials_", "Failed \xC2\xB7 47h ago", "Loading", "Failed", "Waiting for time",
                           "No cert check", "Plaintext token", "Wi-Fi offline", "No URL set", "as of 23:59",
                           "59m ago"};
  for (const char *t : footers) {
    const int w = textWidth(FONT_XS, t, &missing);
    std::printf("    footer %-36s %3dpx  A%s B%s C%s\n", t, w, w <= 108 ? "ok" : "--", w <= 224 ? "ok" : "--",
                w <= 140 ? "ok" : "--");
  }
  CHECK(textWidth(FONT_XS, "Reset 12-31 23:59") <= 108);
  // Everything that can appear in A's footer fits whole, so fitText never cuts an English phrase there.
  CHECK(textWidth(FONT_XS, "Reset 12-31 23:59 \xC2\xB7 as of 23:59") <= 224);
  for (const char *t : {"Stale \xC2\xB7 as of 19:30", "Failed \xC2\xB7 47h ago", "Waiting for time", "No cert check",
                        "Plaintext token", "Wi-Fi offline", "No URL set"}) {
    CHECK(textWidth(FONT_XS, t) <= 108);
  }
  for (Msg m : {Msg::SetupMode, Msg::Updating, Msg::Done, Msg::Error, Msg::ConnectingWifi}) {
    CHECK(textWidth(FONT_M, i18n::text(Lang::En, m), &missing) <= 224);
  }
  for (Msg m : {Msg::Password, Msg::OpenAddress, Msg::AdminLogin, Msg::Restarting}) {
    CHECK(textWidth(FONT_S, i18n::text(Lang::En, m), &missing) <= 224);
  }
  CHECK(textWidth(FONT_S, "Admin  admin / abcdefghjk") <= 224);
  CHECK(missing == 0);
}

static void caseLayoutD() {
  std::puts("0.4.0 design D: glyph coverage and widths");
  int missing = 0;
  for (Lang lang : {Lang::En, Lang::Ko}) {
    const int w = textWidth(FONT_XS, i18n::text(lang, Msg::Last7Days), &missing);
    std::printf("    header %-24s %3dpx (box 224)\n", i18n::text(lang, Msg::Last7Days), w);
    CHECK(w <= 224);
  }
  CHECK_STR(i18n::text(Lang::En, Msg::Last7Days), "Last 7 days");
  CHECK_STR(i18n::text(Lang::Ko, Msg::Last7Days), "최근 7일 사용량");
  // Name box 76px in FONT_M; the number box is 122px for FONT_L digits plus a FONT_P "%".
  for (const char *name : {"Claude", "Fable"}) {
    const int w = textWidth(FONT_M, name, &missing);
    std::printf("    name %-8s %3dpx (box 76)\n", name, w);
    CHECK(w <= 76);
  }
  const int sign = textWidth(FONT_P, "%", &missing);
  for (const char *n : {"0", "7", "42", "100", "--"}) {
    const int w = textWidth(FONT_L, n, &missing) + (std::strcmp(n, "--") ? sign : 0);
    std::printf("    value %-4s %3dpx (box 122)\n", n, w);
    CHECK(w <= 122);
  }
  // Every footer text of design D fits the 224px box whole, so fitText never shortens it.
  const char *footers[] = {"초기화 12-31 23:59 · 23:59 기준", "초기화 12-31 23:59 · 지연 · 23:59 기준",
                           "서버 오류 usage_http_5", "서버 오류 credentials_", "실패 · 47시간 전", "실패", "불러오는 중",
                           "Wi-Fi 연결 끊김", "Wi-Fi 연결 중", "시각 동기화 대기", "인증서 검증 안 함", "토큰 평문 전송", "URL 미설정",
                           "Reset 12-31 23:59 \xC2\xB7 as of 23:59", "Reset 12-31 23:59 \xC2\xB7 Stale \xC2\xB7 as of 23:59",
                           "Server error usage_http_5", "Server error credentials_", "Failed \xC2\xB7 47h ago", "Failed",
                           "Loading", "Wi-Fi offline", "Connecting Wi-Fi", "Waiting for time", "No cert check",
                           "Plaintext token", "No URL set"};
  for (const char *t : footers) {
    const int w = textWidth(FONT_XS, t, &missing);
    std::printf("    footer %-40s %3dpx (box 224)\n", t, w);
    CHECK(w <= 224);
  }
  CHECK(missing == 0);
}

// Builds a relay 1.3.0 response; hourly is appended to both items when given.
static std::string relay130(const char *claudeExtra, const char *fableExtra) {
  std::string j = R"({"updated":"2026-11-18T14:25:00+09:00","period":"7d","status":"ok","version":"1.3.0","sample_age_s":120,"items":[)";
  j += R"({"label":"Claude","value":"42%","pct":42,"resets":"11-18 09:00","pct_raw":42.3)";
  j += claudeExtra;
  j += R"(},{"label":"Fable","value":"17%","pct":17,"resets":"11-18 09:00","pct_raw":17.0)";
  j += fableExtra;
  j += "}]}";
  return j;
}

static void caseIncreases() {
  std::puts("relay 1.3.0 increases: values, null, absent (1.2.0), non-numbers, text");
  UsageState st;
  CHECK(parseStr(relay130(R"(,"delta_1h":1.4,"delta_24h":11.2)", R"(,"delta_1h":0.0,"delta_24h":-0.3)").c_str(), st));
  CHECK(st.items[0].delta1h == 14 && st.items[0].delta24h == 112);
  CHECK(st.items[1].delta1h == 0 && st.items[1].delta24h == -3);
  CHECK(!st.haveHourly);
  CHECK(st.items[0].pct == 42);
  CHECK(parseStr(relay130(R"(,"delta_1h":null,"delta_24h":2)", R"(,"delta_1h":0.1,"delta_24h":"x")").c_str(), st));
  CHECK(st.items[0].delta1h == DELTA_NO_SAMPLE && st.items[0].delta24h == 20);
  CHECK(st.items[1].delta1h == 1 && st.items[1].delta24h == DELTA_MISSING);
  // 1.2.0 response: no increase fields at all.
  CHECK(parseStr(R"({"updated":"2026-11-13T19:30:16+09:00","period":"7d","status":"ok","items":[
      {"label":"Claude","value":"42%","pct":42,"resets":"11-18 09:00"},{"label":"Fable","value":"17%","pct":17}]})", st));
  CHECK(st.items[0].delta1h == DELTA_MISSING && st.items[0].delta24h == DELTA_MISSING);
  CHECK(st.items[1].delta1h == DELTA_MISSING && !st.haveHourly);
  CHECK(st.items[0].pct == 42 && st.items[1].pct == 17);

  char t[24];
  const struct {
    int16_t v;
    Lang lang;
    const char *want;
  } texts[] = {{14, Lang::Ko, "+1.4%"},          {0, Lang::Ko, "+0.0%"},         {-3, Lang::Ko, "-0.3%"},
               {-25, Lang::En, "-2.5%"},         {1000, Lang::En, "+100.0%"},    {DELTA_NO_SAMPLE, Lang::Ko, "표본 부족"},
               {DELTA_NO_SAMPLE, Lang::En, "no data"}, {DELTA_MISSING, Lang::Ko, "--"}, {DELTA_MISSING, Lang::En, "--"}};
  for (const auto &c : texts) {
    usage::deltaText(c.v, c.lang, t, sizeof(t));
    CHECK_STR(t, c.want);
  }
}

static void caseHourly() {
  std::puts("relay 1.3.0 hourly: 24 values, nulls, short and long arrays, not an array");
  UsageState st;
  CHECK(parseStr(relay130(R"(,"hourly":[0.9,1.2,null,0.6,0.3,0.4,0.2,0,0,0,0,0,0,0,0,0,0,0,0.3,1.1,1.6,1.3,-0.4,1.4])",
                          R"(,"hourly":[null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null])")
                     .c_str(),
                 st));
  CHECK(st.haveHourly);
  CHECK(st.items[0].hourly[0] == 9 && st.items[0].hourly[2] == DELTA_NO_SAMPLE && st.items[0].hourly[7] == 0);
  CHECK(st.items[0].hourly[22] == -4 && st.items[0].hourly[23] == 14);
  CHECK(st.items[1].hourly[0] == DELTA_NO_SAMPLE && st.items[1].hourly[23] == DELTA_NO_SAMPLE);
  // Fewer than 24 values end at the previous hour; the missing older hours have no sample.
  CHECK(parseStr(relay130(R"(,"hourly":[0.5,1.0])", R"(,"hourly":[])").c_str(), st));
  CHECK(st.haveHourly);
  CHECK(st.items[0].hourly[21] == DELTA_NO_SAMPLE && st.items[0].hourly[22] == 5 && st.items[0].hourly[23] == 10);
  for (uint8_t i = 0; i < HOURLY_SLOTS; i++) CHECK(st.items[1].hourly[i] == DELTA_NO_SAMPLE);
  // More than 24 values keep the newest 24.
  std::string many = R"(,"hourly":[9.9,9.9)";
  for (int i = 1; i <= 24; i++) many += "," + std::to_string(i) + ".0";
  many += "]";
  CHECK(parseStr(relay130(many.c_str(), "").c_str(), st));
  CHECK(st.items[0].hourly[0] == 10 && st.items[0].hourly[23] == 240);
  // A non-array hourly or a non-number value is ignored rather than drawn.
  CHECK(parseStr(relay130(R"(,"hourly":"x")", R"(,"hourly":[true,"a",0.2])").c_str(), st));
  CHECK(st.items[0].hourly[23] == DELTA_NO_SAMPLE);
  CHECK(st.items[1].hourly[21] == DELTA_NO_SAMPLE && st.items[1].hourly[22] == DELTA_NO_SAMPLE &&
        st.items[1].hourly[23] == 2);
  // A later response without hourly clears the flag.
  CHECK(parseStr(relay130("", "").c_str(), st));
  CHECK(!st.haveHourly && st.items[0].hourly[23] == DELTA_NO_SAMPLE);
}

static void caseHourlyBodyLimit() {
  std::puts("relay 1.3.0 ?hourly=1 response against the 4,096-byte body limit");
  std::string hourly = R"(,"delta_1h":-12.3,"delta_24h":-100.0,"hourly":[)";
  for (int i = 0; i < 24; i++) hourly += i ? ",-100.0" : "-100.0";
  hourly += "]";
  std::string body = relay130(hourly.c_str(), hourly.c_str());
  std::printf("    widest ?hourly=1 body %zu bytes (limit %zu)\n", body.size(), usage::MAX_BODY);
  CHECK(body.size() < usage::MAX_BODY);
  body.insert(body.size() - 1, usage::MAX_BODY - body.size(), ' ');
  CHECK(body.size() == usage::MAX_BODY);
  UsageState st;
  CHECK(parseStr(body.c_str(), st));
  CHECK(st.haveHourly && st.items[1].hourly[23] == -1000 && st.items[0].delta1h == -123);
}

static void caseShowsHourly() {
  std::puts("hourly values are requested only when design E can be on screen");
  Settings s;
  CHECK(!settings::showsHourly(s));
  for (Layout layout : {Layout::A, Layout::B, Layout::C, Layout::D}) {
    s.layout = layout;
    s.layoutMode = LayoutMode::Fixed;
    CHECK(!settings::showsHourly(s));
    s.layoutMode = LayoutMode::Cycle;
    CHECK(settings::showsHourly(s));
  }
  s.layout = Layout::E;
  s.layoutMode = LayoutMode::Fixed;
  CHECK(settings::showsHourly(s));
}

static void caseLayoutE() {
  std::puts("0.5.0 design E: glyph coverage and widths");
  int missing = 0;
  for (Lang lang : {Lang::En, Lang::Ko}) {
    const int w = textWidth(FONT_XS, i18n::text(lang, Msg::Last24Hours), &missing);
    std::printf("    header %-24s %3dpx (box 224)\n", i18n::text(lang, Msg::Last24Hours), w);
    CHECK(w <= 224);
  }
  CHECK_STR(i18n::text(Lang::En, Msg::Last24Hours), "Last 24 hours");
  CHECK_STR(i18n::text(Lang::Ko, Msg::Last24Hours), "최근 24시간 증가");
  CHECK_STR(i18n::text(Lang::En, Msg::NoSample), "no data");
  CHECK_STR(i18n::text(Lang::Ko, Msg::NoSample), "표본 부족");
  // Name box 100px in FONT_M, percentage box 100px in FONT_P, increase line 204px in FONT_XS.
  for (const char *name : {"Claude", "Fable"}) CHECK(textWidth(FONT_M, name, &missing) <= 100);
  for (const char *n : {"0%", "100%", "--"}) CHECK(textWidth(FONT_P, n, &missing) <= 100);
  const char *lines[] = {"1h 표본 부족 \xC2\xB7 24h 표본 부족", "1h no data \xC2\xB7 24h no data",
                         "1h -100.0% \xC2\xB7 24h -100.0%", "1h +100.0% \xC2\xB7 24h +100.0%", "1h -- \xC2\xB7 24h --"};
  for (const char *t : lines) {
    const int w = textWidth(FONT_XS, t, &missing);
    std::printf("    increases %-36s %3dpx (box 204)\n", t, w);
    CHECK(w <= 204);
  }
  CHECK(missing == 0);
}

int main() {
  caseDraft();
  caseRejectsAndLimits();
  caseRelayOk();
  caseRelayStale();
  caseRelayError();
  caseResetsAbsentOrMalformed();
  caseTlsModeNames();
  caseSettingsSerialization();
  caseTlsPolicy();
  caseLayout();
  caseDisplaySettings();
  caseLayoutDSettings();
  caseLangSettings();
  caseScreenTextTable();
  caseEnglishHeader();
  caseEnglishLayout();
  caseLayoutD();
  caseIncreases();
  caseHourly();
  caseHourlyBodyLimit();
  caseShowsHourly();
  caseLayoutE();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
