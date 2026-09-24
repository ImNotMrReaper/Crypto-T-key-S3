#pragma once
#include <Arduino.h>

/**
 * Home screen / home LED theme, chosen in the setup page (NVS "ui_theme").
 * Only the home state is customisable; every other mode keeps its fixed colours
 * so security prompts always look the same.
 */
enum HomeEffect : uint8_t { HOME_FX_SOLID = 0, HOME_FX_BREATHE = 1, HOME_FX_RAINBOW = 2 };

struct HomeTheme {
    uint8_t r = 0x77, g = 0x64, b = 0xD8;    // default: Ubuntu purple #7764D8
    HomeEffect fx = HOME_FX_BREATHE;

    uint32_t rgb() const { return (uint32_t)r << 16 | (uint32_t)g << 8 | b; }
    uint16_t rgb565() const { return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3); }
    void load();
    void save() const;
};

extern HomeTheme homeTheme;
