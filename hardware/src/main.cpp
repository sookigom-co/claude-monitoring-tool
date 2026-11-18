#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <time.h>

#include "board.h"
#include "layout_cycle.h"
#include "settings.h"
#include "ui.h"
#include "usage.h"
#include "web.h"

namespace {

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t PORTAL_RETRY_TIMEOUT_MS = 5 * 60 * 1000;
constexpr uint32_t FAILED_FETCH_RETRY_MS = 2 * 60 * 1000;

Lcd lcd;
Settings cfg;
UsageState usageState;
AppHooks hooks;
bool fetchRequested = true;
uint32_t lastUiMs = 0;
LayoutCycle layoutCycle;

String chipSuffix() {
  char buf[8];
  snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned>(ESP.getChipId() & 0xFFFF));
  return buf;
}

void applySettings() {
  lcd.setBrightness(cfg.brightness);
  ui::setLang(cfg.lang);
  configTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com");
  // Saving restarts the cycle at the chosen design so the change shows at once.
  layoutCycle.restart(millis());
}

void requestFetch() { fetchRequested = true; }

bool connectSavedWifi() {
  WiFi.begin();
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - started > WIFI_CONNECT_TIMEOUT_MS) return false;
    delay(100);
  }
  return true;
}

void setupOta(const String &hostname) {
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword(cfg.adminPass.c_str());
  ArduinoOTA.onStart([] { ui::showMessage(Msg::Updating, "ArduinoOTA"); });
  ArduinoOTA.onEnd([] { ui::showMessage(Msg::Done, Msg::Restarting); });
  ArduinoOTA.onError([](ota_error_t error) {
    char buf[24];
    snprintf(buf, sizeof(buf), "OTA error %d", static_cast<int>(error));
    ui::showMessage(Msg::Error, buf);
    delay(2000);
    ui::invalidate();
  });
  ArduinoOTA.begin(true);
  MDNS.addService("http", "tcp", 80);
}

void serviceUsage() {
  if (cfg.usageUrl.isEmpty() || WiFi.status() != WL_CONNECTED) return;
  uint32_t interval = static_cast<uint32_t>(cfg.refreshMin) * 60000UL;
  if (usageState.lastAttemptMs && !usageState.lastOk && interval > FAILED_FETCH_RETRY_MS) interval = FAILED_FETCH_RETRY_MS;
  if (!fetchRequested && usageState.lastAttemptMs && millis() - usageState.lastAttemptMs < interval) return;
  usageState.waitingForTime = settings::needsValidTime(cfg) && time(nullptr) < usage::VALID_EPOCH;
  if (usageState.waitingForTime) return;
  fetchRequested = false;
  bool ok = usage::fetch(cfg, usageState, settings::showsHourly(cfg));
  Serial.printf("usage fetch: %s %s\n", ok ? "ok" : "failed", usageState.lastError.c_str());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("\nSmartClock %s\n", FW_VERSION);

  lcd.begin(boardLcdPins());
  if (!settings::begin(cfg)) Serial.println("LittleFS mount failed; settings are not persisted");
  lcd.setBrightness(cfg.brightness);
  ui::setLang(cfg.lang);
  ui::begin(lcd);

  const String suffix = chipSuffix();
  const String hostname = "smartclock-" + suffix;
  const String apName = "SmartClock-" + suffix;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.setAutoReconnect(true);

  if (WiFi.SSID().isEmpty()) web::runPortal(cfg, apName, 0);
  ui::showMessage(Msg::ConnectingWifi, WiFi.SSID().c_str());
  if (!connectSavedWifi()) web::runPortal(cfg, apName, PORTAL_RETRY_TIMEOUT_MS);
  Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());

  applySettings();
  setupOta(hostname);
  hooks = AppHooks{&cfg, &usageState, applySettings, requestFetch};
  web::beginAdmin(hooks);

  char ip[40];
  snprintf(ip, sizeof(ip), "http://%s", WiFi.localIP().toString().c_str());
  ui::showMessage("SmartClock", ip, hostname.c_str(), FW_VERSION);
  delay(3000);
  ui::invalidate();
}

void loop() {
  ArduinoOTA.handle();
  web::handle();
  serviceUsage();
  if (millis() - lastUiMs >= 100) {
    lastUiMs = millis();
    ui::setLayout(layoutCycle.active(cfg, millis()));
    ui::draw(time(nullptr) > usage::VALID_EPOCH, WiFi.status() == WL_CONNECTED, settings::tokenWarning(cfg), usageState,
             !cfg.usageUrl.isEmpty());
  }
  delay(5);
}
