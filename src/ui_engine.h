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
    void renderPinScreen(const char* currentDigits, int activeIndex, int currentVal);
    void renderDashboard(uint32_t uptimeSec, bool fidoReady, bool cryptoReady);
    void renderFidoRequest(const char* originDomain);
    void renderCryptoSignRequest(const char* network, const char* recipient, const char* amount);
    void renderAirGapScreen(const char* filename, const char* txDetails);
    void renderBleScreen(const char* deviceName, bool connected);
    void renderSuccessBanner(const char* title, const char* subtitle);
    void renderErrorBanner(const char* message);

private:
    TFT_eSPI* _tft = nullptr;
    void drawHeader(const char* title, uint16_t headerColor = TFT_NAVY);
};
