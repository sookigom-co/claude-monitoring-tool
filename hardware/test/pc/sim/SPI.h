// Host-side SPI that feeds every byte to the simulated ST7789 in render_screens.cpp.
#pragma once
#include <Arduino.h>

enum { SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3 };

struct SPISettings {
  SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass {
 public:
  void begin() {}
  void beginTransaction(const SPISettings &) {}
  void endTransaction() {}
  void write(uint8_t b);
  void writeBytes(const uint8_t *data, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) write(data[i]);
  }
  void writePattern(const uint8_t *data, uint8_t size, uint32_t repeat) {
    while (repeat--) writeBytes(data, size);
  }
};

extern SPIClass SPI;
