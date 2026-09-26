#include "test.h"
#include "ui_theme.h"
#include "ui_engine.h"
#include "rgb_status.h"
#include <Preferences.h>
#include <string.h>
#include <time.h>

static void freshNvs() {
    hoststub::nvs.clear();
    homeTheme.resetDefaults();
}

TEST(price_formatting_full_range_1e9_to_1e6) {
    char buf[32];

    // Sub-cent and micro-cent (e.g. PEPE, BONK, SHIB) down to 1e-9
    // Must never render "$0.00"
    HomeTheme::formatPrice(1e-9f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.00") != 0);
    CHECK(strstr(buf, "e-") != nullptr || strstr(buf, "0.000000001") != nullptr);

    HomeTheme::formatPrice(5.4e-8f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.00") != 0);
    CHECK(strstr(buf, "e-") != nullptr || strstr(buf, "0.000000054") != nullptr);

    HomeTheme::formatPrice(8.2e-6f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.00") != 0);
    CHECK(strcmp(buf, "$0.0000082") == 0);

    HomeTheme::formatPrice(0.000021f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.00") != 0);
    CHECK(strcmp(buf, "$0.000021") == 0);

    HomeTheme::formatPrice(0.0045f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.00") != 0);
    CHECK(strcmp(buf, "$0.0045") == 0);

    HomeTheme::formatPrice(0.45f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.45") == 0);

    HomeTheme::formatPrice(1.25f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$1.25") == 0);

    HomeTheme::formatPrice(148.50f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$148.50") == 0);

    HomeTheme::formatPrice(64120.0f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$64,120.00") == 0);

    HomeTheme::formatPrice(1000000.0f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$1,000,000.00") == 0);

    // Zero or negative
    HomeTheme::formatPrice(0.0f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.00") == 0);

    HomeTheme::formatPrice(-12.5f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.00") == 0);

    // Verify UiEngine static forwarder behaves identically
    UiEngine::formatPrice(8.2e-6f, buf, sizeof(buf));
    CHECK(strcmp(buf, "$0.0000082") == 0);
}

TEST(date_formatting_local_time) {
    char buf[32];
    struct tm t;
    memset(&t, 0, sizeof(t));

    // Friday, September 25, 2026
    t.tm_wday = 5;  // Friday (0=Sun..6=Sat)
    t.tm_mday = 25; // 25th
    t.tm_mon = 8;   // September (0=Jan..11=Dec)
    t.tm_year = 126;
    HomeTheme::formatDate(&t, buf, sizeof(buf));
    CHECK(strcmp(buf, "FRI 25 SEP") == 0);

    // Sunday, January 1
    t.tm_wday = 0;
    t.tm_mday = 1;
    t.tm_mon = 0;
    HomeTheme::formatDate(&t, buf, sizeof(buf));
    CHECK(strcmp(buf, "SUN 1 JAN") == 0);

    // Wednesday, December 31
    t.tm_wday = 3;
    t.tm_mday = 31;
    t.tm_mon = 11;
    HomeTheme::formatDate(&t, buf, sizeof(buf));
    CHECK(strcmp(buf, "WED 31 DEC") == 0);

    // Null pointer fallback
    HomeTheme::formatDate(nullptr, buf, sizeof(buf));
    CHECK(strcmp(buf, "--- -- ---") == 0);

    // Verify UiEngine static forwarder
    memset(&t, 0, sizeof(t));
    t.tm_wday = 5; t.tm_mday = 25; t.tm_mon = 8;
    UiEngine::formatDate(&t, buf, sizeof(buf));
    CHECK(strcmp(buf, "FRI 25 SEP") == 0);
}

TEST(key_name_validation_rules) {
    char out[32];

    // Empty or null string defaults to "T-KEY"
    CHECK(HomeTheme::validateKeyName("", out, sizeof(out)) == true);
    CHECK(strcmp(out, "T-KEY") == 0);

    CHECK(HomeTheme::validateKeyName(nullptr, out, sizeof(out)) == true);
    CHECK(strcmp(out, "T-KEY") == 0);

    CHECK(HomeTheme::validateKeyName("   ", out, sizeof(out)) == true);
    CHECK(strcmp(out, "T-KEY") == 0);

    // Standard valid custom name
    CHECK(HomeTheme::validateKeyName("Reaper's T-Key", out, sizeof(out)) == true);
    CHECK(strcmp(out, "Reaper's T-Key") == 0);

    // Trims leading and trailing spaces
    CHECK(HomeTheme::validateKeyName("  Vault-Dongle  ", out, sizeof(out)) == true);
    CHECK(strcmp(out, "Vault-Dongle") == 0);

    // Exactly 16 characters is allowed
    CHECK(HomeTheme::validateKeyName("1234567890123456", out, sizeof(out)) == true);
    CHECK(strcmp(out, "1234567890123456") == 0);

    // Greater than 16 characters is rejected
    CHECK(HomeTheme::validateKeyName("12345678901234567", out, sizeof(out)) == false);

    // Non-printable characters rejected
    CHECK(HomeTheme::validateKeyName("Key\nName", out, sizeof(out)) == false);
    CHECK(HomeTheme::validateKeyName("Key\tName", out, sizeof(out)) == false);
    CHECK(HomeTheme::validateKeyName("Key\x1bName", out, sizeof(out)) == false);
}

TEST(ticker_rotation_timing) {
    int count = 5;

    // 0 to 2999 ms -> coin 0
    CHECK(HomeTheme::getTickerCoinIndex(0, count) == 0);
    CHECK(HomeTheme::getTickerCoinIndex(1500, count) == 0);
    CHECK(HomeTheme::getTickerCoinIndex(2999, count) == 0);

    // 3000 to 5999 ms -> coin 1
    CHECK(HomeTheme::getTickerCoinIndex(3000, count) == 1);
    CHECK(HomeTheme::getTickerCoinIndex(4500, count) == 1);
    CHECK(HomeTheme::getTickerCoinIndex(5999, count) == 1);

    // 6000 to 8999 ms -> coin 2
    CHECK(HomeTheme::getTickerCoinIndex(6000, count) == 2);
    CHECK(HomeTheme::getTickerCoinIndex(8999, count) == 2);

    // 12000 ms -> coin 4
    CHECK(HomeTheme::getTickerCoinIndex(12000, count) == 4);

    // 15000 ms -> wraparound to coin 0
    CHECK(HomeTheme::getTickerCoinIndex(15000, count) == 0);

    // Zero or negative coin count safety
    CHECK(HomeTheme::getTickerCoinIndex(3000, 0) == 0);
    CHECK(HomeTheme::getTickerCoinIndex(3000, -1) == 0);

    // Verify UiEngine static forwarder
    CHECK(UiEngine::getTickerCoinIndex(3000, count) == 1);
}

TEST(theme_wallpaper_and_name_persistence) {
    freshNvs();

    // Verify initial defaults
    CHECK(strcmp(homeTheme.activeKeyName(), "T-KEY") == 0);
    CHECK(homeTheme.activeWallpaper() == WALLPAPER_NONE);

    // Set custom name and wallpaper
    strncpy(homeTheme.keyName, "Reaper's T-Key", sizeof(homeTheme.keyName) - 1);
    homeTheme.wallpaper = WALLPAPER_AURORA;
    CHECK(homeTheme.save() == true);

    // Load into fresh instance
    HomeTheme loaded;
    loaded.load();
    CHECK(strcmp(loaded.activeKeyName(), "Reaper's T-Key") == 0);
    CHECK(loaded.activeWallpaper() == WALLPAPER_AURORA);

    // Test live preview overrides active values
    loaded.setPreview(0x00, 0xE5, 0xFF, HOME_FX_CYCLE, 4, BRIGHTNESS_HIGH, 1000, WALLPAPER_STARFIELD);
    CHECK(loaded.activeWallpaper() == WALLPAPER_STARFIELD);

    // Clearing preview restores saved wallpaper
    loaded.clearPreview();
    CHECK(loaded.activeWallpaper() == WALLPAPER_AURORA);

    // Resetting defaults clears name and wallpaper
    loaded.resetDefaults();
    CHECK(strcmp(loaded.activeKeyName(), "T-KEY") == 0);
    CHECK(loaded.activeWallpaper() == WALLPAPER_NONE);
}

int main() {
    RUN(price_formatting_full_range_1e9_to_1e6);
    RUN(date_formatting_local_time);
    RUN(key_name_validation_rules);
    RUN(ticker_rotation_timing);
    RUN(theme_wallpaper_and_name_persistence);
    DONE();
}
