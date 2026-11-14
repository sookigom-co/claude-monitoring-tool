#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <time.h>

#include "board.h"
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

String chipSuffix() {
  char buf[8];
  snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned>(ESP.getChipId() & 0xFFFF));
  return buf;
}

void applySettings() {
  lcd.setBrightness(cfg.brightness);
  configTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com");
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
  ArduinoOTA.onStart([] { ui::showMessage("업데이트 중", "ArduinoOTA"); });
  ArduinoOTA.onEnd([] { ui::showMessage("완료", "재시작"); });
  ArduinoOTA.onError([](ota_error_t error) {
    char buf[24];
    snprintf(buf, sizeof(buf), "OTA error %d", static_cast<int>(error));
    ui::showMessage("오류", buf);
    delay(2000);
    ui::invalidate();
  });
  ArduinoOTA.begin(true);
  MDNS.addService("http", "tcp", 80);
}

const char *securityNotice() {
  switch (settings::tokenWarning(cfg)) {
    case TokenWarning::NoCertCheck: return "인증서 검증 안 함";
    case TokenWarning::Plaintext: return "토큰 평문 전송";
    default: return "";
  }
}

void serviceUsage() {
  if (cfg.usageUrl.isEmpty() || WiFi.status() != WL_CONNECTED) return;
  uint32_t interval = static_cast<uint32_t>(cfg.refreshMin) * 60000UL;
  if (usageState.lastAttemptMs && !usageState.lastOk && interval > FAILED_FETCH_RETRY_MS) interval = FAILED_FETCH_RETRY_MS;
  if (!fetchRequested && usageState.lastAttemptMs && millis() - usageState.lastAttemptMs < interval) return;
  usageState.waitingForTime = settings::needsValidTime(cfg) && time(nullptr) < usage::VALID_EPOCH;
  if (usageState.waitingForTime) return;
  fetchRequested = false;
  bool ok = usage::fetch(cfg, usageState);
  Serial.printf("usage fetch: %s %s\n", ok ? "ok" : "failed", usageState.lastError.c_str());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("\nSmartClock %s\n", FW_VERSION);

  lcd.begin(boardLcdPins());
  if (!settings::begin(cfg)) Serial.println("LittleFS mount failed; settings are not persisted");
  lcd.setBrightness(cfg.brightness);
  ui::begin(lcd);

  const String suffix = chipSuffix();
  const String hostname = "smartclock-" + suffix;
  const String apName = "SmartClock-" + suffix;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.setAutoReconnect(true);

  if (WiFi.SSID().isEmpty()) web::runPortal(cfg, apName, 0);
  ui::showMessage("Wi-Fi 연결 중", WiFi.SSID().c_str());
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
    ui::drawClock(time(nullptr) > usage::VALID_EPOCH, WiFi.status() == WL_CONNECTED, securityNotice());
    ui::drawUsage(usageState, !cfg.usageUrl.isEmpty());
  }
  delay(5);
}
