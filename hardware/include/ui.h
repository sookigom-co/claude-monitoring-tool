#pragma once
#include "i18n.h"
#include "lcd.h"
#include "settings.h"
#include "usage.h"

namespace ui {

void begin(Lcd &lcd);
void showMessage(const char *title, const char *line1 = "", const char *line2 = "", const char *line3 = "");
void showMessage(Msg title, const char *line1 = "");
void showMessage(Msg title, Msg line1);
void showSetup(const char *apName, const char *apPass, const char *adminPass);
void invalidate();
// Selects the screen design; the next draw() repaints the whole screen only when it changed.
void setLayout(Layout layout);
// Selects the screen language; the next draw() repaints the whole screen only when it changed.
void setLang(Lang lang);
// Redraws only the parts of the clock screen whose content changed since the last call.
void draw(bool timeValid, bool wifiUp, TokenWarning warning, const UsageState &state, bool configured);

}  // namespace ui
