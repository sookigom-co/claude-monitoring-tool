// Host-side stand-in for the few Arduino APIs used by the model, i18n, web page and font sources.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define PROGMEM
#define PGM_P const char *
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t *>(addr))
#define pgm_read_ptr(addr) (*reinterpret_cast<const void *const *>(addr))
#define strncpy_P strncpy
#define snprintf_P snprintf
#define memcpy_P memcpy

class __FlashStringHelper;
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper *>(p))
#define F(s) FPSTR(s)

class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string &s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  String &operator+=(const String &s) {
    append(s);
    return *this;
  }
  String &operator+=(const char *s) {
    append(s);
    return *this;
  }
  String &operator+=(char c) {
    push_back(c);
    return *this;
  }
  String &operator+=(const __FlashStringHelper *s) { return *this += reinterpret_cast<const char *>(s); }
};

template <typename T, typename L, typename H>
T constrain(T v, L lo, H hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
