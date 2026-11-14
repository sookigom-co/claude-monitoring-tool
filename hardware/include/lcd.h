#pragma once
#include <Arduino.h>
#include "fonts.h"

struct LcdPins {
  int8_t mosi;
  int8_t sclk;
  int8_t cs;
  int8_t dc;
  int8_t rst;
  int8_t bl;
  bool blActiveLow;
  uint8_t spiMode;
  bool invert;
  bool bgr;
  uint8_t rotation;
};

enum Align : uint8_t { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Minimal ST7789 240x240 driver with hardware SPI on GPIO13/14 and bit-banged SPI on any other pins.
class Lcd {
 public:
  static constexpr int16_t W = 240;
  static constexpr int16_t H = 240;

  void begin(const LcdPins &pins);
  void setBrightness(uint8_t percent);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void fillScreen(uint16_t color) { fillRect(0, 0, W, H, color); }
  int16_t textWidth(const Font &font, const char *utf8);
  static int16_t lineHeight(const Font &font) { return font.ascent + font.descent; }
  // Draws text into a box of boxW x lineHeight, repainting the whole box so updates never flicker.
  void drawText(const Font &font, int16_t x, int16_t y, int16_t boxW, Align align, const char *utf8, uint16_t fg,
                uint16_t bg);

 private:
  LcdPins p_{};
  bool hwSpi_ = false;
  uint16_t colOff_ = 0;
  uint16_t rowOff_ = 0;

  void startWrite();
  void endWrite();
  void writeByte(uint8_t b);
  void writeBytes(const uint8_t *data, size_t n);
  void command(uint8_t cmd);
  void commandData(uint8_t cmd, const uint8_t *data, size_t n);
  void setWindow(int16_t x, int16_t y, int16_t w, int16_t h);
  static const Glyph *findGlyph(const Font &font, uint16_t code, Glyph &out);
};

uint16_t nextCodepoint(const char *&s);
