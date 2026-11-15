// Host-side stand-in for the Arduino APIs used by lcd.cpp and ui.cpp, on top of the parser test shim.
#pragma once
#include "../Arduino.h"

#include <ctime>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define MSBFIRST 1
#define pgm_read_word(addr) (*reinterpret_cast<const uint16_t *>(addr))
#define memcpy_P memcpy

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
void delay(unsigned long ms);
unsigned long millis();
void analogWriteRange(uint32_t range);
void analogWrite(uint8_t pin, int value);
