#pragma once
#include <Arduino.h>

struct Glyph {
  uint16_t code;
  uint32_t offset;
  uint8_t w;
  uint8_t h;
  int8_t xo;
  int8_t yo;
  uint8_t adv;
};

struct Font {
  const Glyph *glyphs;
  uint16_t count;
  const uint8_t *bitmap;
  uint8_t ascent;
  uint8_t descent;
};

// Clock digits: "0-9", ":" and "-" only.
extern const Font FONT_L;
extern const Font FONT_M;
extern const Font FONT_S;
extern const Font FONT_XS;
// Gauge percentages: digits, "%" and "-" only, so its line box is no taller than the digits.
extern const Font FONT_P;
extern const Font FONT_PS;
