#include <Arduino.h>

#include "board.h"
#include "lcd.h"

namespace {

struct Candidate {
  int8_t cs;
  int8_t dc;
  int8_t rst;
  int8_t bl;
};

const Candidate CANDIDATES[] = {
    {-1, 0, 2, 5},  {15, 0, 2, 5},  {-1, 0, 2, 4},  {-1, 0, 2, 12}, {-1, 0, 2, 16}, {-1, 0, 2, 15},
    {-1, 0, -1, 5}, {-1, 0, 16, 5}, {-1, 16, 2, 5}, {-1, 4, 2, 5},  {15, 4, 2, 5},  {-1, 4, 5, 12},
    {15, 4, 5, 12}, {-1, 5, 4, 12}, {15, 2, 4, 5},  {-1, 2, 16, 5},
};
const uint8_t SAFE_GPIOS[] = {0, 2, 4, 5, 12, 15, 16};
const uint8_t SPI_MODES[] = {3, 0};
constexpr uint32_t STEP_MS = 4000;

struct Swatch {
  const char *name;
  uint16_t color;
};
const Swatch SWATCHES[] = {{"RED", rgb565(255, 0, 0)}, {"GREEN", rgb565(0, 255, 0)}, {"BLUE", rgb565(0, 0, 255)}};

Lcd lcd;

void releasePins() {
  for (uint8_t gpio : SAFE_GPIOS) pinMode(gpio, INPUT);
}

void runStep(uint16_t step, const Candidate &c, uint8_t mode, bool blActiveLow) {
  const Swatch &sw = SWATCHES[step % 3];
  Serial.printf("[%02u] CS=%d DC=%d RST=%d BL=%d BL_ACTIVE_LOW=%d SPI_MODE=%u expect=%s\n", step, c.cs, c.dc, c.rst,
                c.bl, blActiveLow, mode, sw.name);
  releasePins();
  LcdPins pins = boardLcdPins();
  pins.cs = c.cs;
  pins.dc = c.dc;
  pins.rst = c.rst;
  pins.bl = c.bl;
  pins.blActiveLow = blActiveLow;
  pins.spiMode = mode;
  lcd.begin(pins);
  lcd.setBrightness(100);
  lcd.fillScreen(sw.color);

  char num[6], line1[40], line2[40];
  snprintf(num, sizeof(num), "%02u", step);
  snprintf(line1, sizeof(line1), "DC%d RST%d CS%d BL%d", c.dc, c.rst, c.cs, c.bl);
  snprintf(line2, sizeof(line2), "MODE%u BL-%s %s", mode, blActiveLow ? "LOW" : "HIGH", sw.name);
  lcd.drawText(FONT_M, 10, 10, 220, ALIGN_CENTER, "화면 진단", 0xFFFF, 0x0000);
  lcd.drawText(FONT_L, 40, 60, 160, ALIGN_CENTER, num, 0xFFFF, 0x0000);
  lcd.drawText(FONT_S, 10, 140, 220, ALIGN_CENTER, line1, 0xFFFF, 0x0000);
  lcd.drawText(FONT_S, 10, 170, 220, ALIGN_CENTER, line2, 0xFFFF, 0x0000);
  delay(STEP_MS);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nSmartClock LCD pin diagnostic. Note the step number shown when the screen is readable.");
}

void loop() {
  uint16_t step = 0;
  for (const Candidate &c : CANDIDATES) {
    for (uint8_t mode : SPI_MODES) {
      runStep(step++, c, mode, true);
      runStep(step++, c, mode, false);
    }
  }
  Serial.println("--- cycle complete, restarting ---");
}
