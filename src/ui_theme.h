#pragma once
#include <Arduino.h>
#include <time.h>

/**
 * Home screen / home LED theme, chosen in the setup page (NVS "ui_theme").
 * Only the home state is customisable; every other mode keeps its fixed colours
 * so security prompts always look the same.
 */
enum HomeEffect : uint8_t {
    HOME_FX_SOLID = 0,
    HOME_FX_BREATHE = 1,
    HOME_FX_RAINBOW = 2,
    HOME_FX_HEARTBEAT = 3,
    HOME_FX_CANDLE = 4,
    HOME_FX_AURORA = 5,
    HOME_FX_OCEAN = 6,
    HOME_FX_SPARKLE = 7,
    HOME_FX_COMET = 8,
    HOME_FX_CYCLE = 9
};
constexpr uint8_t HOME_FX_COUNT = 10;

enum HomeWallpaper : uint8_t {
    WALLPAPER_NONE = 0,
    WALLPAPER_GRID = 1,
    WALLPAPER_AURORA = 2,
    WALLPAPER_STARFIELD = 3
};
constexpr uint8_t WALLPAPER_COUNT = 4;

enum LedBrightness : uint8_t {
    BRIGHTNESS_LOW = 0,
    BRIGHTNESS_MED = 1,
    BRIGHTNESS_HIGH = 2
};

struct ThemeColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct CustomMode {
    char name[16];
    uint8_t colorCount;     // 1..4
    uint8_t colors[4][3];   // RGB
    uint8_t fx;             // HomeEffect
    uint8_t speed;          // 1..5
};
constexpr uint8_t MAX_CUSTOM_MODES = 4;

struct HomeTheme {
    uint8_t r = 0x77, g = 0x64, b = 0xD8;    // default: Ubuntu purple #7764D8
    HomeEffect fx = HOME_FX_BREATHE;
    uint8_t speed = 3;                       // 1..5 (default 3 = 1.0x)
    LedBrightness brightness = BRIGHTNESS_MED; // user brightness: low / med / high
    int8_t activeCustomMode = -1;            // -1 = direct preset/custom, 0..3 = custom mode index

    char keyName[17] = "T-KEY";
    HomeWallpaper wallpaper = WALLPAPER_NONE;

    uint8_t customCount = 0;
    CustomMode customModes[MAX_CUSTOM_MODES];

    // Live preview in RAM only (not written to NVS)
    bool previewActive = false;
    uint32_t previewExpiresAt = 0;
    uint8_t prevR = 0x77, prevG = 0x64, prevB = 0xD8;
    HomeEffect prevFx = HOME_FX_BREATHE;
    uint8_t prevSpeed = 3;
    LedBrightness prevBrightness = BRIGHTNESS_MED;
    HomeWallpaper prevWallpaper = WALLPAPER_NONE;

    // Active color and personalization accessors (reflects preview if active)
    uint8_t activeR() const { return previewActive ? prevR : r; }
    uint8_t activeG() const { return previewActive ? prevG : g; }
    uint8_t activeB() const { return previewActive ? prevB : b; }
    HomeEffect activeFx() const { return previewActive ? prevFx : fx; }
    uint8_t activeSpeed() const { return previewActive ? prevSpeed : speed; }
    LedBrightness activeBrightness() const { return previewActive ? prevBrightness : brightness; }
    HomeWallpaper activeWallpaper() const { return previewActive ? prevWallpaper : wallpaper; }
    const char* activeKeyName() const { return keyName[0] ? keyName : "T-KEY"; }

    uint32_t rgb() const { return (uint32_t)activeR() << 16 | (uint32_t)activeG() << 8 | activeB(); }
    uint32_t savedRgb() const { return (uint32_t)r << 16 | (uint32_t)g << 8 | b; }
    uint16_t rgb565() const {
        uint8_t cr = activeR(), cg = activeG(), cb = activeB();
        return (uint16_t)((cr & 0xF8) << 8 | (cg & 0xFC) << 3 | cb >> 3);
    }

    // Color wheel for rainbow and harmonic calculations
    static ThemeColor wheel(uint8_t wheelPos);

    // Frame generator for LED rendering and screen accent sync
    void currentFrame(uint32_t nowMs, uint8_t& outR, uint8_t& outG, uint8_t& outB, float& outLevel) const;
    uint32_t currentRgb(uint32_t nowMs) const;
    uint16_t currentRgb565(uint32_t nowMs) const;

    // Brightness mapper for APA102 DotStar (maps low/med/high at or below cap)
    uint8_t mapBrightness(uint8_t cap) const;

    // Name validation helper
    static bool validateKeyName(const char* inName, char* outName, size_t maxLen);

    // Home screen formatting helpers
    static void formatPrice(float price, char* out, size_t maxLen);
    static void formatDate(const struct tm* t, char* out, size_t maxLen);
    static int getTickerCoinIndex(uint32_t nowMs, int enabledCoinsCount);

    // Live preview methods (RAM only)
    void setPreview(uint8_t pr, uint8_t pg, uint8_t pb, HomeEffect pfx, uint8_t pspeed, LedBrightness pbright, uint32_t nowMs, HomeWallpaper pwall = WALLPAPER_NONE);
    void clearPreview();
    void checkPreviewTimeout(uint32_t nowMs);

    // NVS load / save with versioned blob & CRC8 validation
    void load();
    bool save() const;
    void resetDefaults();
    bool isValid() const;
};

extern HomeTheme homeTheme;
