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

    // Determine coin-specific branding color
    uint16_t coinColor = COLOR_NEON_CYAN;
    if (strstr(symbol, "BTC")) coinColor = COLOR_CYBER_GOLD;       // 0xFD80 Gold
    else if (strstr(symbol, "ETH")) coinColor = 0x9B1F;           // Royal Violet
    else if (strstr(symbol, "SOL")) coinColor = 0x17EE;           // Neon Turquoise
    else if (strstr(symbol, "DOGE")) coinColor = 0xFE00;          // Doge Sunny Yellow

    // Vibrant header with colored bar
    drawHeader(coinName, 0x0008, coinColor);

    // Top metadata row: Coin Badge Pill (Left) & Derivation Path (Right)
    _sprite->fillRoundRect(4, 16, 42, 11, 2, coinColor);
    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0x0000, coinColor);
    _sprite->drawString(symbol, 25, 21, 1);

    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString(path, 156, 17, 1);

    // Address Display Card (Y: 29..64)
    // Dark background card with coin-colored left stripe
    _sprite->fillRoundRect(4, 29, DISP_W - 8, 35, 3, 0x0842);
    _sprite->fillRoundRect(4, 29, 3, 35, 1, coinColor);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(0xFFFF, 0x0842);

    size_t addrLen = address ? strlen(address) : 0;
    if (addrLen <= 23) {
        _sprite->drawString(address ? address : "No Address", 11, 41, 1);
    } else {
        // Split cleanly across 2 lines for 100% full address readability
        char line1[26];
        char line2[32];
        size_t split = (addrLen > 22) ? 22 : addrLen;
        strncpy(line1, address, split);
        line1[split] = '\0';
        strncpy(line2, address + split, sizeof(line2) - 1);
        line2[sizeof(line2) - 1] = '\0';

        _sprite->drawString(line1, 11, 34, 1);
        _sprite->setTextColor(0xDEFB, 0x0842);
        _sprite->drawString(line2, 11, 48, 1);
    }

    drawFooter("[●] TAP: NEXT COIN  [■] HOLD: LOCK", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderPortfolioCard(const char* symbol, const char* name, float balance, float priceUsd, float change24h, int activeIdx, int totalActive, float totalPortfolioUsd) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    uint16_t coinColor = COLOR_NEON_CYAN;
    if (strstr(symbol, "BTC")) coinColor = COLOR_CYBER_GOLD;
    else if (strstr(symbol, "ETH")) coinColor = 0x9B1F;
    else if (strstr(symbol, "SOL")) coinColor = 0x17EE;
    else if (strstr(symbol, "DOGE")) coinColor = 0xFE00;

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "%s (%d/%d) // TRACKER", symbol, activeIdx + 1, totalActive);
    drawHeader(hdr, 0x0008, coinColor);

    // Primary Content Zone (Y: 14..65)
    _sprite->setTextDatum(TL_DATUM);

    // Balance
    char balStr[32];
    if (balance < 1000.0f) {
        snprintf(balStr, sizeof(balStr), "%.4f %s", balance, symbol);
    } else {
        snprintf(balStr, sizeof(balStr), "%.1f %s", balance, symbol);
    }
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(balStr, 8, 18, 2);

    // USD Price & 24h change
    char priceStr[32];
    snprintf(priceStr, sizeof(priceStr), "$%.2f", priceUsd);
    _sprite->setTextColor(coinColor, COLOR_BG);
    _sprite->drawString(priceStr, 8, 36, 1);

    char chgStr[16];
    snprintf(chgStr, sizeof(chgStr), "%s%.1f%%", change24h >= 0 ? "+" : "", change24h);
    _sprite->setTextColor(change24h >= 0 ? COLOR_SIGNAL_GREEN : COLOR_CRIMSON_PANIC, COLOR_BG);
    _sprite->drawString(chgStr, 75, 36, 1);

    // Fiat value of this asset vs total portfolio
    float assetFiat = balance * priceUsd;
    char valStr[32];
    snprintf(valStr, sizeof(valStr), "VAL: $%.2f", assetFiat);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(valStr, 8, 50, 1);

    char totStr[32];
    snprintf(totStr, sizeof(totStr), "TOT: $%.0f", totalPortfolioUsd);
    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString(totStr, 95, 50, 1);

    drawFooter("[●] NEXT  [▲] DBL: PREV  [■] HOLD: EXIT", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderEntropyGatherScreen(int currentSamples, int requiredSamples) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("SEED GENERATOR // ENTROPY", 0x3000, COLOR_CYBER_GOLD);

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString("TAP BUTTON RANDOMLY", DISP_W / 2, 28, 1);

    char countStr[24];
    snprintf(countStr, sizeof(countStr), "COLLECTED: %d / %d", currentSamples, requiredSamples);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(countStr, DISP_W / 2, 44, 2);

    float p = (float)currentSamples / (float)requiredSamples;
    if (p > 1.0f) p = 1.0f;
    drawFooter("", COLOR_CYBER_GOLD, p);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderSeedBackupScreen(int wordNum, int totalWords, const char* word) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "BIP-39 SEED (%d/%d)", wordNum, totalWords);
    drawHeader(hdr, 0x0010, COLOR_SIGNAL_GREEN);

    _sprite->setTextDatum(MC_DATUM);
    char numStr[16];
    snprintf(numStr, sizeof(numStr), "WORD #%d", wordNum);
    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString(numStr, DISP_W / 2, 26, 1);

    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(word, DISP_W / 2, 44, 4); // Big font for mnemonic word

    drawFooter(wordNum < totalWords ? "[●] TAP: NEXT WORD" : "[■] HOLD: CONFIRM SEED", 0, 0.0f);

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

    drawFooter(readyToSign ? "[●] HOLD: SIGN PSBT" : "[▲] DBL: EXIT", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderAirGapPsbt(const char* fileName, const char* recipient, const char* amountBtc, const char* feeStr, bool readyToSign) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("BIP-174 PSBT SIGNER", 0x0010, 0x541F);

    // File name tag
    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(fileName, 6, 16, 1);

    // Recipient Card (Y: 26..51)
    _sprite->fillRoundRect(4, 26, DISP_W - 8, 26, 3, 0x0842);
    _sprite->fillRoundRect(4, 26, 3, 26, 1, 0x541F); // Sapphire blue accent

    _sprite->setTextColor(0xFFFF, 0x0842);
    size_t addrLen = recipient ? strlen(recipient) : 0;
    if (addrLen <= 22) {
        _sprite->drawString(recipient ? recipient : "No Recipient", 10, 34, 1);
    } else {
        char line1[24], line2[28];
        size_t split = (addrLen > 21) ? 21 : addrLen;
        strncpy(line1, recipient, split);
        line1[split] = '\0';
        strncpy(line2, recipient + split, sizeof(line2) - 1);
        line2[sizeof(line2) - 1] = '\0';
        _sprite->drawString(line1, 10, 29, 1);
        _sprite->setTextColor(0xDEFB, 0x0842);
        _sprite->drawString(line2, 10, 39, 1);
    }

    // Amount & Fee Row (Y: 53..65)
    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(COLOR_SIGNAL_GREEN, COLOR_BG);
    _sprite->drawString(amountBtc, 6, 54, 1);

    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(feeStr, 154, 54, 1);

    drawFooter(readyToSign ? "[●] HOLD: SIGN & EXPORT" : "CANNOT SIGN", 0, 0.0f);

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
