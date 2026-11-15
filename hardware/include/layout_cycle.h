#pragma once
#include "settings.h"

// Picks the screen design to show: the configured one, or in cycle mode the next one every cycleSec seconds.
struct LayoutCycle {
  uint8_t step = 0;
  uint32_t sinceMs = 0;

  void restart(uint32_t nowMs) {
    step = 0;
    sinceMs = nowMs;
  }

  Layout active(const Settings &s, uint32_t nowMs) {
    if (s.layoutMode == LayoutMode::Fixed) return s.layout;
    const uint32_t periodMs = static_cast<uint32_t>(s.cycleSec) * 1000UL;
    // Advance by whole periods so a late loop iteration does not shift later switches.
    while (nowMs - sinceMs >= periodMs) {
      sinceMs += periodMs;
      step = (step + 1) % LAYOUT_COUNT;
    }
    return static_cast<Layout>((static_cast<uint8_t>(s.layout) + step) % LAYOUT_COUNT);
  }
};
