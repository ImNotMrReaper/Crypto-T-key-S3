#include "test.h"
#include "rgb_status.h"
#include "ui_theme.h"

TEST(brightness_cap_and_mapping) {
    RgbStatus rgb;
    rgb.begin(40, 39);

    // The cap is config.h's LED_BRIGHTNESS_LIMIT (a 5-bit APA102 value), never above 31
    CHECK(RgbStatus::APA102_MAX_BRIGHTNESS == 31);
    CHECK(RgbStatus::BRIGHTNESS_CAP == (LED_BRIGHTNESS_LIMIT < 31 ? LED_BRIGHTNESS_LIMIT : 31));
    CHECK(RgbStatus::BRIGHTNESS_CAP < 31);   // a real cap: full brightness heats the enclosed dongle

    // Every requested value maps to min(requested, cap) and a valid APA102 header
    for (int b = 0; b <= 255; b++) {
        rgb.setPixel(10, 20, 30, (uint8_t)b);
        uint8_t bright = rgb.lastBrightness;
        uint8_t want = b < RgbStatus::BRIGHTNESS_CAP ? (uint8_t)b : RgbStatus::BRIGHTNESS_CAP;
        CHECK(bright == want);
        uint8_t header = 0xE0 | (bright & 0x1F);
        CHECK((header & 0xE0) == 0xE0 && (header & 0x1F) == bright);
    }
    // The strobe (7) and the UI's usual 4..6 stay unchanged under the cap
    for (uint8_t b : {4, 5, 6, 7}) {
        rgb.setPixel(1, 2, 3, b);
        CHECK(rgb.lastBrightness == b);
    }
}

TEST(led_modes_and_rgb) {
    RgbStatus rgb;
    rgb.begin(40, 39);

    rgb.setMode(LED_MODE_OFF);
    CHECK((int)rgb.currentMode() == (int)LED_MODE_OFF);
    CHECK(rgb.lastR == 0);
    CHECK(rgb.lastG == 0);
    CHECK(rgb.lastB == 0);
    CHECK(rgb.lastBrightness == 0);

    hoststub::now_ms = 100;
    rgb.setMode(LED_MODE_SOLID_AMBER);
    CHECK(rgb.lastR == 255);
    CHECK(rgb.lastG == 130);
    CHECK(rgb.lastB == 0);
    CHECK(rgb.lastBrightness == 4);

    rgb.flashTap(50, 60, 70, 100);
    CHECK(rgb.lastR == 50);
    CHECK(rgb.lastG == 60);
    CHECK(rgb.lastB == 70);
    CHECK(rgb.lastBrightness == 6);
}

TEST(home_effects_and_user_brightness) {
    RgbStatus rgb;
    rgb.begin(40, 39);

    // Security screen mode (AMBER) keeps fixed brightness
    hoststub::now_ms = 100;
    rgb.setMode(LED_MODE_SOLID_AMBER);
    CHECK(rgb.lastBrightness == 4);

    // Home mode responds to user brightness low / med / high
    homeTheme.resetDefaults();
    homeTheme.brightness = BRIGHTNESS_LOW;
    rgb.setMode(LED_MODE_HOME);
    hoststub::now_ms = 200;
    rgb.update();
    uint8_t lowBright = rgb.lastBrightness;
    CHECK(lowBright <= RgbStatus::BRIGHTNESS_CAP);

    homeTheme.brightness = BRIGHTNESS_HIGH;
    hoststub::now_ms = 300;
    rgb.update();
    uint8_t highBright = rgb.lastBrightness;
    CHECK(highBright == RgbStatus::BRIGHTNESS_CAP);
    CHECK(highBright >= lowBright);

    // Coin mode uses user brightness setting
    rgb.setCoin("BTC");
    hoststub::now_ms = 400;
    rgb.update();
    CHECK(rgb.lastBrightness == highBright);
}

int main() {
    RUN(brightness_cap_and_mapping);
    RUN(led_modes_and_rgb);
    RUN(home_effects_and_user_brightness);
    DONE();
}
