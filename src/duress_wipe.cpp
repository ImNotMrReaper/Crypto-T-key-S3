/**
 * duress_wipe.cpp — Implementation of Duress Flash Scrub
 */

#include "duress_wipe.h"
#include <esp_partition.h>
#include <nvs_flash.h>

void DuressWipe::zeroizeMemoryAndNVS() {
    // 1. Wipe Preferences / NVS Keystores
    Preferences prefs;
    const char* namespaces[] = {"vault_sec", "fido_vault", "wifi_cfg", "sec_key", "fido2_keys", "wallet_seed", "totp_cfg", "device_cfg", nullptr};
    for (int i = 0; namespaces[i] != nullptr; i++) {
        if (prefs.begin(namespaces[i], false)) {
            prefs.clear();
            prefs.end();
        }
    }

    // 2. Erase the entire NVS partition flash sectors
    nvs_flash_erase();

    // 3. Silence debug output to preserve genuine crash decoy
    Serial.flush();
}

void DuressWipe::renderDecoyPanicScreen(TFT_eSPI& tft) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(1);
    tft.setTextSize(1);

    tft.setCursor(2, 2);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("Guru Meditation Error:");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("Core 0 panic'ed (LoadProhibited)");
    tft.println("Exception was unhandled.");
    tft.println("PC : 0x40081a24  PS : 0x00060020");
    tft.println("A0 : 0x80084f00  A1 : 0x3ffb1be0");
    tft.println("Flash read: CRC mismatch 0xFF");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.println("SYSTEM HALTED. REBOOT REQ.");
}

void DuressWipe::execute(TFT_eSPI& tft, RgbStatus& rgb, const char* reason) {
    // Rapid red strobe
    rgb.setMode(LED_MODE_STROBE_RED);
    for (int i = 0; i < 8; i++) {
        rgb.update();
        delay(60);
    }
    rgb.setMode(LED_MODE_OFF);
    rgb.update();

    // Zeroize everything
    zeroizeMemoryAndNVS();

    // Show decoy crash screen
    renderDecoyPanicScreen(tft);

    // Halt device permanently until hard power-cycle
    while (true) {
        delay(1000);
    }
}
