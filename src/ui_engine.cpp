/**
 * ui_engine.cpp — Implementation of High-Fidelity ST7735 UI Renderer
 * ==============================================================================
 * Follows the lilygo-tdongle-ui-dev specification:
 * - 14px Header Zone (Y: 0..13)
 * - 52px Primary Content Zone (Y: 14..65)
 * - 14px Action / Footer Zone (Y: 66..79)
 */

#include "ui_engine.h"

void UiEngine::begin(TFT_eSPI* tft) {
    _tft = tft;
    if (_tft) {
        _sprite = new TFT_eSprite(_tft);
        _sprite->createSprite(DISP_W, DISP_H);
        _sprite->setRotation(DISP_ROTATION);
    }
}

void UiEngine::drawHeader(const char* title, uint16_t headerColor, uint16_t textColor) {
    if (!_sprite) return;
    _sprite->fillRect(0, 0, DISP_W, 14, headerColor);
    _sprite->drawFastHLine(0, 13, DISP_W, textColor);
    _sprite->setTextColor(textColor, headerColor);
    _sprite->setTextDatum(MC_DATUM);
    _sprite->drawString(title, DISP_W / 2, 7, 1);
}

void UiEngine::drawFooter(const char* hint, uint16_t barColor, float progress0to1) {
    if (!_sprite) return;
    _sprite->fillRect(0, 66, DISP_W, 14, COLOR_DARK_GRAY);
    _sprite->drawFastHLine(0, 66, DISP_W, COLOR_NEON_CYAN);

    if (progress0to1 > 0.0f) {
        int w = (int)((DISP_W - 4) * progress0to1);
        if (w > DISP_W - 4) w = DISP_W - 4;
        _sprite->fillRect(2, 68, w, 10, barColor != 0 ? barColor : COLOR_SIGNAL_GREEN);
    } else {
        _sprite->setTextColor(0xAD55, COLOR_DARK_GRAY);
        _sprite->setTextDatum(MC_DATUM);
        _sprite->drawString(hint, DISP_W / 2, 73, 1);
    }
}

void UiEngine::truncateAddress(const char* addr, char* outBuf, size_t maxLen) {
    size_t len = strlen(addr);
    if (len <= 14) {
        strncpy(outBuf, addr, maxLen - 1);
        outBuf[maxLen - 1] = '\0';
        return;
    }
    snprintf(outBuf, maxLen, "%.6s...%.4s", addr, addr + (len - 4));
}

void UiEngine::formatRpDomain(const char* rpId, char* outBuf, size_t maxLen) {
    if (strlen(rpId) <= 16) {
        strncpy(outBuf, rpId, maxLen - 1);
        outBuf[maxLen - 1] = '\0';
    } else {
        snprintf(outBuf, maxLen, "%.13s...", rpId);
    }
}

void UiEngine::renderBootSplash() {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("CRYPTO TKEY S3", COLOR_HEADER_BG, COLOR_NEON_CYAN);

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString("REAPER SECURITY", DISP_W / 2, 32, 2);

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("FIDO2 / PASSKEY / WYSIWYS", DISP_W / 2, 50, 1);

    drawFooter("INITIALIZING HARDWARE...", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderReadyDashboard(uint32_t uptimeSec, bool fidoReady, bool vaultUnlocked) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("SECURITY KEY READY", COLOR_HEADER_BG, COLOR_SIGNAL_GREEN);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString("CTAPHID :", 8, 20, 1);
    _sprite->setTextColor(fidoReady ? COLOR_SIGNAL_GREEN : COLOR_CRIMSON_PANIC, COLOR_BG);
    _sprite->drawString(fidoReady ? "ACTIVE (1-TAP UP)" : "OFFLINE", 72, 20, 1);

    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString("VAULT   :", 8, 34, 1);
    _sprite->setTextColor(vaultUnlocked ? COLOR_NEON_CYAN : COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(vaultUnlocked ? "UNLOCKED (PIN OK)" : "LOCKED (PIN REQ)", 72, 34, 1);

    char upBuf[24];
    snprintf(upBuf, sizeof(upBuf), "UPTIME: %lus | 80MHz", uptimeSec);
    _sprite->setTextColor(0x7BEF, COLOR_BG);
    _sprite->drawString(upBuf, 8, 48, 1);

    drawFooter("[●] HOLD: ENTER PIN VAULT", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderPinScreen(const char* currentDigits, int activeIndex, int currentVal, uint8_t holdStage) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("MASTER PIN ENTRY", 0x3000, COLOR_CYBER_GOLD);

    // Render 4 rounded digit box slots
    const int slotW = 24, slotH = 30;
    const int startX = 20, startY = 22;
    const int gap = 8;

    for (int i = 0; i < PIN_LENGTH; i++) {
        int x = startX + i * (slotW + gap);
        bool isActive = (i == activeIndex);

        uint16_t boxBorder = isActive ? COLOR_CYBER_GOLD : 0x4208;
        uint16_t boxBg     = isActive ? 0x2100 : COLOR_BG;

        _sprite->fillRoundRect(x, startY, slotW, slotH, 4, boxBg);
        _sprite->drawRoundRect(x, startY, slotW, slotH, 4, boxBorder);

        _sprite->setTextDatum(MC_DATUM);
        _sprite->setTextColor(isActive ? 0xFFFF : 0xAD55, boxBg);

        char c[2] = { currentDigits[i], '\0' };
        if (isActive) c[0] = '0' + currentVal;
        _sprite->drawString(c, x + slotW / 2, startY + slotH / 2, 4);
    }

    if (holdStage > 0) {
        float p = (float)holdStage / 100.0f;
        drawFooter("", COLOR_CYBER_GOLD, p);
    } else {
        drawFooter("TAP:+1 | HOLD:OK | DBL:DEL", 0, 0.0f);
    }

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderFidoPrompt(const char* rpId, float progress0to1) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("WEBAUTHN // USER PRESENCE", COLOR_HEADER_BG, COLOR_SIGNAL_GREEN);

    char dom[20];
    formatRpDomain(rpId, dom, sizeof(dom));

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString("TAP BUTTON TO SIGN IN:", DISP_W / 2, 26, 1);

    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(dom, DISP_W / 2, 44, 2);

    drawFooter("", COLOR_SIGNAL_GREEN, progress0to1);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderCryptoSignPrompt(const char* chain, const char* recipient, const char* amount) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("WYSIWYS // SIGN TRANSACTION", 0x3000, COLOR_CYBER_GOLD);

    char addrTrunc[18];
    truncateAddress(recipient, addrTrunc, sizeof(addrTrunc));

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(chain, 8, 18, 1);

    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(amount, 60, 18, 2);

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("TO:", 8, 36, 1);
    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(addrTrunc, 32, 36, 1);

    drawFooter("[●] HOLD: APPROVE  [▲] DBL: REJECT", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderWalletScreen(const char* coinName, const char* symbol, const char* address, const char* path) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader(coinName, COLOR_HEADER_BG, COLOR_NEON_CYAN);

    char addrTrunc[18];
    truncateAddress(address, addrTrunc, sizeof(addrTrunc));

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(symbol, DISP_W / 2, 26, 2);

    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(addrTrunc, DISP_W / 2, 42, 1);

    _sprite->setTextColor(0x7BEF, COLOR_BG);
    _sprite->drawString(path, DISP_W / 2, 54, 1);

    drawFooter("[●] TAP: NEXT  [■] HOLD: LOCK", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderAirGapScreen(const char* psbtFile, const char* summary, bool readyToSign) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("AIR-GAP MICROSD SIGNER", 0x0010, 0x541F);

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(psbtFile, DISP_W / 2, 26, 1);

    _sprite->setTextColor(readyToSign ? COLOR_SIGNAL_GREEN : COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(summary, DISP_W / 2, 44, 2);

    drawFooter(readyToSign ? "[●] HOLD: SIGN PSBT" : "NO PSBT DETECTED", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderOobeWizard(uint8_t step, const char* title, const char* detail, const char* hint) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    char hdr[24];
    snprintf(hdr, sizeof(hdr), "SETUP WIZARD (%d/3)", step);
    drawHeader(hdr, COLOR_HEADER_BG, COLOR_NEON_CYAN);

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(title, DISP_W / 2, 28, 2);

    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(detail, DISP_W / 2, 46, 1);

    drawFooter(hint, 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderSuccessBanner(const char* title, const char* subtitle) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    _sprite->fillRect(0, 0, DISP_W, DISP_H, 0x0340); // Dark Forest Green
    _sprite->drawRect(0, 0, DISP_W, DISP_H, COLOR_SIGNAL_GREEN);

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0xFFFF, 0x0340);
    _sprite->drawString("✔", DISP_W / 2, 22, 2);

    _sprite->setTextColor(COLOR_SIGNAL_GREEN, 0x0340);
    _sprite->drawString(title, DISP_W / 2, 42, 2);

    _sprite->setTextColor(0xFFFF, 0x0340);
    _sprite->drawString(subtitle, DISP_W / 2, 60, 1);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderErrorBanner(const char* message) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    _sprite->fillRect(0, 0, DISP_W, DISP_H, 0x5000); // Dark Crimson
    _sprite->drawRect(0, 0, DISP_W, DISP_H, COLOR_CRIMSON_PANIC);

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0xFFFF, 0x5000);
    _sprite->drawString("✖ ERROR", DISP_W / 2, 30, 2);

    _sprite->setTextColor(0xFFFF, 0x5000);
    _sprite->drawString(message, DISP_W / 2, 52, 1);

    _sprite->pushSprite(0, 0);
}
