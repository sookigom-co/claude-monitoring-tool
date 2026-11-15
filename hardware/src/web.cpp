#include "web.h"

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <Updater.h>
#include <WiFiUdp.h>

#include "board.h"
#include "ui.h"

namespace web {
namespace {

const char *ADMIN_USER = "admin";
const char *REALM = "SmartClock";
constexpr uint8_t MAX_AUTH_FAILURES = 10;
constexpr uint32_t LOCKOUT_MS = 60000;
const char *const RELAY_STATUS_NAMES[] = {"unknown", "ok", "stale", "error"};

struct TlsModeOption {
  TlsMode mode;
  const char *label;
};
const TlsModeOption TLS_MODE_OPTIONS[] = {
    {TlsMode::Ca, "CA (Let's Encrypt ISRG Root X1/X2) - default"},
    {TlsMode::Fingerprint, "Pinned SHA-1 fingerprint"},
    {TlsMode::Insecure, "No check (insecure)"},
};

const char *const LAYOUT_LABELS[LAYOUT_COUNT] = {
    "A - concentric rings (Claude outside, Fable inside)",
    "B - clock on top, two gauges below",
    "C - left/right half rings (default)",
};

ESP8266WebServer server(80);
AppHooks *app = nullptr;
String csrf;
uint8_t authFailures = 0;
uint32_t lockoutUntil = 0;
bool uploadAllowed = false;
bool uploadFailed = false;
String uploadError;

String esc(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (char c : in) {
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c;
    }
  }
  return out;
}

const char PAGE_HEAD[] PROGMEM =
    "<!doctype html><html lang=ko><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>SmartClock</title>"
    "<style>body{font-family:sans-serif;max-width:560px;margin:1em auto;padding:0 1em;background:#111;color:#eee}"
    "input,select,button{font-size:1em;margin:.2em 0;padding:.4em;width:100%;box-sizing:border-box}"
    "input[type=checkbox]{width:auto}label{display:block;margin-top:.8em;color:#aaa}"
    "fieldset{border:1px solid #444;margin:1em 0}td{padding:.2em .6em .2em 0;vertical-align:top}"
    "a{color:#7cf}.warn{color:#fa4}</style></head><body>";
const char PAGE_TAIL[] PROGMEM = "</body></html>";

void sendPage(int code, const String &body) {
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.sendHeader(F("X-Frame-Options"), F("DENY"));
  server.sendHeader(F("Content-Security-Policy"), F("default-src 'self'; style-src 'unsafe-inline'; form-action 'self'"));
  String page;
  page.reserve(body.length() + 900);
  page += FPSTR(PAGE_HEAD);
  page += body;
  page += FPSTR(PAGE_TAIL);
  server.send(code, F("text/html; charset=utf-8"), page);
}

void sendRedirect(const String &to) {
  server.sendHeader(F("Location"), to);
  server.send(303, F("text/plain"), "");
}

String csrfField() { return String(F("<input type=hidden name=csrf value='")) + csrf + "'>"; }

bool csrfOk() { return server.hasArg(F("csrf")) && server.arg(F("csrf")) == csrf; }

bool authed() {
  if (lockoutUntil && static_cast<int32_t>(millis() - lockoutUntil) < 0) {
    server.send(429, F("text/plain"), F("Too many failed logins. Try again later."));
    return false;
  }
  if (server.authenticate(ADMIN_USER, app->settings->adminPass.c_str())) {
    authFailures = 0;
    lockoutUntil = 0;
    return true;
  }
  if (server.hasHeader(F("Authorization")) && ++authFailures >= MAX_AUTH_FAILURES) {
    authFailures = 0;
    lockoutUntil = millis() + LOCKOUT_MS;
  }
  server.requestAuthentication(DIGEST_AUTH, REALM, F("Authentication required"));
  return false;
}

String tokenWarningHtml(const Settings &s) {
  switch (settings::tokenWarning(s)) {
    case TokenWarning::NoCertCheck:
      return F("<p class=warn>Warning: the certificate is not checked, so anyone on the path can read the Bearer token. Use CA mode.</p>");
    case TokenWarning::Plaintext:
      return F("<p class=warn>Warning: http:// sends the Bearer token in plain text. Use an https:// URL with CA mode.</p>");
    default:
      return String();
  }
}

String row(const __FlashStringHelper *k, const String &v) { return String(F("<tr><td>")) + k + F("</td><td>") + v + F("</td></tr>"); }

void handleRoot() {
  if (!authed()) return;
  const Settings &s = *app->settings;
  const UsageState &u = *app->usage;
  time_t now = time(nullptr);
  char clock[32] = "not synced";
  if (now > usage::VALID_EPOCH) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S", &t);
  }
  String usageStatus;
  if (s.usageUrl.isEmpty()) usageStatus = F("URL not set");
  else if (u.lastOk && u.relayStatus == RelayStatus::Stale)
    usageStatus = String(F("<span class=warn>relay stale: ")) + esc(u.relayError) + F("</span>");
  else if (u.lastOk && u.relayStatus == RelayStatus::Error)
    usageStatus = String(F("<span class=warn>relay error: ")) + esc(u.relayError) + F("</span>");
  else if (u.lastOk) usageStatus = String(F("OK, ")) + ((millis() - u.lastSuccessMs) / 60000) + F(" min ago");
  else if (u.lastAttemptMs) usageStatus = String(F("<span class=warn>failed: ")) + esc(u.lastError) + F("</span>");
  else usageStatus = F("pending");

  String b;
  b.reserve(5400);
  b += F("<h2>SmartClock</h2><table>");
  b += row(F("Firmware"), F(FW_VERSION));
  b += row(F("Wi-Fi"), esc(WiFi.SSID()) + F(" (") + WiFi.RSSI() + F(" dBm)"));
  b += row(F("IP"), WiFi.localIP().toString());
  b += row(F("Time"), clock);
  b += row(F("Free heap"), String(ESP.getFreeHeap()));
  b += row(F("Usage feed"), usageStatus);
  b += row(F("Certificate check"), settings::tlsModeName(s.tlsMode));
  b += F("</table><form method=post action=/save>");
  b += csrfField();
  b += F("<fieldset><legend>Usage feed</legend><label>JSON URL (http:// or https://)</label><input name=usage_url maxlength=200 value='");
  b += esc(s.usageUrl);
  b += F("'><label>Bearer token</label><input name=usage_token type=password autocomplete=off maxlength=200 placeholder='");
  b += s.usageToken.length() ? F("set - leave empty to keep") : F("not set");
  b += F("'><label><input type=checkbox name=clear_token value=1> clear token</label><label>HTTPS certificate check</label><select name=tls_mode>");
  for (const auto &m : TLS_MODE_OPTIONS) {
    b += F("<option value=");
    b += settings::tlsModeName(m.mode);
    if (m.mode == s.tlsMode) b += F(" selected");
    b += '>';
    b += m.label;
    b += F("</option>");
  }
  b += F("</select>");
  b += tokenWarningHtml(s);
  b += F("<label>TLS SHA-1 fingerprint (fingerprint mode only)</label><input name=tls_fp maxlength=64 value='");
  b += esc(s.tlsFingerprint);
  b += F("'><label>Refresh interval (minutes)</label><input name=refresh_min type=number min=1 max=1440 value=");
  b += s.refreshMin;
  b += F("></fieldset><fieldset><legend>Display</legend><label>POSIX time zone</label><input name=tz maxlength=48 value='");
  b += esc(s.tz);
  b += F("'><label>Brightness (0-100)</label><input name=brightness type=number min=0 max=100 value=");
  b += s.brightness;
  b += F("><label>Screen design</label><select name=layout>");
  for (uint8_t i = 0; i < LAYOUT_COUNT; i++) {
    b += F("<option value=");
    b += settings::layoutName(static_cast<Layout>(i));
    if (static_cast<uint8_t>(s.layout) == i) b += F(" selected");
    b += '>';
    b += LAYOUT_LABELS[i];
    b += F("</option>");
  }
  b += F("</select><label>Design change</label><select name=layout_mode><option value=fixed");
  if (s.layoutMode == LayoutMode::Fixed) b += F(" selected");
  b += F(">Fixed - always show the design above</option><option value=cycle");
  if (s.layoutMode == LayoutMode::Cycle) b += F(" selected");
  b += F(">Cycle - A, B, C in turn, starting from the design above</option></select>"
         "<label>Cycle interval (seconds, 5-300)</label><input name=cycle_sec type=number min=5 max=300 value=");
  b += s.cycleSec;
  b += F("></fieldset><fieldset><legend>Admin password (also OTA password)</legend>"
         "<label>New password (8+ chars, empty = keep)</label><input name=admin_pass type=password autocomplete=new-password maxlength=64>"
         "<label>Repeat</label><input name=admin_pass2 type=password autocomplete=new-password maxlength=64></fieldset>"
         "<button>Save</button></form>");
  b += F("<fieldset><legend>Maintenance</legend><p><a href=/update>Firmware update</a></p>");
  const char *actions[][2] = {{"/reboot", "Reboot"}, {"/wifi-reset", "Forget Wi-Fi and open setup portal"},
                              {"/factory-reset", "Factory reset (Wi-Fi + all settings)"}};
  for (auto &a : actions) {
    b += F("<form method=post action=");
    b += a[0];
    b += '>';
    b += csrfField();
    b += F("<button>");
    b += a[1];
    b += F("</button></form>");
  }
  b += F("</fieldset>");
  sendPage(200, b);
}

void handleSave() {
  if (!authed()) return;
  if (!csrfOk()) return sendPage(403, F("<p>Invalid form token. Reload the page.</p>"));
  Settings next = *app->settings;
  String err;

  String url = server.arg(F("usage_url"));
  url.trim();
  if (url.length() && !url.startsWith(F("http://")) && !url.startsWith(F("https://"))) err += F("URL must start with http:// or https://. ");
  next.usageUrl = url;

  String token = server.arg(F("usage_token"));
  token.trim();
  if (server.arg(F("clear_token")) == "1") next.usageToken = "";
  else if (token.length()) next.usageToken = token;

  String fp = server.arg(F("tls_fp"));
  fp.trim();
  if (!settings::validFingerprint(fp)) err += F("Fingerprint must be 40 hex digits. ");
  next.tlsFingerprint = fp;

  if (!settings::parseTlsMode(server.arg(F("tls_mode")).c_str(), next.tlsMode)) err += F("Unknown certificate check mode. ");
  else if (next.tlsMode == TlsMode::Fingerprint && fp.isEmpty()) err += F("Fingerprint mode needs a fingerprint. ");

  String tz = server.arg(F("tz"));
  tz.trim();
  if (!settings::validTz(tz)) err += F("Invalid time zone. ");
  next.tz = tz;

  long bright = server.arg(F("brightness")).toInt();
  long refresh = server.arg(F("refresh_min")).toInt();
  if (bright < 0 || bright > 100) err += F("Brightness must be 0-100. ");
  if (refresh < 1 || refresh > 1440) err += F("Refresh must be 1-1440. ");
  next.brightness = static_cast<uint8_t>(constrain(bright, 0L, 100L));
  next.refreshMin = static_cast<uint16_t>(constrain(refresh, 1L, 1440L));

  if (!settings::parseLayout(server.arg(F("layout")).c_str(), next.layout)) err += F("Unknown screen design. ");
  if (!settings::parseLayoutMode(server.arg(F("layout_mode")).c_str(), next.layoutMode)) err += F("Unknown design change mode. ");
  long cycle = server.arg(F("cycle_sec")).toInt();
  if (!settings::validCycleSec(cycle)) err += F("Cycle interval must be 5-300 seconds. ");
  else next.cycleSec = static_cast<uint16_t>(cycle);

  String pass = server.arg(F("admin_pass"));
  bool passChanged = false;
  if (pass.length()) {
    if (pass != server.arg(F("admin_pass2"))) err += F("Passwords do not match. ");
    else if (pass.length() < settings::MIN_ADMIN_PASS) err += F("Password too short. ");
    else {
      next.adminPass = pass;
      passChanged = true;
    }
  }

  if (err.length()) return sendPage(400, String(F("<p class=warn>")) + esc(err) + F("</p><p><a href=/>Back</a></p>"));
  if (!settings::save(next)) return sendPage(500, F("<p class=warn>Saving to flash failed.</p><p><a href=/>Back</a></p>"));
  *app->settings = next;
  if (passChanged) {
    sendPage(200, F("<p>Saved. Restarting to apply the new password.</p>"));
    delay(500);
    ESP.restart();
  }
  app->applySettings();
  app->requestFetch();
  sendRedirect(F("/"));
}

void handleReboot() {
  if (!authed()) return;
  if (!csrfOk()) return sendPage(403, F("<p>Invalid form token.</p>"));
  sendPage(200, F("<p>Restarting.</p>"));
  delay(500);
  ESP.restart();
}

void forgetWifi() {
  WiFi.persistent(true);
  WiFi.disconnect(true);
  WiFi.persistent(false);
}

void handleWifiReset() {
  if (!authed()) return;
  if (!csrfOk()) return sendPage(403, F("<p>Invalid form token.</p>"));
  sendPage(200, F("<p>Wi-Fi forgotten. The setup portal opens after restart; its details are shown on the clock.</p>"));
  delay(500);
  forgetWifi();
  ESP.restart();
}

void handleFactoryReset() {
  if (!authed()) return;
  if (!csrfOk()) return sendPage(403, F("<p>Invalid form token.</p>"));
  sendPage(200, F("<p>Factory reset. New passwords are shown on the clock after restart.</p>"));
  delay(500);
  forgetWifi();
  settings::factoryReset();
  ESP.restart();
}

void handleUpdatePage() {
  if (!authed()) return;
  String b = F("<h2>Firmware update</h2><p>Current: " FW_VERSION "</p><form method=post enctype=multipart/form-data action='/update?csrf=");
  b += csrf;
  b += F("'><input type=file name=firmware accept=.bin required><button>Upload and flash</button></form><p><a href=/>Back</a></p>");
  sendPage(200, b);
}

void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadFailed = false;
    uploadError = "";
    uploadAllowed = server.authenticate(ADMIN_USER, app->settings->adminPass.c_str()) && csrfOk();
    if (!uploadAllowed) return;
    ui::showMessage("업데이트 중", up.filename.c_str());
    WiFiUDP::stopAll();
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace, U_FLASH)) {
      uploadFailed = true;
      uploadError = Update.getErrorString();
    }
  } else if (!uploadAllowed || uploadFailed) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      uploadFailed = true;
      uploadError = Update.getErrorString();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      uploadFailed = true;
      uploadError = Update.getErrorString();
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    uploadFailed = true;
    uploadError = F("upload aborted");
  }
  yield();
}

void handleUpdateDone() {
  if (!authed()) return;
  if (!uploadAllowed) return sendPage(403, F("<p>Invalid form token. Reload the update page.</p>"));
  if (uploadFailed || Update.hasError()) {
    ui::invalidate();
    return sendPage(500, String(F("<p class=warn>Update failed: ")) + esc(uploadError) + F("</p><p><a href=/>Back</a></p>"));
  }
  ui::showMessage("완료", "재시작");
  sendPage(200, F("<p>Update complete. Restarting.</p>"));
  delay(1000);
  ESP.restart();
}

void handleStatusJson() {
  if (!authed()) return;
  const UsageState &u = *app->usage;
  String j = F("{\"version\":\"" FW_VERSION "\",\"ip\":\"");
  j += WiFi.localIP().toString();
  j += F("\",\"rssi\":");
  j += WiFi.RSSI();
  j += F(",\"heap\":");
  j += ESP.getFreeHeap();
  j += F(",\"time\":");
  j += static_cast<long>(time(nullptr));
  j += F(",\"usage_ok\":");
  j += u.lastOk ? F("true") : F("false");
  j += F(",\"relay_status\":\"");
  j += RELAY_STATUS_NAMES[static_cast<uint8_t>(u.relayStatus)];
  j += F("\",\"layout\":\"");
  j += settings::layoutName(app->settings->layout);
  j += F("\",\"layout_mode\":\"");
  j += settings::layoutModeName(app->settings->layoutMode);
  j += F("\",\"cycle_sec\":");
  j += app->settings->cycleSec;
  j += F(",\"usage_age_s\":");
  j += u.haveData ? String((millis() - u.lastSuccessMs) / 1000) : String(F("null"));
  j += F(",\"waiting_for_time\":");
  j += u.waitingForTime ? F("true") : F("false");
  j += F(",\"tls_mode\":\"");
  j += settings::tlsModeName(app->settings->tlsMode);
  j += F("\",\"tls\":{\"heap_before\":");
  j += u.tls.heapBefore;
  j += F(",\"max_block_before\":");
  j += u.tls.maxBlockBefore;
  j += F(",\"heap_connected\":");
  j += u.tls.heapConnected;
  j += F(",\"heap_after\":");
  j += u.tls.heapAfter;
  j += F(",\"mfln\":");
  j += u.tls.mfln < 0 ? String(F("null")) : String(u.tls.mfln ? F("true") : F("false"));
  j += F(",\"ssl_error\":");
  j += u.tls.sslError;
  j += F("}}");
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(200, F("application/json"), j);
}

void portalPage(const String &message) {
  String b;
  b.reserve(2400);
  b += F("<h2>SmartClock setup</h2>");
  if (message.length()) b += String(F("<p class=warn>")) + esc(message) + F("</p>");
  b += F("<form method=post action=/save><label>Network</label><select name=ssid_pick><option value=''>(type below)</option>");
  int n = WiFi.scanComplete();
  for (int i = 0; i < n && i < 20; i++) {
    String ssid = esc(WiFi.SSID(i));
    b += F("<option value='");
    b += ssid;
    b += F("'>");
    b += ssid;
    b += F(" (");
    b += WiFi.RSSI(i);
    b += F(" dBm)</option>");
  }
  b += F("</select><label>or SSID</label><input name=ssid maxlength=32>"
         "<label>Wi-Fi password</label><input name=pass type=password autocomplete=off maxlength=64>"
         "<label>New admin password (8+ chars, empty = keep the one shown on the clock)</label>"
         "<input name=admin_pass type=password autocomplete=new-password maxlength=64>"
         "<button>Save and connect</button></form><p><a href=/rescan>Rescan networks</a></p>");
  sendPage(200, b);
}

}  // namespace

[[noreturn]] void runPortal(Settings &s, const String &apName, uint32_t timeoutMs) {
  static Settings *cfg = &s;
  DNSServer dns;
  const IPAddress apIp(192, 168, 4, 1);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str(), s.apPass.c_str());
  WiFi.scanNetworks(true);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", apIp);
  ui::showSetup(apName.c_str(), s.apPass.c_str(), s.adminPass.c_str());

  server.on(F("/"), HTTP_GET, [] { portalPage(""); });
  server.on(F("/rescan"), HTTP_GET, [] {
    WiFi.scanNetworks(true);
    delay(4000);
    sendRedirect(F("/"));
  });
  server.on(F("/save"), HTTP_POST, [] {
    String ssid = server.arg(F("ssid"));
    if (ssid.isEmpty()) ssid = server.arg(F("ssid_pick"));
    String pass = server.arg(F("pass"));
    String admin = server.arg(F("admin_pass"));
    if (ssid.isEmpty() || ssid.length() > 32) return portalPage(F("SSID is required (max 32 chars)."));
    if (pass.length() && (pass.length() < 8 || pass.length() > 63)) return portalPage(F("Wi-Fi password must be 8-63 chars or empty."));
    if (admin.length() && admin.length() < settings::MIN_ADMIN_PASS) return portalPage(F("Admin password must be 8+ chars."));
    if (admin.length()) {
      cfg->adminPass = admin;
      if (!settings::save(*cfg)) return portalPage(F("Saving settings failed."));
    }
    sendPage(200, F("<p>Saved. The clock restarts and joins the network.</p>"));
    delay(500);
    WiFi.persistent(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    WiFi.persistent(false);
    delay(500);
    ESP.restart();
  });
  server.onNotFound([] { sendRedirect(F("http://192.168.4.1/")); });
  server.begin();

  const uint32_t started = millis();
  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    if (timeoutMs && millis() - started > timeoutMs) ESP.restart();
    delay(2);
  }
}

void beginAdmin(AppHooks &hooks) {
  app = &hooks;
  csrf = settings::randomPassword(24);
  server.on(F("/"), HTTP_GET, handleRoot);
  server.on(F("/save"), HTTP_POST, handleSave);
  server.on(F("/reboot"), HTTP_POST, handleReboot);
  server.on(F("/wifi-reset"), HTTP_POST, handleWifiReset);
  server.on(F("/factory-reset"), HTTP_POST, handleFactoryReset);
  server.on(F("/update"), HTTP_GET, handleUpdatePage);
  server.on(F("/update"), HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on(F("/api/status"), HTTP_GET, handleStatusJson);
  server.onNotFound([] { server.send(404, F("text/plain"), F("Not found")); });
  server.begin();
}

void handle() { server.handleClient(); }

}  // namespace web
