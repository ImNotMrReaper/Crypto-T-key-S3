#include "ui_theme.h"
#include <Preferences.h>

HomeTheme homeTheme;

void HomeTheme::load() {
    Preferences p;
    if (!p.begin("ui_theme", true)) return;
    uint32_t v = p.getUInt("home_rgb", rgb());
    r = v >> 16; g = v >> 8; b = v;
    uint8_t f = p.getUChar("home_fx", fx);
    fx = f <= HOME_FX_RAINBOW ? (HomeEffect)f : HOME_FX_BREATHE;
    p.end();
}

void HomeTheme::save() const {
    Preferences p;
    if (!p.begin("ui_theme", false)) return;
    p.putUInt("home_rgb", rgb());
    p.putUChar("home_fx", fx);
    p.end();
}
