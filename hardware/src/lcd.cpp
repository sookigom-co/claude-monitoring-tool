#include "lcd.h"

#include <SPI.h>

namespace {

constexpr uint32_t SPI_HZ = 40000000;
constexpr uint8_t MAX_GLYPHS = 40;
constexpr uint8_t MAX_SPANS = 4;

uint8_t madctlFor(uint8_t rotation, bool bgr) {
  static const uint8_t table[4] = {0x00, 0x60, 0xC0, 0xA0};
  return table[rotation & 3] | (bgr ? 0x08 : 0x00);
}

uint16_t blend255(uint16_t bg, uint16_t fg, uint32_t a) {
  uint32_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * (255 - a)) / 255;
  uint32_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * (255 - a)) / 255;
  uint32_t b = ((fg & 0x1F) * a + (bg & 0x1F) * (255 - a)) / 255;
  return (r << 11) | (g << 5) | b;
}

uint16_t blend(uint16_t bg, uint16_t fg, uint8_t alpha16) { return blend255(bg, fg, alpha16 * 17); }

int16_t clampCoord(float v, int16_t size) { return v < 0 ? 0 : (v > size - 1 ? size - 1 : static_cast<int16_t>(v)); }

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Clock angle of (dx, dy) with y pointing down, in [0, 360). Polynomial atan, error below 0.001 degrees.
float clockDegrees(float dx, float dy) {
  const float x = -dy, y = dx;
  const float ax = fabsf(x), ay = fabsf(y);
  const float mx = ax > ay ? ax : ay;
  if (mx == 0) return 0;
  const float a = (ax < ay ? ax : ay) / mx;
  const float s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0) r = 3.14159274f - r;
  if (y < 0) r = -r;
  float deg = r * 57.2957795f;
  return deg < 0 ? deg + 360 : deg;
}

// Antialiased coverage of the pixel centred at (px, py) by a horizontal capsule from x0 to x1 around cy.
float capsuleCoverage(float px, float py, float x0, float x1, float cy, float r) {
  float dx = 0;
  if (px < x0 + r) dx = x0 + r - px;
  else if (px > x1 - r) dx = px - (x1 - r);
  const float dy = py - cy;
  return clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy));
}

struct SpanShape {
  float start;
  float sweep;
  bool closed;
  float capX[2];
  float capY[2];
  uint16_t color;
};

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

void Lcd::writeRun(int16_t x, int16_t y, int16_t w, const uint8_t *pixels) {
  setWindow(x, y, w, 1);
  startWrite();
  digitalWrite(p_.dc, LOW);
  writeByte(0x2C);
  digitalWrite(p_.dc, HIGH);
  writeBytes(pixels, w * 2);
  endWrite();
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

void Lcd::drawRing(float cx, float cy, float rMid, float halfW, const ArcSpan *spans, uint8_t n, uint16_t bg) {
  SpanShape shapes[MAX_SPANS];
  uint8_t count = 0;
  for (uint8_t i = 0; i < n && count < MAX_SPANS; i++) {
    if (spans[i].sweep <= 0) continue;
    SpanShape &sh = shapes[count++];
    sh.start = spans[i].start;
    sh.sweep = spans[i].sweep;
    sh.closed = sh.sweep >= 360;
    sh.color = spans[i].color;
    for (uint8_t e = 0; e < 2; e++) {
      const float rad = (sh.start + (e ? sh.sweep : 0)) * 0.0174532925f;
      sh.capX[e] = cx + rMid * sinf(rad);
      sh.capY[e] = cy - rMid * cosf(rad);
    }
  }
  if (!count) return;

  // Every covered pixel lies within halfW + 0.5 of the centre line, caps included.
  const float band = halfW + 1;
  const float outer = rMid + band, inner = rMid - band;
  const int16_t y0 = clampCoord(floorf(cy - outer), H);
  const int16_t y1 = clampCoord(ceilf(cy + outer), H);
  uint8_t line[W * 2];
  for (int16_t y = y0; y <= y1; y++) {
    const float dy = y + 0.5f - cy;
    if (dy * dy >= outer * outer) continue;
    const float xo = sqrtf(outer * outer - dy * dy);
    const int16_t xa = clampCoord(floorf(cx - xo), W);
    const int16_t xb = clampCoord(ceilf(cx + xo), W);
    // Pixels strictly inside the inner edge are skipped without per-pixel maths.
    int16_t holeA = xb + 1, holeB = xb;
    if (inner > 0 && dy * dy < inner * inner) {
      const float xi = sqrtf(inner * inner - dy * dy);
      holeA = ceilf(cx - xi);
      holeB = floorf(cx + xi) - 1;
    }
    int16_t runX = -1;
    for (int16_t x = xa; x <= xb + 1; x++) {
      bool covered = false;
      uint16_t color = bg;
      if (x >= holeA && x <= holeB) {
        // Flush below, then jump over the hole.
      } else if (x <= xb) {
        const float dx = x + 0.5f - cx;
        const float d = sqrtf(dx * dx + dy * dy);
        const float radial = clamp01(halfW + 0.5f - fabsf(d - rMid));
        if (radial > 0) {
          const float deg = clockDegrees(dx, dy);
          for (uint8_t i = 0; i < count; i++) {
            const SpanShape &sh = shapes[i];
            float cov = 0;
            float rel = deg - sh.start;
            while (rel < 0) rel += 360;
            while (rel >= 360) rel -= 360;
            if (sh.closed || rel <= sh.sweep) cov = radial;
            if (!sh.closed) {
              for (uint8_t e = 0; e < 2; e++) {
                const float ex = x + 0.5f - sh.capX[e], ey = y + 0.5f - sh.capY[e];
                const float capCov = clamp01(halfW + 0.5f - sqrtf(ex * ex + ey * ey));
                if (capCov > cov) cov = capCov;
              }
            }
            if (cov > 0) {
              covered = true;
              color = blend255(color, sh.color, static_cast<uint32_t>(cov * 255 + 0.5f));
            }
          }
        }
      }
      if (covered) {
        if (runX < 0) runX = x;
        line[2 * (x - runX)] = color >> 8;
        line[2 * (x - runX) + 1] = color & 0xFF;
      } else if (runX >= 0) {
        writeRun(runX, y, x - runX, line);
        runX = -1;
      }
      if (x >= holeA && x < holeB) x = holeB;
    }
  }
}

void Lcd::drawBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t fillW, uint16_t fill, uint16_t track,
                  uint16_t bg) {
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > W || y + h > H) return;
  if (fillW > w) fillW = w;
  const float r = h / 2.0f, cy = y + r;
  // A fill shorter than the bar height keeps the track's rounded start and ends in a straight edge.
  const bool roundFill = fillW >= h;
  uint8_t line[W * 2];
  setWindow(x, y, w, h);
  startWrite();
  digitalWrite(p_.dc, LOW);
  writeByte(0x2C);
  digitalWrite(p_.dc, HIGH);
  for (int16_t row = 0; row < h; row++) {
    const float py = y + row + 0.5f;
    for (int16_t i = 0; i < w; i++) {
      const float px = x + i + 0.5f;
      const float trackCov = capsuleCoverage(px, py, x, x + w, cy, r);
      uint16_t color = blend255(bg, track, static_cast<uint32_t>(trackCov * 255 + 0.5f));
      if (fillW > 0) {
        float fillCov = roundFill ? capsuleCoverage(px, py, x, x + fillW, cy, r) : clamp01(x + fillW - px + 0.5f);
        if (fillCov > trackCov) fillCov = trackCov;
        color = blend255(color, fill, static_cast<uint32_t>(fillCov * 255 + 0.5f));
      }
      line[2 * i] = color >> 8;
      line[2 * i + 1] = color & 0xFF;
    }
    writeBytes(line, w * 2);
  }
  endWrite();
}
