#pragma once
#include <Arduino.h>

struct CaRoot {
  const char *name;
  const uint8_t *der;
  size_t len;
};

extern const CaRoot CA_ROOTS[];
extern const size_t CA_ROOT_COUNT;
