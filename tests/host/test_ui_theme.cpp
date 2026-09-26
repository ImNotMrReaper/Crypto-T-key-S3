#include "test.h"
#include "ui_theme.h"
#include "rgb_status.h"
#include <Preferences.h>
#include <string.h>

static void freshNvs() {
    hoststub::nvs.clear();
    homeTheme.resetDefaults();
}

TEST(theme_blob_save_load_round_trip) {
    freshNvs();

    homeTheme.r = 0x12;
    homeTheme.g = 0x34;
    homeTheme.b = 0x56;
    homeTheme.fx = HOME_FX_AURORA;
    homeTheme.speed = 4;
    homeTheme.brightness = BRIGHTNESS_HIGH;
    homeTheme.activeCustomMode = 1;

    homeTheme.customCount = 2;
    strncpy(homeTheme.customModes[0].name, "Cyberpunk", sizeof(homeTheme.customModes[0].name) - 1);
    homeTheme.customModes[0].fx = HOME_FX_CYCLE;
    homeTheme.customModes[0].speed = 2;
    homeTheme.customModes[0].colorCount = 2;
    homeTheme.customModes[0].colors[0][0] = 0xFF; homeTheme.customModes[0].colors[0][1] = 0x00; homeTheme.customModes[0].colors[0][2] = 0x55;
    homeTheme.customModes[0].colors[1][0] = 0x00; homeTheme.customModes[0].colors[1][1] = 0xE5; homeTheme.customModes[0].colors[1][2] = 0xFF;

    strncpy(homeTheme.customModes[1].name, "Neon Gold", sizeof(homeTheme.customModes[1].name) - 1);
    homeTheme.customModes[1].fx = HOME_FX_BREATHE;
    homeTheme.customModes[1].speed = 5;
    homeTheme.customModes[1].colorCount = 3;
    homeTheme.customModes[1].colors[0][0] = 0xFF; homeTheme.customModes[1].colors[0][1] = 0xD7; homeTheme.customModes[1].colors[0][2] = 0x00;
    homeTheme.customModes[1].colors[1][0] = 0xFF; homeTheme.customModes[1].colors[1][1] = 0x88; homeTheme.customModes[1].colors[1][2] = 0x00;
    homeTheme.customModes[1].colors[2][0] = 0x33; homeTheme.customModes[1].colors[2][1] = 0x33; homeTheme.customModes[1].colors[2][2] = 0x33;

    CHECK(homeTheme.save());

    HomeTheme loaded;
    loaded.load();

    CHECK(loaded.r == 0x12);
    CHECK(loaded.g == 0x34);
    CHECK(loaded.b == 0x56);
    CHECK(loaded.fx == HOME_FX_AURORA);
    CHECK(loaded.speed == 4);
    CHECK(loaded.brightness == BRIGHTNESS_HIGH);
    CHECK(loaded.activeCustomMode == 1);
    CHECK(loaded.customCount == 2);

    CHECK(strcmp(loaded.customModes[0].name, "Cyberpunk") == 0);
    CHECK(loaded.customModes[0].fx == HOME_FX_CYCLE);
    CHECK(loaded.customModes[0].speed == 2);
    CHECK(loaded.customModes[0].colorCount == 2);
    CHECK(loaded.customModes[0].colors[0][0] == 0xFF);
    CHECK(loaded.customModes[0].colors[1][1] == 0xE5);

    CHECK(strcmp(loaded.customModes[1].name, "Neon Gold") == 0);
    CHECK(loaded.customModes[1].fx == HOME_FX_BREATHE);
    CHECK(loaded.customModes[1].speed == 5);
    CHECK(loaded.customModes[1].colorCount == 3);
    CHECK(loaded.customModes[1].colors[0][0] == 0xFF);
    CHECK(loaded.customModes[1].colors[1][1] == 0x88);
    CHECK(loaded.customModes[1].colors[2][2] == 0x33);
}

TEST(theme_blob_corruption_handling) {
    freshNvs();
    homeTheme.r = 0xAA; homeTheme.g = 0xBB; homeTheme.b = 0xCC;
    homeTheme.fx = HOME_FX_OCEAN;
    homeTheme.speed = 4;
    homeTheme.brightness = BRIGHTNESS_HIGH;
    CHECK(homeTheme.save());

    Preferences p;
    p.begin("ui_theme", false);
    uint8_t raw[134];
    CHECK(p.getBytes("theme_blob", raw, sizeof(raw)) == sizeof(raw));

    // Test 1: Bad CRC -> fallback to defaults
    raw[sizeof(raw) - 1] ^= 0xFF;
    p.putBytes("theme_blob", raw, sizeof(raw));
    p.end();

    homeTheme.load();
    CHECK(homeTheme.r == 0x77 && homeTheme.g == 0x64 && homeTheme.b == 0xD8);
    CHECK(homeTheme.fx == HOME_FX_BREATHE);
    CHECK(homeTheme.speed == 3);
    CHECK(homeTheme.brightness == BRIGHTNESS_MED);

    // Test 2: Bad version -> fallback to defaults
    p.begin("ui_theme", false);
    raw[sizeof(raw) - 1] ^= 0xFF; // restore CRC
    raw[0] = 99; // bad version
    p.putBytes("theme_blob", raw, sizeof(raw));
    p.end();

    homeTheme.load();
    CHECK(homeTheme.r == 0x77);
    CHECK(homeTheme.fx == HOME_FX_BREATHE);

    // Test 3: Truncated blob
    p.begin("ui_theme", false);
    p.putBytes("theme_blob", raw, sizeof(raw) - 10);
    p.end();

    homeTheme.load();
    CHECK(homeTheme.r == 0x77);

    // Test 4: Trailing garbage
    uint8_t oversized[sizeof(raw) + 5];
    memcpy(oversized, raw, sizeof(raw));
    p.begin("ui_theme", false);
    p.putBytes("theme_blob", oversized, sizeof(oversized));
    p.end();

    homeTheme.load();
    CHECK(homeTheme.r == 0x77);

    // Test 5: Every single 1-bit flip must fail closed or remain valid
    CHECK(homeTheme.save());
    p.begin("ui_theme", true);
    p.getBytes("theme_blob", raw, sizeof(raw));
    p.end();

    for (size_t byteIdx = 0; byteIdx < sizeof(raw); byteIdx++) {
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint8_t mutated[sizeof(raw)];
            memcpy(mutated, raw, sizeof(raw));
            mutated[byteIdx] ^= (1 << bit);

            p.begin("ui_theme", false);
            p.putBytes("theme_blob", mutated, sizeof(mutated));
            p.end();

            HomeTheme t;
            t.load();
            CHECK(t.isValid());
        }
    }
}

TEST(rainbow_sync_test) {
    freshNvs();
    homeTheme.fx = HOME_FX_RAINBOW;
    homeTheme.speed = 3;

    // Across 10 seconds in 25ms steps, UI accent and LED frame hue must be identical
    for (uint32_t t = 0; t <= 10000; t += 25) {
        uint8_t lr, lg, lb;
        float level;
        homeTheme.currentFrame(t, lr, lg, lb, level);
        uint32_t ledRgb = ((uint32_t)lr << 16) | ((uint32_t)lg << 8) | lb;
        uint32_t uiRgb = homeTheme.currentRgb(t);
        CHECK(ledRgb == uiRgb);
    }

    // Color cycle with harmonics must also synchronize
    homeTheme.fx = HOME_FX_CYCLE;
    for (uint32_t t = 0; t <= 8000; t += 40) {
        uint8_t lr, lg, lb;
        float level;
        homeTheme.currentFrame(t, lr, lg, lb, level);
        uint32_t ledRgb = ((uint32_t)lr << 16) | ((uint32_t)lg << 8) | lb;
        uint32_t uiRgb = homeTheme.currentRgb(t);
        CHECK(ledRgb == uiRgb);
    }
}

TEST(every_effect_within_brightness_cap) {
    freshNvs();
    RgbStatus rgb;
    rgb.begin(40, 39);

    CHECK(RgbStatus::BRIGHTNESS_CAP <= 31);
    CHECK(homeTheme.mapBrightness(RgbStatus::BRIGHTNESS_CAP) <= RgbStatus::BRIGHTNESS_CAP);

    // Verify low, med, and high brightness mappings stay at or below cap
    for (uint8_t b = 0; b <= 2; b++) {
        homeTheme.brightness = (LedBrightness)b;
        uint8_t val = homeTheme.mapBrightness(RgbStatus::BRIGHTNESS_CAP);
        CHECK(val > 0 && val <= RgbStatus::BRIGHTNESS_CAP);
    }

    // Test each of the 10 effects over full cycles
    for (uint8_t fx = 0; fx < HOME_FX_COUNT; fx++) {
        homeTheme.fx = (HomeEffect)fx;
        for (uint8_t spd : {1, 3, 5}) {
            homeTheme.speed = spd;
            rgb.setMode(LED_MODE_HOME);

            // Test 400 frames over 8 seconds
            for (uint32_t t = 0; t < 8000; t += 20) {
                hoststub::now_ms = t;
                rgb.update();

                // Physical brightness register must never exceed hardware safety cap
                CHECK(rgb.lastBrightness <= RgbStatus::BRIGHTNESS_CAP);

                uint8_t fr, fg, fb;
                float flvl;
                homeTheme.currentFrame(t, fr, fg, fb, flvl);
                CHECK(flvl >= 0.0f && flvl <= 1.0f);
            }
        }
    }

    // Coin LEDs must also follow user brightness setting and never exceed cap
    homeTheme.brightness = BRIGHTNESS_HIGH;
    rgb.setCoin("BTC");
    hoststub::now_ms = 1000;
    rgb.update();
    CHECK(rgb.lastBrightness == homeTheme.mapBrightness(RgbStatus::BRIGHTNESS_CAP));
    CHECK(rgb.lastBrightness <= RgbStatus::BRIGHTNESS_CAP);

    homeTheme.brightness = BRIGHTNESS_LOW;
    rgb.setCoin("ETH");
    hoststub::now_ms = 2000;
    rgb.update();
    CHECK(rgb.lastBrightness == homeTheme.mapBrightness(RgbStatus::BRIGHTNESS_CAP));
    CHECK(rgb.lastBrightness <= RgbStatus::BRIGHTNESS_CAP);
}

TEST(preview_then_revert) {
    freshNvs();
    homeTheme.r = 0x11; homeTheme.g = 0x22; homeTheme.b = 0x33;
    homeTheme.fx = HOME_FX_SOLID;
    homeTheme.speed = 3;
    homeTheme.brightness = BRIGHTNESS_MED;
    CHECK(homeTheme.save());

    // 1. Set preview in RAM
    homeTheme.setPreview(0xAA, 0xBB, 0xCC, HOME_FX_OCEAN, 5, BRIGHTNESS_HIGH, 1000);
    CHECK(homeTheme.previewActive);
    CHECK(homeTheme.activeR() == 0xAA);
    CHECK(homeTheme.activeG() == 0xBB);
    CHECK(homeTheme.activeB() == 0xCC);
    CHECK(homeTheme.activeFx() == HOME_FX_OCEAN);
    CHECK(homeTheme.activeSpeed() == 5);
    CHECK(homeTheme.activeBrightness() == BRIGHTNESS_HIGH);

    // Verify NVS was NOT touched
    HomeTheme persisted;
    persisted.load();
    CHECK(persisted.r == 0x11 && persisted.g == 0x22 && persisted.b == 0x33);
    CHECK(persisted.fx == HOME_FX_SOLID);
    CHECK(persisted.speed == 3);

    // 2. Clear preview reverts immediately
    homeTheme.clearPreview();
    CHECK(!homeTheme.previewActive);
    CHECK(homeTheme.activeR() == 0x11);
    CHECK(homeTheme.activeFx() == HOME_FX_SOLID);
    CHECK(homeTheme.activeSpeed() == 3);

    // 3. 30-second timeout auto-revert
    homeTheme.setPreview(0xEE, 0xFF, 0x00, HOME_FX_SPARKLE, 4, BRIGHTNESS_LOW, 1000);
    CHECK(homeTheme.previewActive);

    // At 20s, preview should still be active
    homeTheme.checkPreviewTimeout(21000);
    CHECK(homeTheme.previewActive);

    // At 30.1s, preview must automatically expire and revert in RAM
    homeTheme.checkPreviewTimeout(31100);
    CHECK(!homeTheme.previewActive);
    CHECK(homeTheme.activeR() == 0x11);
    CHECK(homeTheme.activeFx() == HOME_FX_SOLID);
}

int main() {
    RUN(theme_blob_save_load_round_trip);
    RUN(theme_blob_corruption_handling);
    RUN(rainbow_sync_test);
    RUN(every_effect_within_brightness_cap);
    RUN(preview_then_revert);
    DONE();
}
