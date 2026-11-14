// Host-side stand-in for the few Arduino APIs used by usage_model.cpp and fonts_gen.cpp.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define PROGMEM
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t *>(addr))

class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string &s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
};

template <typename T, typename L, typename H>
T constrain(T v, L lo, H hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
