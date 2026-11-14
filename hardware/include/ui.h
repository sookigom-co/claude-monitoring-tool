#pragma once
#include "lcd.h"
#include "usage.h"

namespace ui {

void begin(Lcd &lcd);
void showMessage(const char *title, const char *line1 = "", const char *line2 = "", const char *line3 = "");
void showSetup(const char *apName, const char *apPass, const char *adminPass);
void invalidate();
void drawClock(bool timeValid, bool wifiUp, const char *notice);
void drawUsage(const UsageState &state, bool configured);

}  // namespace ui
