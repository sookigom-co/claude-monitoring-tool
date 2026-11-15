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
  CHECK(!settings::parseLayout(nullptr, l) && !settings::parseLayout("", l) && !settings::parseLayout("D", l));
  for (Layout layout : {Layout::A, Layout::B, Layout::C}) {
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
  caseLangSettings();
  caseScreenTextTable();
  caseEnglishHeader();
  caseEnglishLayout();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
