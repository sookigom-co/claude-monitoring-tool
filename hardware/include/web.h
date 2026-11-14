#pragma once
#include <Arduino.h>

#include "settings.h"
#include "usage.h"

struct AppHooks {
  Settings *settings;
  UsageState *usage;
  void (*applySettings)();
  void (*requestFetch)();
};

namespace web {

// Runs the first-boot Wi-Fi setup portal on a WPA2 soft AP; never returns (restarts on save or timeout).
[[noreturn]] void runPortal(Settings &s, const String &apName, uint32_t timeoutMs);
void beginAdmin(AppHooks &hooks);
void handle();

}  // namespace web
