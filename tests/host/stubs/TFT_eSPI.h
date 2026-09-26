#pragma once
#include <stdint.h>

#define TFT_BLACK  0x0000
#define TFT_WHITE  0xFFFF
#define TFT_RED    0xF800
#define TFT_YELLOW 0xFFE0

class TFT_eSPI {
public:
    void fillScreen(uint32_t) {}
    void setTextColor(uint32_t, uint32_t = 0) {}
    void setTextFont(uint8_t) {}
    void setTextSize(uint8_t) {}
    void setCursor(int16_t, int16_t) {}
    void println(const char* = "") {}
    void print(const char* = "") {}
};
