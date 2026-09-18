/**
 * ui_engine.h — ST7735 0.96" IPS Display UI Renderer (160x80)
 * ==============================================================
 * Renders PIN Entry, Security Dashboard, WebAuthn User Presence,
 * Clear-Signing Crypto Verification, and Air-Gap SD views.
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

class UiEngine {
public:
    void begin(TFT_eSPI* tft);

    // View Renders
    void renderBootSplash();
    void renderPinScreen(const char* currentDigits, int activeIndex, int currentVal, uint8_t holdStage = 0);
    void renderDashboard(uint32_t uptimeSec, bool fidoReady, bool cryptoReady, bool wifiConnected = false, const char* ipStr = nullptr);
    void renderFidoRequest(const char* originDomain);
    void renderCryptoSignRequest(const char* network, const char* recipient, const char* amount);
    void renderWalletScreen(const char* coinName, const char* symbol, const char* path, const char* address);
    void renderCryptoPrices(float btc, float eth, float sol, float doge, bool isLive);
    void renderSetupStep(uint8_t step, const char* title, const char* line1, const char* line2, const char* line3, const char* hint);
    void renderWifiScreen(bool connected, const char* ssid, const char* ip, int8_t rssi, int savedCount);
    void renderAirGapScreen(const char* filename, const char* txDetails);
    void renderBleScreen(const char* deviceName, bool connected);
    void renderSuccessBanner(const char* title, const char* subtitle);
    void renderErrorBanner(const char* message);

private:
    TFT_eSPI* _tft = nullptr;
    void drawHeader(const char* title, uint16_t headerColor = TFT_NAVY);
};
