#pragma once
#include "lcd.h"

#ifndef LCD_MOSI
#define LCD_MOSI 13
#endif
#ifndef LCD_SCLK
#define LCD_SCLK 14
#endif
#ifndef LCD_CS
#define LCD_CS -1
#endif
#ifndef LCD_DC
#define LCD_DC 0
#endif
#ifndef LCD_RST
#define LCD_RST 2
#endif
#ifndef LCD_BL
#define LCD_BL 5
#endif
#ifndef LCD_BL_ACTIVE_LOW
#define LCD_BL_ACTIVE_LOW 1
#endif
#ifndef LCD_SPI_MODE
#define LCD_SPI_MODE 3
#endif
#ifndef LCD_INVERT
#define LCD_INVERT 1
#endif
#ifndef LCD_BGR
#define LCD_BGR 0
#endif
#ifndef LCD_ROTATION
#define LCD_ROTATION 0
#endif

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif

inline LcdPins boardLcdPins() {
  return LcdPins{LCD_MOSI, LCD_SCLK, LCD_CS, LCD_DC, LCD_RST, LCD_BL, LCD_BL_ACTIVE_LOW != 0,
                 LCD_SPI_MODE, LCD_INVERT != 0, LCD_BGR != 0, LCD_ROTATION};
}
