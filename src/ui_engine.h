/**
 * ui_engine.h — High-Fidelity UI Engine for LilyGo T-Dongle S3 (160x80 ST7735)
 * ==============================================================================
 * Double-buffered zero-flicker rendering, 3-zone spatial grid layout,
 * and high-contrast Cyberpunk / OLED pitch-black palette.
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

class UiEngine {
public:
    void begin(TFT_eSPI* tft);

    // Core View Renders (Zero-Flicker Double Buffered)
    void renderBootSplash();
    void renderReadyDashboard(uint32_t uptimeSec, bool fidoReady, bool vaultUnlocked);
    void renderPinScreen(const char* currentDigits, int activeIndex, int currentVal, uint8_t holdStage = 0);
    void renderFidoPrompt(const char* rpId, float progress0to1 = 1.0f);
    void renderCryptoSignPrompt(const char* chain, const char* recipient, const char* amount);
    void renderWalletScreen(const char* coinName, const char* symbol, const char* address, const char* path);
    void renderPortfolioCard(const char* symbol, const char* name, float balance, float priceUsd, float change24h, int activeIdx, int totalActive, float totalPortfolioUsd);
    void renderSeedBackupScreen(int wordNum, int totalWords, const char* word);
    void renderEntropyGatherScreen(int currentSamples, int requiredSamples);
    void renderAirGapScreen(const char* psbtFile, const char* summary, bool readyToSign);
    void renderAirGapPsbt(const char* fileName, const char* recipient, const char* amountBtc, const char* feeStr, bool readyToSign);
    void renderOobeWizard(uint8_t step, const char* title, const char* detail, const char* hint);
    void renderSuccessBanner(const char* title, const char* subtitle);
    void renderErrorBanner(const char* message);

private:
    TFT_eSPI*   _tft = nullptr;
    TFT_eSprite* _sprite = nullptr;

    void drawHeader(const char* title, uint16_t headerColor = COLOR_HEADER_BG, uint16_t textColor = COLOR_NEON_CYAN);
    void drawFooter(const char* hint, uint16_t barColor = 0, float progress0to1 = 0.0f);
    static void truncateAddress(const char* addr, char* outBuf, size_t maxLen);
    static void formatRpDomain(const char* rpId, char* outBuf, size_t maxLen);
};
