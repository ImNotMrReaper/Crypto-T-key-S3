/**
 * ui_engine.h — High-Fidelity Zero-Cutoff UI Engine for LilyGo T-Dongle S3
 * ==============================================================================
 * Double-buffered zero-flicker rendering, 3-zone spatial grid layout,
 * responsive 4-to-8 digit PIN box scaling, and zero-text-truncation typography.
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "ui_theme.h"   // HomeTheme and HomeWallpaper are used in this header
#include "config.h"

class TFT_eSprite;

class UiEngine {
public:
    TFT_eSprite* sprite() { return _sprite; }   // test instrumentation (screen capture)
    void begin(TFT_eSPI* tft);

    // Core View Renders (Zero-Flicker Double Buffered)
    void renderBootSplash();
    void renderHomeDashboard(uint32_t uptimeSec, const char* wifiSsid, float totalPortfolioUsd, bool isFlipped = false);
    void renderPasskeyHub(bool authPending = false, const char* rpId = nullptr, float progress0to1 = 1.0f);
    void renderReadyDashboard(uint32_t uptimeSec, bool fidoReady, bool vaultUnlocked, int activeCoinsCount = 4);
    void renderPinScreen(const char* currentDigits, int pinLength, int activeIndex, int currentVal, uint8_t holdStage = 0);
    void renderFidoPrompt(const char* rpId, float progress0to1 = 1.0f);
    void renderCryptoSignPrompt(const char* chain, const char* recipient, const char* amount);
    void renderWalletScreen(const char* coinName, const char* symbol, const char* address, const char* path);
    // Receive screen: QR code of `qrText` on the left, network + wrapped address on the right.
    void renderReceiveScreen(const char* symbol, const char* network, const char* address,
                             const char* qrText, const char* hint);
    // Setup portal: Wi-Fi join QR on the left, network name + password on the right.
    void renderPortalScreen(const char* ssid, const char* pass, const char* qrText, const char* hint);
    void renderPortfolioCard(const char* symbol, const char* name, float balance, float priceUsd, float change24h, int activeIdx, int totalActive, float totalPortfolioUsd, bool isLive = false);
    void renderSeedBackupScreen(int wordNum, int totalWords, const char* word);
    void renderSeedWordsView(int wordNum, int totalWords, const char* word);
    void renderEntropyGatherScreen(int currentSamples, int requiredSamples);
    void renderAirGapScreen(const char* psbtFile, const char* summary, bool readyToSign);
    void renderAirGapPsbt(const char* fileName, const char* recipient, const char* amountBtc, const char* feeStr, bool readyToSign);
    // PSBT approval: one page per external output (the FULL address, up to 3 lines) ...
    void renderPsbtOutput(int page, int pages, const char* address, const char* amount);
    // ... then a summary page; signing is offered only there
    void renderPsbtSummary(const char* sending, const char* change, const char* fee, int foreignInputs);
    void renderOobeWizard(uint8_t step, const char* title, const char* detail, const char* hint);
    void renderSuccessBanner(const char* title, const char* subtitle);
    void renderErrorBanner(const char* message);
    void setRotation(uint8_t rot);

    // Helpers for Home Screen rendering and testing
    static void formatPrice(float price, char* out, size_t maxLen) {
        HomeTheme::formatPrice(price, out, maxLen);
    }
    static void formatDate(const struct tm* t, char* out, size_t maxLen) {
        HomeTheme::formatDate(t, out, maxLen);
    }
    static int getTickerCoinIndex(uint32_t nowMs, int enabledCoinsCount) {
        return HomeTheme::getTickerCoinIndex(nowMs, enabledCoinsCount);
    }
    void drawWallpaper(HomeWallpaper wp, uint16_t accent);

private:
    TFT_eSPI*   _tft = nullptr;
    TFT_eSprite* _sprite = nullptr;

    void drawHeader(const char* title, uint16_t headerColor = COLOR_HEADER_BG, uint16_t textColor = COLOR_NEON_CYAN);
    void drawFooter(const char* hint, uint16_t barColor = 0, float progress0to1 = 0.0f);
    bool checkSprite();
    void renderAllocError();
    static void truncateAddress(const char* addr, char* outBuf, size_t maxLen);
    static void formatRpDomain(const char* rpId, char* outBuf, size_t maxLen);
};
