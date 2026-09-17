/**
 * ui_engine.cpp — Implementation of 160x80 TFT Display Graphics
 */

#include "ui_engine.h"

void UiEngine::begin(TFT_eSPI* tft) {
    _tft = tft;
    _tft->init();
    _tft->setRotation(DISP_ROTATION);
    _tft->fillScreen(TFT_BLACK);

    // Turn on backlight GPIO 38
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
}

void UiEngine::drawHeader(const char* title, uint16_t headerColor) {
    _tft->fillRect(0, 0, DISP_W, 14, headerColor);
    _tft->setTextColor(TFT_WHITE, headerColor);
    _tft->setTextFont(1);
    _tft->setTextSize(1);
    _tft->drawString(title, 4, 3);
}

void UiEngine::renderPinScreen(const char* currentDigits, int activeIndex, int currentVal) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("AUTHENTICATION // PIN", TFT_MAROON);

    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->drawString("ENTER SECURITY PIN:", 10, 18);

    // Draw 4 PIN digit slots
    int startX = 30;
    int y = 34;
    for (int i = 0; i < PIN_LENGTH; i++) {
        int x = startX + (i * 26);
        if (i < activeIndex) {
            // Entered digit: show bullet/star
            _tft->fillRect(x, y, 20, 22, TFT_DARKGREY);
            _tft->setTextColor(TFT_GREEN, TFT_DARKGREY);
            _tft->drawString("*", x + 7, y + 5, 2);
        } else if (i == activeIndex) {
            // Currently active digit being modified
            _tft->fillRect(x, y, 20, 22, TFT_NAVY);
            _tft->drawRect(x, y, 20, 22, TFT_CYAN);
            _tft->setTextColor(TFT_YELLOW, TFT_NAVY);
            char numBuf[3];
            snprintf(numBuf, sizeof(numBuf), "%d", currentVal);
            _tft->drawString(numBuf, x + 6, y + 4, 2);
        } else {
            // Pending digit
            _tft->drawRect(x, y, 20, 22, TFT_DARKGREY);
            _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            _tft->drawString("_", x + 7, y + 4, 2);
        }
    }

    // Controls hint bar at bottom
    _tft->fillRect(0, 66, DISP_W, 14, TFT_BLACK);
    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("TAP:+1 | HOLD:OK | 2x:DEL", 8, 68, 1);
}

void UiEngine::renderDashboard(uint32_t uptimeSec, bool fidoReady, bool cryptoReady) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("T-KEY S3 // VAULT READY", 0x1144); // Dark cyan/blue

    // Status lines
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString("USB : Composite (HID+CDC)", 4, 18, 1);

    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->drawString("FIDO2  : Active (WebAuthn)", 4, 30, 1);

    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft->drawString("SIGNER : Clear-Sign Active", 4, 42, 1);

    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->drawString("STORAGE: AES-XTS Encrypted", 4, 54, 1);

    // Footer
    _tft->fillRect(0, 68, DISP_W, 12, 0x2104);
    _tft->setTextColor(TFT_WHITE, 0x2104);
    char buf[32];
    snprintf(buf, sizeof(buf), "Uptime: %lus | HOLD to Lock", (unsigned long)uptimeSec);
    _tft->drawString(buf, 4, 70, 1);
}

void UiEngine::renderFidoRequest(const char* originDomain) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("WEBAUTHN PASSKEY AUTH", 0x03E0); // Forest green

    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString("SITE / RP DOMAIN:", 6, 18, 1);

    // Big highlight domain badge
    _tft->fillRect(4, 28, DISP_W - 8, 20, 0x18E3);
    _tft->drawRect(4, 28, DISP_W - 8, 20, TFT_GREEN);
    _tft->setTextColor(TFT_GREEN, 0x18E3);
    _tft->drawString(originDomain, 10, 32, 2);

    // Call to action
    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft->drawString("TAP BUTTON TO CONFIRM", 14, 52, 1);

    _tft->setTextColor(TFT_RED, TFT_BLACK);
    _tft->drawString("Double-click: REJECT", 24, 66, 1);
}

void UiEngine::renderCryptoSignRequest(const char* network, const char* recipient, const char* amount) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("SIGN TRANSACTION (WYSIWYS)", 0xA800); // Amber

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("NET :", 4, 17, 1);
    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->drawString(network, 32, 17, 1);

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("TO  :", 4, 29, 1);
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString(recipient, 32, 29, 1);

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("AMT :", 4, 42, 1);
    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->drawString(amount, 32, 42, 2);

    // Call to action
    _tft->fillRect(0, 64, DISP_W, 16, 0x4208);
    _tft->setTextColor(TFT_YELLOW, 0x4208);
    _tft->drawString("HOLD BUTTON TO SIGN", 16, 68, 1);
}

void UiEngine::renderAirGapScreen(const char* filename, const char* txDetails) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("AIR-GAP SD SIGNER", TFT_BLUE);

    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString("MICROSD DETECTED:", 6, 18, 1);

    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->drawString(filename, 10, 30, 2);

    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft->drawString(txDetails, 10, 50, 1);

    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->drawString("TAP BUTTON TO SIGN", 18, 66, 1);
}

void UiEngine::renderBleScreen(const char* deviceName, bool connected) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("WIRELESS PHONE MODE", TFT_PURPLE);

    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString("BLE ADVERTISING:", 6, 20, 1);

    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->drawString(deviceName, 10, 34, 2);

    _tft->setTextColor(connected ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    _tft->drawString(connected ? "STATUS: PAIRED" : "STATUS: AWAITING PHONE", 10, 56, 1);
}

void UiEngine::renderSuccessBanner(const char* title, const char* subtitle) {
    _tft->fillRect(10, 15, DISP_W - 20, 50, 0x0400); // Dark green box
    _tft->drawRect(10, 15, DISP_W - 20, 50, TFT_GREEN);

    _tft->setTextColor(TFT_WHITE, 0x0400);
    _tft->drawString(title, 20, 24, 2);

    _tft->setTextColor(TFT_GREEN, 0x0400);
    _tft->drawString(subtitle, 20, 44, 1);
}

void UiEngine::renderErrorBanner(const char* message) {
    _tft->fillRect(10, 15, DISP_W - 20, 50, 0x8000); // Dark red box
    _tft->drawRect(10, 15, DISP_W - 20, 50, TFT_RED);

    _tft->setTextColor(TFT_WHITE, 0x8000);
    _tft->drawString("REJECTED / ERROR", 20, 24, 2);

    _tft->setTextColor(TFT_YELLOW, 0x8000);
    _tft->drawString(message, 20, 44, 1);
}
