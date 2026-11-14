#include "lcd.h"

#include <SPI.h>

namespace {

constexpr uint32_t SPI_HZ = 40000000;
constexpr uint8_t MAX_GLYPHS = 40;

uint8_t madctlFor(uint8_t rotation, bool bgr) {
  static const uint8_t table[4] = {0x00, 0x60, 0xC0, 0xA0};
  return table[rotation & 3] | (bgr ? 0x08 : 0x00);
}

uint16_t blend(uint16_t bg, uint16_t fg, uint8_t alpha16) {
  uint32_t a = alpha16 * 17;
  uint32_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (255 - a)) / 255;
  uint32_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (255 - a)) / 255;
  uint32_t b = ((fg & 0x1F) * a + (bg & 0x1F) * (255 - a)) / 255;
  return (r << 11) | (g << 5) | b;
}

}  // namespace

uint16_t nextCodepoint(const char *&s) {
  uint8_t c = static_cast<uint8_t>(*s);
  if (!c) return 0;
  s++;
  if (c < 0x80) return c;
  int extra = (c >= 0xE0) ? 2 : (c >= 0xC0 ? 1 : 0);
  uint16_t cp = c & (extra == 2 ? 0x0F : 0x1F);
  if (c >= 0xF0) {
    while ((static_cast<uint8_t>(*s) & 0xC0) == 0x80) s++;
    return '?';
  }
  for (int i = 0; i < extra; i++) {
    uint8_t n = static_cast<uint8_t>(*s);
    if ((n & 0xC0) != 0x80) return '?';
    cp = (cp << 6) | (n & 0x3F);
    s++;
  }
  return extra ? cp : '?';
}

void Lcd::begin(const LcdPins &pins) {
  p_ = pins;
  hwSpi_ = (p_.mosi == 13 && p_.sclk == 14);
  colOff_ = (p_.rotation & 3) == 3 ? 80 : 0;
  rowOff_ = (p_.rotation & 3) == 2 ? 80 : 0;

  if (hwSpi_) {
    SPI.begin();
  } else {
    pinMode(p_.mosi, OUTPUT);
    pinMode(p_.sclk, OUTPUT);
    digitalWrite(p_.sclk, p_.spiMode >= 2 ? HIGH : LOW);
  }
  if (p_.cs >= 0) {
    pinMode(p_.cs, OUTPUT);
    digitalWrite(p_.cs, HIGH);
  }
  pinMode(p_.dc, OUTPUT);
  digitalWrite(p_.dc, HIGH);
  if (p_.bl >= 0) {
    pinMode(p_.bl, OUTPUT);
    digitalWrite(p_.bl, p_.blActiveLow ? HIGH : LOW);
  }
  if (p_.rst >= 0) {
    pinMode(p_.rst, OUTPUT);
    digitalWrite(p_.rst, HIGH);
    delay(5);
    digitalWrite(p_.rst, LOW);
    delay(20);
    digitalWrite(p_.rst, HIGH);
    delay(150);
  }

  command(0x01);
  delay(150);
  command(0x11);
  delay(120);
  const uint8_t colmod = 0x55;
  commandData(0x3A, &colmod, 1);
  const uint8_t madctl = madctlFor(p_.rotation, p_.bgr);
  commandData(0x36, &madctl, 1);
  command(p_.invert ? 0x21 : 0x20);
  command(0x13);
  fillScreen(0);
  command(0x29);
  delay(20);
}

void Lcd::setBrightness(uint8_t percent) {
  if (p_.bl < 0) return;
  if (percent > 100) percent = 100;
  analogWriteRange(255);
  uint16_t duty = static_cast<uint16_t>(percent) * 255 / 100;
  analogWrite(p_.bl, p_.blActiveLow ? 255 - duty : duty);
}

void Lcd::startWrite() {
  static const uint8_t modes[4] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
  if (hwSpi_) SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, modes[p_.spiMode & 3]));
  if (p_.cs >= 0) digitalWrite(p_.cs, LOW);
}

void Lcd::endWrite() {
  if (p_.cs >= 0) digitalWrite(p_.cs, HIGH);
  if (hwSpi_) SPI.endTransaction();
}

void Lcd::writeByte(uint8_t b) {
  if (hwSpi_) {
    SPI.write(b);
    return;
  }
  bool idleHigh = p_.spiMode >= 2;
  for (uint8_t mask = 0x80; mask; mask >>= 1) {
    if (idleHigh) digitalWrite(p_.sclk, LOW);
    digitalWrite(p_.mosi, (b & mask) ? HIGH : LOW);
    digitalWrite(p_.sclk, HIGH);
    if (!idleHigh) digitalWrite(p_.sclk, LOW);
  }
}

void Lcd::writeBytes(const uint8_t *data, size_t n) {
  if (hwSpi_) {
    // SPI.writeBytes reads 32-bit words, so stage data through a word-aligned buffer.
    uint32_t chunk[16];
    while (n) {
      size_t len = n > sizeof(chunk) ? sizeof(chunk) : n;
      memcpy(chunk, data, len);
      SPI.writeBytes(reinterpret_cast<const uint8_t *>(chunk), len);
      data += len;
      n -= len;
    }
    return;
  }
  for (size_t i = 0; i < n; i++) writeByte(data[i]);
}

void Lcd::command(uint8_t cmd) { commandData(cmd, nullptr, 0); }

void Lcd::commandData(uint8_t cmd, const uint8_t *data, size_t n) {
  startWrite();
  digitalWrite(p_.dc, LOW);
  writeByte(cmd);
  digitalWrite(p_.dc, HIGH);
  if (n) writeBytes(data, n);
  endWrite();
}

void Lcd::setWindow(int16_t x, int16_t y, int16_t w, int16_t h) {
  uint16_t x0 = x + colOff_, x1 = x + w - 1 + colOff_;
  uint16_t y0 = y + rowOff_, y1 = y + h - 1 + rowOff_;
  const uint8_t cas[4] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
  const uint8_t ras[4] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
  commandData(0x2A, cas, 4);
  commandData(0x2B, ras, 4);
}

void Lcd::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  setWindow(x, y, w, h);
  startWrite();
  digitalWrite(p_.dc, LOW);
  writeByte(0x2C);
  digitalWrite(p_.dc, HIGH);
  uint32_t count = static_cast<uint32_t>(w) * h;
  if (hwSpi_) {
    uint8_t pattern[2] = {uint8_t(color >> 8), uint8_t(color)};
    SPI.writePattern(pattern, 2, count);
  } else {
    while (count--) {
      writeByte(color >> 8);
      writeByte(color);
    }
  }
  endWrite();
}

const Glyph *Lcd::findGlyph(const Font &font, uint16_t code, Glyph &out) {
  int lo = 0, hi = font.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t c = pgm_read_word(&font.glyphs[mid].code);
    if (c == code) {
      memcpy_P(&out, &font.glyphs[mid], sizeof(Glyph));
      return &out;
    }
    if (c < code) lo = mid + 1; else hi = mid - 1;
  }
  return code == '?' ? nullptr : findGlyph(font, '?', out);
}

int16_t Lcd::textWidth(const Font &font, const char *utf8) {
  int16_t w = 0;
  Glyph g;
  for (const char *s = utf8; *s;) {
    uint16_t cp = nextCodepoint(s);
    if (findGlyph(font, cp, g)) w += g.adv;
  }
  return w;
}

void Lcd::drawText(const Font &font, int16_t x, int16_t y, int16_t boxW, Align align, const char *utf8, uint16_t fg,
                   uint16_t bg) {
  if (x < 0 || y < 0 || boxW <= 0 || x + boxW > W) return;
  const int16_t boxH = lineHeight(font);
  if (y + boxH > H) return;

  Glyph glyphs[MAX_GLYPHS];
  int16_t pens[MAX_GLYPHS];
  uint8_t n = 0;
  int16_t width = 0;
  for (const char *s = utf8; *s && n < MAX_GLYPHS;) {
    uint16_t cp = nextCodepoint(s);
    if (!findGlyph(font, cp, glyphs[n])) continue;
    pens[n] = width;
    width += glyphs[n].adv;
    n++;
  }
  int16_t start = 0;
  if (align == ALIGN_CENTER) start = (boxW - width) / 2;
  if (align == ALIGN_RIGHT) start = boxW - width;
  if (start < 0) start = 0;

  uint16_t palette[16];
  for (uint8_t i = 0; i < 16; i++) palette[i] = blend(bg, fg, i);

  uint8_t line[W * 2];
  setWindow(x, y, boxW, boxH);
  startWrite();
  digitalWrite(p_.dc, LOW);
  writeByte(0x2C);
  digitalWrite(p_.dc, HIGH);
  for (int16_t row = 0; row < boxH; row++) {
    for (int16_t i = 0; i < boxW; i++) {
      line[2 * i] = bg >> 8;
      line[2 * i + 1] = bg & 0xFF;
    }
    int16_t gy = row - font.ascent;
    for (uint8_t k = 0; k < n; k++) {
      const Glyph &g = glyphs[k];
      int16_t r = gy - g.yo;
      if (r < 0 || r >= g.h) continue;
      int16_t gx = start + pens[k] + g.xo;
      for (int16_t c = 0; c < g.w; c++) {
        int16_t px = gx + c;
        if (px < 0 || px >= boxW) continue;
        uint32_t idx = static_cast<uint32_t>(r) * g.w + c;
        uint8_t b = pgm_read_byte(font.bitmap + g.offset + idx / 2);
        uint8_t a = (idx & 1) ? (b & 0x0F) : (b >> 4);
        if (!a) continue;
        uint16_t col = palette[a];
        line[2 * px] = col >> 8;
        line[2 * px + 1] = col & 0xFF;
      }
    }
    writeBytes(line, boxW * 2);
  }
  endWrite();
}
