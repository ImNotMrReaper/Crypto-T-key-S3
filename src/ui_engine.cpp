/**
 * ui_engine.cpp — Implementation of 160x80 TFT Display Graphics
 */

#include "ui_engine.h"

void UiEngine::begin(TFT_eSPI* tft) {
    _tft = tft;
    _tft->init();
    _tft->setRotation(DISP_ROTATION);
    _tft->fillScreen(TFT_BLACK);

    // Turn on backlight GPIO 38 (Active LOW)
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, TFT_BL_ON);
}

void UiEngine::drawHeader(const char* title, uint16_t headerColor) {
    _tft->fillRect(0, 0, DISP_W, 14, headerColor);
    _tft->setTextColor(TFT_WHITE, headerColor);
    _tft->setTextFont(1);
    _tft->setTextSize(1);
    _tft->drawString(title, 4, 3);
}

void UiEngine::renderBootSplash() {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("ANTIGRAVITY // T-KEY S3", TFT_NAVY);

    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString("HARDWARE SECURITY KEY", 6, 20, 1);

    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->drawString("SYSTEM : ESP32-S3 @ 240MHz", 6, 34, 1);
    _tft->drawString("CRYPTO : AES-XTS / WebAuthn", 6, 46, 1);

    _tft->fillRect(0, 64, DISP_W, 16, 0x1144);
    _tft->setTextColor(TFT_YELLOW, 0x1144);
    _tft->drawString("INITIALIZING VAULT...", 16, 68, 1);
}

void UiEngine::renderPinScreen(const char* currentDigits, int activeIndex, int currentVal, uint8_t holdStage) {
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

    // Dynamic Controls & Hold Stage Feedback Bar
    if (holdStage == 1) {
        _tft->fillRect(0, 64, DISP_W, 16, 0x03E0); // Forest green
        _tft->setTextColor(TFT_WHITE, 0x03E0);
        _tft->drawString("[ RELEASE: CONFIRM ]", 14, 68, 1);
    } else if (holdStage == 2) {
        _tft->fillRect(0, 64, DISP_W, 16, TFT_MAGENTA);
        _tft->setTextColor(TFT_WHITE, TFT_MAGENTA);
        _tft->drawString("[ RELEASE: RESET ALL ]", 10, 68, 1);
    } else if (holdStage == 3) {
        _tft->fillRect(0, 64, DISP_W, 16, TFT_RED);
        _tft->setTextColor(TFT_WHITE, TFT_RED);
        _tft->drawString("! DURESS WIPE IMMINENT !", 8, 68, 1);
    } else {
        _tft->fillRect(0, 66, DISP_W, 14, TFT_BLACK);
        _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        _tft->drawString("TAP:+1 | 2x:DEL | HOLD:OK", 6, 68, 1);
    }
}

void UiEngine::renderDashboard(uint32_t uptimeSec, bool fidoReady, bool cryptoReady, bool wifiConnected, const char* ipStr) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("T-KEY S3 // VAULT READY", 0x1144); // Dark cyan/blue

    // Status lines
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString("USB    : HID + CDC Active", 4, 17, 1);

    _tft->setTextColor(TFT_GREEN, TFT_BLACK);
    _tft->drawString("FIDO2  : WebAuthn Passkey", 4, 28, 1);

    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft->drawString("WALLET : BTC/ETH/SOL Ready", 4, 39, 1);

    _tft->setTextColor(wifiConnected ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    char wifiBuf[32];
    if (wifiConnected && ipStr) {
        snprintf(wifiBuf, sizeof(wifiBuf), "WIFI   : %s", ipStr);
    } else {
        snprintf(wifiBuf, sizeof(wifiBuf), "WIFI   : Auto-Scan Ready");
    }
    _tft->drawString(wifiBuf, 4, 50, 1);

    // Footer
    _tft->fillRect(0, 66, DISP_W, 14, 0x2104);
    _tft->setTextColor(TFT_WHITE, 0x2104);
    _tft->drawString("TAP:Menu | HOLD:Lock", 18, 69, 1);
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

void UiEngine::renderWalletScreen(const char* coinName, const char* symbol, const char* path, const char* address) {
    _tft->fillScreen(TFT_BLACK);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "VAULT // %s", symbol ? symbol : "CRYPTO");
    drawHeader(hdr, 0x9000); // Dark orange/gold

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("COIN:", 4, 17, 1);
    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->drawString(coinName, 36, 17, 1);

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("PATH:", 4, 29, 1);
    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft->drawString(path, 36, 29, 1);

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("ADDR:", 4, 42, 1);

    // Address card
    _tft->fillRect(4, 52, DISP_W - 8, 14, 0x18C3);
    _tft->setTextColor(TFT_GREEN, 0x18C3);
    _tft->drawString(address, 8, 55, 1);

    // Footer
    _tft->fillRect(0, 68, DISP_W, 12, 0x3186);
    _tft->setTextColor(TFT_WHITE, 0x3186);
    _tft->drawString("TAP:Next Coin | 2x:Back", 14, 70, 1);
}

void UiEngine::renderWifiScreen(bool connected, const char* ssid, const char* ip, int8_t rssi, int savedCount) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader("WI-FI NETWORK ENGINE", 0x028A); // Blue-green

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("STATE :", 4, 18, 1);
    if (connected) {
        _tft->setTextColor(TFT_GREEN, TFT_BLACK);
        _tft->drawString("CONNECTED", 48, 18, 1);
    } else {
        _tft->setTextColor(TFT_RED, TFT_BLACK);
        _tft->drawString("SEARCHING / IDLE", 48, 18, 1);
    }

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("SSID  :", 4, 30, 1);
    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->drawString(ssid ? ssid : "None", 48, 30, 1);

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("IP    :", 4, 42, 1);
    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft->drawString(ip ? ip : "0.0.0.0", 48, 42, 1);

    _tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    _tft->drawString("SAVED :", 4, 54, 1);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d network profile(s)", savedCount);
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    _tft->drawString(buf, 48, 54, 1);

    // Footer
    _tft->fillRect(0, 68, DISP_W, 12, 0x1104);
    _tft->setTextColor(TFT_WHITE, 0x1104);
    _tft->drawString("TAP:Scan/Connect | 2x:Back", 10, 70, 1);
}

void UiEngine::renderCryptoPrices(float btc, float eth, float sol, float doge, bool isLive) {
    _tft->fillScreen(TFT_BLACK);
    drawHeader(isLive ? "LIVE CRYPTO PRICES (USD)" : "PRICES (OFFLINE CACHE)", isLive ? 0x03E0 : 0x4208);

    char btcBuf[24], ethBuf[24], solBuf[24], dogeBuf[24];
    snprintf(btcBuf, sizeof(btcBuf), "BTC : $%0.0f", btc);
    snprintf(ethBuf, sizeof(ethBuf), "ETH : $%0.0f", eth);
    snprintf(solBuf, sizeof(solBuf), "SOL : $%0.2f", sol);
    snprintf(dogeBuf, sizeof(dogeBuf), "DOGE: $%0.3f", doge);

    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    _tft->drawString(btcBuf, 6, 17, 1);

    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    _tft->drawString(ethBuf, 84, 17, 1);

    _tft->setTextColor(TFT_PURPLE, TFT_BLACK);
    _tft->drawString(solBuf, 6, 33, 1);

    _tft->setTextColor(TFT_GOLD, TFT_BLACK);
    _tft->drawString(dogeBuf, 84, 33, 1);

    _tft->fillRect(4, 48, DISP_W - 8, 16, 0x10C2);
    _tft->setTextColor(isLive ? TFT_GREEN : TFT_ORANGE, 0x10C2);
    _tft->drawString(isLive ? "FEED: CoinGecko API" : "FEED: Awaiting Wi-Fi Sync", 8, 52, 1);

    // Footer
    _tft->fillRect(0, 68, DISP_W, 12, 0x2125);
    _tft->setTextColor(TFT_WHITE, 0x2125);
    _tft->drawString("TAP:Refresh | 2x:Back", 16, 70, 1);
}

void UiEngine::renderSetupStep(uint8_t step, const char* title, const char* line1, const char* line2, const char* line3, const char* hint) {
    _tft->fillScreen(TFT_BLACK);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "SETUP [%d/4] // %s", step, title);
    drawHeader(hdr, 0x5800); // Amber-red

    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
    if (line1) _tft->drawString(line1, 6, 18, 1);

    _tft->setTextColor(TFT_CYAN, TFT_BLACK);
    if (line2) _tft->drawString(line2, 6, 32, 1);

    _tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    if (line3) _tft->drawString(line3, 6, 46, 1);

    // Footer hint
    _tft->fillRect(0, 66, DISP_W, 14, 0x18C3);
    _tft->setTextColor(TFT_GREEN, 0x18C3);
    _tft->drawString(hint ? hint : "TAP: Continue", 8, 68, 1);
}
