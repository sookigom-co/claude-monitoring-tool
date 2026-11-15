#include "web.h"

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <Updater.h>
#include <WiFiUdp.h>

#include "board.h"
#include "ui.h"
#include "web_pages.h"

namespace web {
namespace {

const char *ADMIN_USER = "admin";
const char *REALM = "SmartClock";
constexpr uint8_t MAX_AUTH_FAILURES = 10;
constexpr uint32_t LOCKOUT_MS = 60000;
const char *const RELAY_STATUS_NAMES[] = {"unknown", "ok", "stale", "error"};

ESP8266WebServer server(80);
AppHooks *app = nullptr;
String csrf;
uint8_t authFailures = 0;
uint32_t lockoutUntil = 0;
bool uploadAllowed = false;
bool uploadFailed = false;
String uploadError;

void sendChunk(const String &chunk) { server.sendContent(chunk); }

// Starts a chunked response with the security headers and streams the page that build writes into it.
template <typename Build>
void sendPage(int code, Lang lang, Build build) {
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.sendHeader(F("X-Frame-Options"), F("DENY"));
  server.sendHeader(F("Content-Security-Policy"), F("default-src 'self'; style-src 'unsafe-inline'; form-action 'self'"));
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(code, F("text/html; charset=utf-8"), emptyString);
  Html out(lang, sendChunk);
  build(out);
  out.flush();
  server.sendContent(emptyString);
}

void sendMessage(int code, Lang lang, Txt message, bool warn = false, bool back = false) {
  sendPage(code, lang, [&](Html &out) { messagePage(out, message, warn, back); });
}

Lang adminLang() { return app->settings->lang; }

void sendRedirect(const String &to) {
  server.sendHeader(F("Location"), to);
  server.send(303, F("text/plain"), "");
}

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

void handleRoot() {
  if (!authed()) return;
  const DeviceStatus dev{WiFi.SSID(), WiFi.RSSI(), WiFi.localIP().toString(), time(nullptr), ESP.getFreeHeap(), millis()};
  sendPage(200, adminLang(), [&](Html &out) { adminPage(out, *app->settings, *app->usage, dev, csrf); });
}

void handleSave() {
  if (!authed()) return;
  if (!csrfOk()) return sendMessage(403, adminLang(), Txt::BadToken);
  Settings next = *app->settings;
  const bool langOk = settings::parseLang(server.arg(F("lang")).c_str(), next.lang);
  const Lang lang = next.lang;
  String err;
  auto fail = [&](Txt id) { err += FPSTR(text(lang, id)); };
  if (!langOk) fail(Txt::ErrLang);

  String url = server.arg(F("usage_url"));
  url.trim();
  if (url.length() && !url.startsWith(F("http://")) && !url.startsWith(F("https://"))) fail(Txt::ErrUrl);
  next.usageUrl = url;

  String token = server.arg(F("usage_token"));
  token.trim();
  if (server.arg(F("clear_token")) == "1") next.usageToken = "";
  else if (token.length()) next.usageToken = token;

  String fp = server.arg(F("tls_fp"));
  fp.trim();
  if (!settings::validFingerprint(fp)) fail(Txt::ErrFingerprint);
  next.tlsFingerprint = fp;

  if (!settings::parseTlsMode(server.arg(F("tls_mode")).c_str(), next.tlsMode)) fail(Txt::ErrTlsMode);
  else if (next.tlsMode == TlsMode::Fingerprint && fp.isEmpty()) fail(Txt::ErrNeedFingerprint);

  String tz = server.arg(F("tz"));
  tz.trim();
  if (!settings::validTz(tz)) fail(Txt::ErrTz);
  next.tz = tz;

  long bright = server.arg(F("brightness")).toInt();
  long refresh = server.arg(F("refresh_min")).toInt();
  if (bright < 0 || bright > 100) fail(Txt::ErrBrightness);
  if (refresh < 1 || refresh > 1440) fail(Txt::ErrRefresh);
  next.brightness = static_cast<uint8_t>(constrain(bright, 0L, 100L));
  next.refreshMin = static_cast<uint16_t>(constrain(refresh, 1L, 1440L));

  if (!settings::parseLayout(server.arg(F("layout")).c_str(), next.layout)) fail(Txt::ErrLayout);
  if (!settings::parseLayoutMode(server.arg(F("layout_mode")).c_str(), next.layoutMode)) fail(Txt::ErrLayoutMode);
  long cycle = server.arg(F("cycle_sec")).toInt();
  if (!settings::validCycleSec(cycle)) fail(Txt::ErrCycle);
  else next.cycleSec = static_cast<uint16_t>(cycle);

  String pass = server.arg(F("admin_pass"));
  bool passChanged = false;
  if (pass.length()) {
    if (pass != server.arg(F("admin_pass2"))) fail(Txt::ErrPassMismatch);
    else if (pass.length() < settings::MIN_ADMIN_PASS) fail(Txt::ErrPassShort);
    else {
      next.adminPass = pass;
      passChanged = true;
    }
  }

  if (err.length()) return sendPage(400, lang, [&](Html &out) { errorPage(out, nullptr, err); });
  if (!settings::save(next)) return sendMessage(500, lang, Txt::ErrFlash, true, true);
  *app->settings = next;
  if (passChanged) {
    sendMessage(200, lang, Txt::SavedRestart);
    delay(500);
    ESP.restart();
  }
  app->applySettings();
  app->requestFetch();
  sendRedirect(F("/"));
}

void handleReboot() {
  if (!authed()) return;
  if (!csrfOk()) return sendMessage(403, adminLang(), Txt::BadToken);
  sendMessage(200, adminLang(), Txt::Restarting);
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
  if (!csrfOk()) return sendMessage(403, adminLang(), Txt::BadToken);
  sendMessage(200, adminLang(), Txt::WifiForgotten);
  delay(500);
  forgetWifi();
  ESP.restart();
}

void handleFactoryReset() {
  if (!authed()) return;
  if (!csrfOk()) return sendMessage(403, adminLang(), Txt::BadToken);
  sendMessage(200, adminLang(), Txt::FactoryResetDone);
  delay(500);
  forgetWifi();
  settings::factoryReset();
  ESP.restart();
}

void handleUpdatePage() {
  if (!authed()) return;
  sendPage(200, adminLang(), [](Html &out) { updatePage(out, csrf); });
}

void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadFailed = false;
    uploadError = "";
    uploadAllowed = server.authenticate(ADMIN_USER, app->settings->adminPass.c_str()) && csrfOk();
    if (!uploadAllowed) return;
    ui::showMessage(Msg::Updating, up.filename.c_str());
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
  if (!uploadAllowed) return sendMessage(403, adminLang(), Txt::BadUploadToken);
  if (uploadFailed || Update.hasError()) {
    ui::invalidate();
    return sendPage(500, adminLang(), [](Html &out) { errorPage(out, text(out.lang(), Txt::UpdateFailed), uploadError); });
  }
  ui::showMessage(Msg::Done, Msg::Restarting);
  sendMessage(200, adminLang(), Txt::UpdateDone);
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
  j += F(",\"lang\":\"");
  j += settings::langName(app->settings->lang);
  j += F("\",\"usage_age_s\":");
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

Settings *portalCfg = nullptr;
const String *portalAp = nullptr;
Lang portalSavedLang = Lang::Ko;

void sendPortal(PGM_P message) {
  const NetworkList nets{WiFi.scanComplete(), [](int i) { return WiFi.SSID(i); }, [](int i) { return WiFi.RSSI(i); }};
  sendPage(200, portalCfg->lang, [&](Html &out) { portalPage(out, nets, message); });
}

void portalError(Txt message) { sendPortal(text(portalCfg->lang, message)); }

void showPortalSetup() { ui::showSetup(portalAp->c_str(), portalCfg->apPass.c_str(), portalCfg->adminPass.c_str()); }

void handlePortalSave() {
  Lang lang = portalCfg->lang;
  settings::parseLang(server.arg(F("lang")).c_str(), lang);
  if (lang != portalCfg->lang) {
    portalCfg->lang = lang;
    ui::setLang(lang);
    showPortalSetup();
  }
  String ssid = server.arg(F("ssid"));
  if (ssid.isEmpty()) ssid = server.arg(F("ssid_pick"));
  String pass = server.arg(F("pass"));
  String admin = server.arg(F("admin_pass"));
  if (ssid.isEmpty() || ssid.length() > 32) return portalError(Txt::ErrSsid);
  if (pass.length() && (pass.length() < 8 || pass.length() > 63)) return portalError(Txt::ErrWifiPass);
  if (admin.length() && admin.length() < settings::MIN_ADMIN_PASS) return portalError(Txt::ErrAdminShort);
  if (admin.length() || portalCfg->lang != portalSavedLang) {
    if (admin.length()) portalCfg->adminPass = admin;
    if (!settings::save(*portalCfg)) return portalError(Txt::ErrSaveSettings);
    portalSavedLang = portalCfg->lang;
  }
  sendMessage(200, portalCfg->lang, Txt::PortalSaved);
  delay(500);
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.persistent(false);
  delay(500);
  ESP.restart();
}

}  // namespace

[[noreturn]] void runPortal(Settings &s, const String &apName, uint32_t timeoutMs) {
  portalCfg = &s;
  portalAp = &apName;
  portalSavedLang = s.lang;
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
  showPortalSetup();

  server.on(F("/"), HTTP_GET, [] { sendPortal(nullptr); });
  server.on(F("/rescan"), HTTP_GET, [] {
    WiFi.scanNetworks(true);
    delay(4000);
    sendRedirect(F("/"));
  });
  server.on(F("/save"), HTTP_POST, handlePortalSave);
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
