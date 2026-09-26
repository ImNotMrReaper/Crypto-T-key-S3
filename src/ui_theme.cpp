#include "ui_theme.h"
#include <Preferences.h>
#include <string.h>
#include <math.h>

HomeTheme homeTheme;

namespace {
static const char* const THEME_NAMESPACE = "ui_theme";
static const char* const THEME_BLOB_KEY = "theme_blob";
static const uint8_t THEME_BLOB_VERSION = 1;

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static uint8_t themeCrc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

struct __attribute__((packed)) CustomModeBlob {
    char name[16];
    uint8_t colorCount;
    uint8_t colors[4][3];
    uint8_t fx;
    uint8_t speed;
};

struct __attribute__((packed)) ThemeBlob {
    uint8_t version;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t fx;
    uint8_t speed;
    uint8_t brightness;
    int8_t activeCustomMode;
    uint8_t customCount;
    CustomModeBlob customModes[MAX_CUSTOM_MODES];
    uint8_t crc;
};

static_assert(sizeof(ThemeBlob) == 134, "ThemeBlob size must be exactly 134 bytes");
} // namespace

ThemeColor HomeTheme::wheel(uint8_t wheelPos) {
    wheelPos = 255 - wheelPos;
    if (wheelPos < 85) {
        return { (uint8_t)(255 - wheelPos * 3), 0, (uint8_t)(wheelPos * 3) };
    }
    if (wheelPos < 170) {
        wheelPos -= 85;
        return { 0, (uint8_t)(wheelPos * 3), (uint8_t)(255 - wheelPos * 3) };
    }
    wheelPos -= 170;
    return { (uint8_t)(wheelPos * 3), (uint8_t)(255 - wheelPos * 3), 0 };
}

// Rotates a colour's hue by `deg`, keeping its saturation and brightness
static void analogous(uint8_t r, uint8_t g, uint8_t b, float deg, uint8_t& orr, uint8_t& og, uint8_t& ob) {
    float fr = r / 255.0f, fg = g / 255.0f, fb = b / 255.0f;
    float mx = fmaxf(fr, fmaxf(fg, fb)), mn = fminf(fr, fminf(fg, fb)), d = mx - mn;
    float h = 0;
    if (d > 0) {
        if (mx == fr) h = 60.0f * fmodf((fg - fb) / d + 6.0f, 6.0f);
        else if (mx == fg) h = 60.0f * ((fb - fr) / d + 2.0f);
        else h = 60.0f * ((fr - fg) / d + 4.0f);
    }
    float s = mx > 0 ? d / mx : 0, v = mx;
    h = fmodf(h + deg, 360.0f);
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c;
    float r1 = 0, g1 = 0, b1 = 0;
    if (h < 60) { r1 = c; g1 = x; } else if (h < 120) { r1 = x; g1 = c; } else if (h < 180) { g1 = c; b1 = x; }
    else if (h < 240) { g1 = x; b1 = c; } else if (h < 300) { r1 = x; b1 = c; } else { r1 = c; b1 = x; }
    orr = (uint8_t)((r1 + m) * 255); og = (uint8_t)((g1 + m) * 255); ob = (uint8_t)((b1 + m) * 255);
}

void HomeTheme::currentFrame(uint32_t nowMs, uint8_t& outR, uint8_t& outG, uint8_t& outB, float& outLevel) const {
    uint8_t curR = activeR(), curG = activeG(), curB = activeB();
    HomeEffect curFx = activeFx();
    uint8_t curSpeed = activeSpeed();
    const CustomMode* cmPtr = nullptr;

    if (!previewActive && activeCustomMode >= 0 && activeCustomMode < customCount) {
        cmPtr = &customModes[activeCustomMode];
        curFx = (HomeEffect)cmPtr->fx;
        curSpeed = cmPtr->speed;
        if (cmPtr->colorCount > 0) {
            curR = cmPtr->colors[0][0];
            curG = cmPtr->colors[0][1];
            curB = cmPtr->colors[0][2];
        }
    }

    uint8_t spd = (curSpeed >= 1 && curSpeed <= 5) ? curSpeed : 3;
    float speedMult = 0.5f + 0.25f * (float)(spd - 1); // 0.5x, 0.75x, 1.0x, 1.25x, 1.5x

    switch (curFx) {
        case HOME_FX_SOLID: {
            outR = curR;
            outG = curG;
            outB = curB;
            outLevel = 0.8f;
            break;
        }

        case HOME_FX_BREATHE: {
            uint32_t period = (uint32_t)(4000.0f / speedMult);
            if (period == 0) period = 4000;
            float s = 0.5f - 0.5f * cosf(((float)(nowMs % period)) * (2.0f * PI / (float)period));
            outLevel = 0.20f + 0.80f * s;
            outR = curR;
            outG = curG;
            outB = curB;
            break;
        }

        case HOME_FX_RAINBOW: {
            uint32_t period = (uint32_t)(4500.0f / speedMult);
            if (period == 0) period = 4500;
            uint8_t pos = (uint8_t)(((nowMs % period) * 256) / period);
            ThemeColor c = wheel(pos);
            outR = c.r;
            outG = c.g;
            outB = c.b;
            outLevel = 0.8f;
            break;
        }

        case HOME_FX_HEARTBEAT: {
            uint32_t period = (uint32_t)(1500.0f / speedMult);
            if (period == 0) period = 1500;
            float p = (float)(nowMs % period) / (float)period;
            float lvl = 0.08f;
            if (p < 0.18f) {
                lvl = 0.08f + 0.82f * sinf((p / 0.18f) * PI);
            } else if (p >= 0.24f && p < 0.42f) {
                lvl = 0.08f + 0.62f * sinf(((p - 0.24f) / 0.18f) * PI);
            }
            outLevel = lvl;
            outR = curR;
            outG = curG;
            outB = curB;
            break;
        }

        case HOME_FX_CANDLE: {
            float t = (float)nowMs * 0.0016f * speedMult;
            float n = sinf(t * 1.618f) * 0.18f + sinf(t * 3.1415f) * 0.14f + sinf(t * 0.707f) * 0.12f;
            float lvl = 0.48f + n;
            if (lvl < 0.15f) lvl = 0.15f;
            if (lvl > 0.95f) lvl = 0.95f;
            outLevel = lvl;
            outR = curR;
            outG = curG;
            outB = curB;
            break;
        }

        case HOME_FX_AURORA: {
            uint32_t period = (uint32_t)(5500.0f / speedMult);
            if (period == 0) period = 5500;
            float blend = 0.5f + 0.5f * sinf(((float)(nowMs % period)) * (2.0f * PI / (float)period));
            uint8_t r2, g2, b2;
            if (cmPtr && cmPtr->colorCount >= 2) {
                r2 = cmPtr->colors[1][0];
                g2 = cmPtr->colors[1][1];
                b2 = cmPtr->colors[1][2];
            } else {
                // A real aurora drifts between neighbouring shades: the user's colour and the same
                // colour turned 45 degrees round the hue wheel (never across the whole rainbow)
                analogous(curR, curG, curB, 45.0f, r2, g2, b2);
            }
            outR = (uint8_t)(curR * (1.0f - blend) + r2 * blend);
            outG = (uint8_t)(curG * (1.0f - blend) + g2 * blend);
            outB = (uint8_t)(curB * (1.0f - blend) + b2 * blend);
            outLevel = 0.75f;
            break;
        }

        case HOME_FX_OCEAN: {
            uint32_t period = (uint32_t)(3600.0f / speedMult);
            if (period == 0) period = 3600;
            float p = (float)(nowMs % period) / (float)period;
            float swell = (p < 0.45f) ? sinf(p / 0.45f * (PI / 2.0f)) : cosf((p - 0.45f) / 0.55f * (PI / 2.0f));
            outLevel = 0.12f + 0.78f * swell;
            outR = curR;
            outG = curG;
            outB = curB;
            break;
        }

        case HOME_FX_SPARKLE: {
            float p1 = 0.5f + 0.5f * sinf((float)nowMs * 0.002f * speedMult);
            float p2 = 0.5f + 0.5f * sinf((float)nowMs * 0.0053f * speedMult);
            outLevel = 0.25f + 0.35f * p1 + 0.30f * p2 * p2;
            outR = curR;
            outG = curG;
            outB = curB;
            break;
        }

        case HOME_FX_COMET: {
            uint32_t period = (uint32_t)(2200.0f / speedMult);
            if (period == 0) period = 2200;
            float p = (float)(nowMs % period) / (float)period;
            float lvl = (p < 0.20f) ? (0.08f + 0.85f * sinf((p / 0.20f) * (PI / 2.0f)))
                                    : (0.08f + 0.85f * expf(-(p - 0.20f) * 4.5f));
            outLevel = lvl;
            outR = curR;
            outG = curG;
            outB = curB;
            break;
        }

        case HOME_FX_CYCLE: {
            if (cmPtr && cmPtr->colorCount >= 2) {
                uint8_t count = cmPtr->colorCount;
                uint32_t slotDur = (uint32_t)(2000.0f / speedMult);
                if (slotDur == 0) slotDur = 2000;
                uint32_t totalDur = slotDur * count;
                uint32_t phase = nowMs % totalDur;
                uint8_t idx1 = phase / slotDur;
                uint8_t idx2 = (idx1 + 1) % count;
                float frac = (float)(phase % slotDur) / (float)slotDur;
                outR = (uint8_t)(cmPtr->colors[idx1][0] * (1.0f - frac) + cmPtr->colors[idx2][0] * frac);
                outG = (uint8_t)(cmPtr->colors[idx1][1] * (1.0f - frac) + cmPtr->colors[idx2][1] * frac);
                outB = (uint8_t)(cmPtr->colors[idx1][2] * (1.0f - frac) + cmPtr->colors[idx2][2] * frac);
                outLevel = 0.8f;
            } else {
                uint32_t period = (uint32_t)(5000.0f / speedMult);
                if (period == 0) period = 5000;
                uint8_t pos = (uint8_t)(((nowMs % period) * 256) / period);
                ThemeColor c = wheel(pos);
                outR = c.r;
                outG = c.g;
                outB = c.b;
                outLevel = 0.8f;
            }
            break;
        }

        default: {
            outR = curR;
            outG = curG;
            outB = curB;
            outLevel = 0.8f;
            break;
        }
    }

    if (outLevel < 0.0f) outLevel = 0.0f;
    if (outLevel > 1.0f) outLevel = 1.0f;
}

uint32_t HomeTheme::currentRgb(uint32_t nowMs) const {
    uint8_t cr, cg, cb;
    float lvl;
    currentFrame(nowMs, cr, cg, cb, lvl);
    return ((uint32_t)cr << 16) | ((uint32_t)cg << 8) | cb;
}

uint16_t HomeTheme::currentRgb565(uint32_t nowMs) const {
    uint8_t cr, cg, cb;
    float lvl;
    currentFrame(nowMs, cr, cg, cb, lvl);
    uint8_t mx = max(cr, max(cg, cb));
    if (mx < 60 && mx > 0) {
        float boost = 60.0f / (float)mx;
        cr = (uint8_t)min(255.0f, (float)cr * boost);
        cg = (uint8_t)min(255.0f, (float)cg * boost);
        cb = (uint8_t)min(255.0f, (float)cb * boost);
    }
    return (uint16_t)(((cr & 0xF8) << 8) | ((cg & 0xFC) << 3) | (cb >> 3));
}

uint8_t HomeTheme::mapBrightness(uint8_t cap) const {
    if (cap == 0) return 0;
    LedBrightness b = activeBrightness();
    switch (b) {
        case BRIGHTNESS_LOW:
            return (cap >= 4) ? (cap / 4) : 1;
        case BRIGHTNESS_HIGH:
            return cap;
        case BRIGHTNESS_MED:
        default:
            return (cap >= 2) ? (cap / 2) : 1;
    }
}

void HomeTheme::setPreview(uint8_t pr, uint8_t pg, uint8_t pb, HomeEffect pfx, uint8_t pspeed, LedBrightness pbright, uint32_t nowMs, HomeWallpaper pwall) {
    prevR = pr;
    prevG = pg;
    prevB = pb;
    prevFx = (pfx < HOME_FX_COUNT) ? pfx : HOME_FX_BREATHE;
    prevSpeed = (pspeed >= 1 && pspeed <= 5) ? pspeed : 3;
    prevBrightness = (pbright <= BRIGHTNESS_HIGH) ? pbright : BRIGHTNESS_MED;
    prevWallpaper = (pwall < WALLPAPER_COUNT) ? pwall : wallpaper;
    previewActive = true;
    previewExpiresAt = nowMs + 30000;
}

void HomeTheme::clearPreview() {
    previewActive = false;
    previewExpiresAt = 0;
    prevWallpaper = wallpaper;
}

void HomeTheme::checkPreviewTimeout(uint32_t nowMs) {
    if (previewActive && nowMs >= previewExpiresAt) {
        clearPreview();
    }
}

bool HomeTheme::validateKeyName(const char* inName, char* outName, size_t maxLen) {
    if (!outName || maxLen == 0) return false;
    if (!inName || !inName[0]) {
        strncpy(outName, "T-KEY", maxLen - 1);
        outName[maxLen - 1] = '\0';
        return true;
    }
    // Trim leading spaces
    while (*inName == ' ') inName++;
    size_t len = strlen(inName);
    // Trim trailing spaces
    while (len > 0 && inName[len - 1] == ' ') len--;
    if (len == 0) {
        strncpy(outName, "T-KEY", maxLen - 1);
        outName[maxLen - 1] = '\0';
        return true;
    }
    if (len > 16 || len >= maxLen) return false;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)inName[i];
        if (c < 0x20 || c > 0x7E) return false; // printable ASCII only
    }
    memcpy(outName, inName, len);
    outName[len] = '\0';
    return true;
}

static void trimPriceZeros(char* s) {
    char* dot = strchr(s, '.');
    if (!dot) return;
    if (strchr(s, 'e') || strchr(s, 'E')) return;
    size_t len = strlen(s);
    size_t minLen = (size_t)(dot - s) + 3; // keep at least 2 decimal places e.g. .00
    while (len > minLen && s[len - 1] == '0') {
        s[--len] = '\0';
    }
}

void HomeTheme::formatPrice(float price, char* out, size_t maxLen) {
    if (!out || maxLen == 0) return;
    if (price <= 0.0f) {
        snprintf(out, maxLen, "$0.00");
        return;
    }
    if (price >= 1000.0f) {
        uint32_t whole = (uint32_t)price;
        uint32_t cents = (uint32_t)((price - (float)whole) * 100.0f + 0.5f);
        if (cents >= 100) { cents = 0; whole++; }
        if (whole >= 1000000) {
            snprintf(out, maxLen, "$%lu,%03lu,%03lu.%02lu",
                     (unsigned long)(whole / 1000000), (unsigned long)((whole / 1000) % 1000), (unsigned long)(whole % 1000), (unsigned long)cents);
        } else {
            snprintf(out, maxLen, "$%lu,%03lu.%02lu",
                     (unsigned long)(whole / 1000), (unsigned long)(whole % 1000), (unsigned long)cents);
        }
    } else if (price >= 1.0f) {
        snprintf(out, maxLen, "$%.2f", price);
    } else if (price >= 0.01f) {
        snprintf(out, maxLen, "$%.4f", price);
        trimPriceZeros(out);
    } else if (price >= 0.0001f) {
        snprintf(out, maxLen, "$%.6f", price);
        trimPriceZeros(out);
    } else if (price >= 0.000001f) {
        snprintf(out, maxLen, "$%.7f", price);
        trimPriceZeros(out);
    } else {
        snprintf(out, maxLen, "%.2e", price);
    }
}

void HomeTheme::formatDate(const struct tm* t, char* out, size_t maxLen) {
    if (!out || maxLen == 0) return;
    if (!t) {
        snprintf(out, maxLen, "--- -- ---");
        return;
    }
    static const char* const W_DAYS[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    static const char* const MONTHS[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int wday = (t->tm_wday >= 0 && t->tm_wday < 7) ? t->tm_wday : 0;
    int mon  = (t->tm_mon >= 0 && t->tm_mon < 12) ? t->tm_mon : 0;
    int mday = (t->tm_mday >= 1 && t->tm_mday <= 31) ? t->tm_mday : 1;
    snprintf(out, maxLen, "%s %d %s", W_DAYS[wday], mday, MONTHS[mon]);
}

int HomeTheme::getTickerCoinIndex(uint32_t nowMs, int enabledCoinsCount) {
    if (enabledCoinsCount <= 0) return 0;
    return (int)((nowMs / 3000UL) % (uint32_t)enabledCoinsCount);
}

void HomeTheme::resetDefaults() {
    r = 0x77; g = 0x64; b = 0xD8;
    fx = HOME_FX_BREATHE;
    speed = 3;
    brightness = BRIGHTNESS_MED;
    activeCustomMode = -1;
    strncpy(keyName, "T-KEY", sizeof(keyName));
    keyName[sizeof(keyName) - 1] = '\0';
    wallpaper = WALLPAPER_NONE;
    customCount = 0;
    for (uint8_t i = 0; i < MAX_CUSTOM_MODES; i++) {
        customModes[i] = CustomMode{};
    }
    clearPreview();
}

bool HomeTheme::isValid() const {
    if ((uint8_t)fx >= HOME_FX_COUNT) return false;
    if (speed < 1 || speed > 5) return false;
    if ((uint8_t)brightness > BRIGHTNESS_HIGH) return false;
    if ((uint8_t)wallpaper >= WALLPAPER_COUNT) return false;
    if (activeCustomMode < -1 || activeCustomMode >= MAX_CUSTOM_MODES) return false;
    if (customCount > MAX_CUSTOM_MODES) return false;
    for (uint8_t i = 0; i < customCount; i++) {
        const CustomMode& cm = customModes[i];
        if (cm.colorCount < 1 || cm.colorCount > 4) return false;
        if ((uint8_t)cm.fx >= HOME_FX_COUNT) return false;
        if (cm.speed < 1 || cm.speed > 5) return false;
        if (cm.name[sizeof(cm.name) - 1] != '\0') return false;
    }
    return true;
}

void HomeTheme::load() {
    Preferences p;
    if (!p.begin(THEME_NAMESPACE, true)) {
        resetDefaults();
        return;
    }

    String kn = p.getString("key_name", "T-KEY");
    validateKeyName(kn.c_str(), keyName, sizeof(keyName));
    uint8_t wp = p.getUChar("wallpaper", (uint8_t)WALLPAPER_NONE);
    wallpaper = (wp < WALLPAPER_COUNT) ? (HomeWallpaper)wp : WALLPAPER_NONE;

    ThemeBlob blob;
    size_t len = p.getBytesLength(THEME_BLOB_KEY);
    if (len == sizeof(blob) && p.getBytes(THEME_BLOB_KEY, &blob, sizeof(blob)) == sizeof(blob)) {
        p.end();
        if (blob.version == THEME_BLOB_VERSION &&
            themeCrc8((const uint8_t*)&blob, sizeof(blob) - 1) == blob.crc) {
            
            HomeTheme candidate;
            strncpy(candidate.keyName, keyName, sizeof(candidate.keyName));
            candidate.wallpaper = wallpaper;
            candidate.r = blob.r;
            candidate.g = blob.g;
            candidate.b = blob.b;
            candidate.fx = (HomeEffect)blob.fx;
            candidate.speed = blob.speed;
            candidate.brightness = (LedBrightness)blob.brightness;
            candidate.activeCustomMode = blob.activeCustomMode;
            candidate.customCount = (blob.customCount <= MAX_CUSTOM_MODES) ? blob.customCount : 0;
            for (uint8_t i = 0; i < candidate.customCount; i++) {
                memcpy(candidate.customModes[i].name, blob.customModes[i].name, sizeof(candidate.customModes[i].name));
                candidate.customModes[i].name[sizeof(candidate.customModes[i].name) - 1] = '\0';
                candidate.customModes[i].colorCount = blob.customModes[i].colorCount;
                memcpy(candidate.customModes[i].colors, blob.customModes[i].colors, sizeof(candidate.customModes[i].colors));
                candidate.customModes[i].fx = (HomeEffect)blob.customModes[i].fx;
                candidate.customModes[i].speed = blob.customModes[i].speed;
            }

            if (candidate.isValid()) {
                *this = candidate;
                clearPreview();
                return;
            }
        }
        resetDefaults();
        return;
    }

    if (p.isKey("home_rgb")) {
        uint32_t v = p.getUInt("home_rgb", 0x7764D8);
        r = (v >> 16) & 0xFF;
        g = (v >> 8) & 0xFF;
        b = v & 0xFF;
        uint8_t f = p.getUChar("home_fx", HOME_FX_BREATHE);
        fx = (f < HOME_FX_COUNT) ? (HomeEffect)f : HOME_FX_BREATHE;
        speed = 3;
        brightness = BRIGHTNESS_MED;
        activeCustomMode = -1;
        customCount = 0;
        p.end();
        clearPreview();
        return;
    }

    p.end();
    resetDefaults();
}

bool HomeTheme::save() const {
    if (!isValid()) return false;

    ThemeBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.version = THEME_BLOB_VERSION;
    blob.r = r;
    blob.g = g;
    blob.b = b;
    blob.fx = (uint8_t)fx;
    blob.speed = speed;
    blob.brightness = (uint8_t)brightness;
    blob.activeCustomMode = activeCustomMode;
    blob.customCount = (customCount <= MAX_CUSTOM_MODES) ? customCount : 0;
    for (uint8_t i = 0; i < blob.customCount; i++) {
        memcpy(blob.customModes[i].name, customModes[i].name, sizeof(blob.customModes[i].name));
        blob.customModes[i].name[sizeof(blob.customModes[i].name) - 1] = '\0';
        blob.customModes[i].colorCount = customModes[i].colorCount;
        memcpy(blob.customModes[i].colors, customModes[i].colors, sizeof(blob.customModes[i].colors));
        blob.customModes[i].fx = (uint8_t)customModes[i].fx;
        blob.customModes[i].speed = customModes[i].speed;
    }
    blob.crc = themeCrc8((const uint8_t*)&blob, sizeof(blob) - 1);

    Preferences p;
    if (!p.begin(THEME_NAMESPACE, false)) return false;
    bool ok = p.putBytes(THEME_BLOB_KEY, &blob, sizeof(blob)) == sizeof(blob);
    p.putString("key_name", keyName[0] ? keyName : "T-KEY");
    p.putUChar("wallpaper", (uint8_t)wallpaper);
    p.end();
    return ok;
}
