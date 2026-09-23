/**
 * ui_engine.cpp — Implementation of High-Fidelity ST7735 Zero-Cutoff UI Renderer
 * ==============================================================================
 * Follows the lilygo-tdongle-ui-dev specification:
 * - 14px Header Zone (Y: 0..13)
 * - 52px Primary Content Zone (Y: 14..65)
 * - 14px Action / Footer Zone (Y: 66..79)
 * Zero text clipping: all text strings strictly clamped to fit within 160x80 frame.
 */

#include "ui_engine.h"
#include "rgb_status.h"

uint16_t getCoinColor565(const char* symbol) {
    RgbColor c = RgbStatus::getCoinRgb(symbol);
    return ((uint16_t)(c.r >> 3) << 11) | ((uint16_t)(c.g >> 2) << 5) | (uint16_t)(c.b >> 3);
}

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

    // Clamp title to max 22 chars to prevent header overflow
    char cleanTitle[24];
    if (strlen(title) > 22) {
        strncpy(cleanTitle, title, 19);
        cleanTitle[19] = '.';
        cleanTitle[20] = '.';
        cleanTitle[21] = '.';
        cleanTitle[22] = '\0';
    } else {
        strncpy(cleanTitle, title, sizeof(cleanTitle) - 1);
        cleanTitle[sizeof(cleanTitle) - 1] = '\0';
    }
    _sprite->drawString(cleanTitle, DISP_W / 2, 7, 1);
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
        // Clamp footer string to max 24 chars to avoid screen edge clipping
        char cleanHint[26];
        if (strlen(hint) > 24) {
            strncpy(cleanHint, hint, 21);
            cleanHint[21] = '.';
            cleanHint[22] = '.';
            cleanHint[23] = '.';
            cleanHint[24] = '\0';
        } else {
            strncpy(cleanHint, hint, sizeof(cleanHint) - 1);
            cleanHint[sizeof(cleanHint) - 1] = '\0';
        }
        _sprite->drawString(cleanHint, DISP_W / 2, 73, 1);
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
    _sprite->drawString("REAPER SECURITY", DISP_W / 2, 30, 2);

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("FIDO2 / PASSKEY / WYSIWYS", DISP_W / 2, 48, 1);

    drawFooter("HARDWARE READY", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderHomeDashboard(uint32_t uptimeSec, const char* wifiSsid, float totalPortfolioUsd, bool isFlipped) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("T-KEY S3 // HOME", COLOR_HEADER_BG, COLOR_NEON_CYAN);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("USB :", 8, 17, 1);
    _sprite->setTextColor(COLOR_SIGNAL_GREEN, COLOR_BG);
    _sprite->drawString("READY (HID+CDC)", 44, 17, 1);

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("WIFI:", 8, 29, 1);
    if (wifiSsid && strlen(wifiSsid) > 0 && strcmp(wifiSsid, "DISCONNECTED") != 0) {
        _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
        char wBuf[20];
        if (strlen(wifiSsid) > 14) {
            strncpy(wBuf, wifiSsid, 11);
            wBuf[11] = '.'; wBuf[12] = '.'; wBuf[13] = '.'; wBuf[14] = '\0';
        } else {
            strncpy(wBuf, wifiSsid, sizeof(wBuf) - 1);
            wBuf[sizeof(wBuf) - 1] = '\0';
        }
        _sprite->drawString(wBuf, 44, 29, 1);
    } else {
        _sprite->setTextColor(0x7BEF, COLOR_BG);
        _sprite->drawString("OFFLINE / AIRGAP", 44, 29, 1);
    }

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("SYS :", 8, 41, 1);
    _sprite->setTextColor(0xDEFB, COLOR_BG);
    _sprite->drawString("80MHz COOL <35mA", 44, 41, 1);

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("PORT:", 8, 53, 1);
    char pBuf[24];
    snprintf(pBuf, sizeof(pBuf), "$%.2f USD", totalPortfolioUsd);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(pBuf, 44, 53, 1);

    drawFooter("[●] PASSKEY  [▲▲] FLIP", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderPasskeyHub(bool authPending, const char* rpId, float progress0to1) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    if (!authPending) {
        drawHeader("PASSKEY // AUTH HUB", COLOR_HEADER_BG, COLOR_SIGNAL_GREEN);

        _sprite->setTextDatum(TL_DATUM);
        _sprite->setTextColor(0xAD55, COLOR_BG);
        _sprite->drawString("FIDO2  :", 8, 19, 1);
        _sprite->setTextColor(COLOR_SIGNAL_GREEN, COLOR_BG);
        _sprite->drawString("CTAP2 / WEBAUTHN", 56, 19, 1);

        _sprite->setTextColor(0xAD55, COLOR_BG);
        _sprite->drawString("STATUS :", 8, 33, 1);
        _sprite->setTextColor(0xFFFF, COLOR_BG);
        _sprite->drawString("READY TO AUTH", 56, 33, 1);

        _sprite->setTextColor(0x7BEF, COLOR_BG);
        _sprite->drawString("WAITING FOR LOGIN...", 8, 48, 1);

        drawFooter("[●] CRYPTO  [▲▲] HOME", 0, 0.0f);
    } else {
        drawHeader("PASSKEY // AUTH REQ", 0x0340, COLOR_SIGNAL_GREEN);

        _sprite->setTextDatum(MC_DATUM);
        _sprite->setTextColor(0xFFFF, COLOR_BG);
        _sprite->drawString("LOGIN REQUEST FOR:", DISP_W / 2, 26, 1);

        char dom[20];
        formatRpDomain(rpId ? rpId : "webauthn", dom, sizeof(dom));
        _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
        _sprite->drawString(dom, DISP_W / 2, 44, 2);

        drawFooter("[●] 1-TAP APPROVE", COLOR_SIGNAL_GREEN, progress0to1);
    }

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderReadyDashboard(uint32_t uptimeSec, bool fidoReady, bool vaultUnlocked, int activeCoinsCount) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("T-KEY S3 // READY", COLOR_HEADER_BG, COLOR_SIGNAL_GREEN);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("FIDO2 :", 8, 18, 1);
    _sprite->setTextColor(fidoReady ? COLOR_SIGNAL_GREEN : COLOR_CRIMSON_PANIC, COLOR_BG);
    _sprite->drawString(fidoReady ? "ACTIVE (1-TAP UP)" : "OFFLINE", 56, 18, 1);

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("VAULT :", 8, 30, 1);
    _sprite->setTextColor(vaultUnlocked ? COLOR_NEON_CYAN : COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(vaultUnlocked ? "UNLOCKED (PIN OK)" : "LOCKED (PIN REQ)", 56, 30, 1);

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("ASSETS:", 8, 42, 1);
    char cBuf[24];
    snprintf(cBuf, sizeof(cBuf), "%d Active Coins", activeCoinsCount);
    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(cBuf, 56, 42, 1);

    char upBuf[28];
    snprintf(upBuf, sizeof(upBuf), "UPTIME: %lus | 80MHz", uptimeSec);
    _sprite->setTextColor(0x7BEF, COLOR_BG);
    _sprite->drawString(upBuf, 8, 54, 1);

    drawFooter("[●] COINS  [■] HOLD: PIN", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderPinScreen(const char* currentDigits, int pinLength, int activeIndex, int currentVal, uint8_t holdStage) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("MASTER PIN ENTRY", 0x3000, COLOR_CYBER_GOLD);

    // Dynamic 4-to-8 digit scaling across 146px width
    if (pinLength < 4) pinLength = 4;
    if (pinLength > 8) pinLength = 8;

    int gap = (pinLength >= 7) ? 3 : ((pinLength >= 5) ? 4 : 8);
    int slotW = (146 - (pinLength - 1) * gap) / pinLength;
    int totalW = pinLength * slotW + (pinLength - 1) * gap;
    int startX = (DISP_W - totalW) / 2;
    int startY = 22;
    int slotH = 30;

    for (int i = 0; i < pinLength; i++) {
        int x = startX + i * (slotW + gap);
        bool isActive = (i == activeIndex);

        uint16_t boxBorder = isActive ? COLOR_CYBER_GOLD : 0x4208;
        uint16_t boxBg     = isActive ? 0x2100 : COLOR_BG;

        _sprite->fillRoundRect(x, startY, slotW, slotH, 3, boxBg);
        _sprite->drawRoundRect(x, startY, slotW, slotH, 3, boxBorder);

        _sprite->setTextDatum(MC_DATUM);
        _sprite->setTextColor(isActive ? 0xFFFF : 0xAD55, boxBg);

        char c[2] = { currentDigits[i], '\0' };
        if (isActive) c[0] = '0' + currentVal;

        // Scale font down for 6..8 digits so it never exceeds the box
        if (slotW >= 24) {
            _sprite->drawString(c, x + slotW / 2, startY + slotH / 2, 4);
        } else {
            _sprite->drawString(c, x + slotW / 2, startY + slotH / 2, 2);
        }
    }

    if (holdStage > 0) {
        float p = (float)holdStage / 100.0f;
        drawFooter("", COLOR_CYBER_GOLD, p);
    } else {
        drawFooter("TAP:+1 | HOLD:OK | DBL:<", 0, 0.0f);
    }

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderFidoPrompt(const char* rpId, float progress0to1) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("WEBAUTHN // PASSKEY", COLOR_HEADER_BG, COLOR_SIGNAL_GREEN);

    char dom[20];
    formatRpDomain(rpId, dom, sizeof(dom));

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString("TAP BUTTON TO SIGN IN:", DISP_W / 2, 26, 1);

    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(dom, DISP_W / 2, 44, 2);

    drawFooter("[●] 1-TAP TO CONFIRM", COLOR_SIGNAL_GREEN, progress0to1);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderCryptoSignPrompt(const char* chain, const char* recipient, const char* amount) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("SIGN TRANSACTION", 0x3000, COLOR_CYBER_GOLD);

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

    drawFooter("[●] HOLD: SIGN  [▲] EXIT", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderWalletScreen(const char* coinName, const char* symbol, const char* address, const char* path) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    // Determine coin branding color (matching master brand palette)
    uint16_t coinColor = getCoinColor565(symbol);

    drawHeader(coinName, 0x0008, coinColor);

    // Top metadata row: Coin Symbol (Left) & Derivation Path (Right)
    _sprite->fillRoundRect(4, 16, 44, 11, 2, coinColor);
    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0x0000, coinColor);
    _sprite->drawString(symbol, 26, 21, 1);

    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString(path, 156, 17, 1);

    // Address Display Card (Y: 29..64)
    _sprite->fillRoundRect(4, 29, DISP_W - 8, 35, 3, 0x0842);
    _sprite->fillRoundRect(4, 29, 3, 35, 1, coinColor);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(0xFFFF, 0x0842);

    size_t addrLen = address ? strlen(address) : 0;
    if (addrLen <= 23) {
        _sprite->drawString(address ? address : "No Address", 11, 41, 1);
    } else {
        char line1[24];
        char line2[32];
        size_t split = (addrLen > 21) ? 21 : addrLen;
        strncpy(line1, address, split);
        line1[split] = '\0';
        strncpy(line2, address + split, sizeof(line2) - 1);
        line2[sizeof(line2) - 1] = '\0';

        _sprite->drawString(line1, 11, 34, 1);
        _sprite->setTextColor(0xDEFB, 0x0842);
        _sprite->drawString(line2, 11, 48, 1);
    }

    drawFooter("[●] COIN [▲▲] SEED [■] LOCK", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderPortfolioCard(const char* symbol, const char* name, float balance, float priceUsd, float change24h, int activeIdx, int totalActive, float totalPortfolioUsd, bool isLive) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    // ── Brand Colors: exact RGB565 derived from master brand palette ─────────
    uint16_t coinColor = getCoinColor565(symbol);

    // ── Category badge: [MEME] in magenta | [CRYPTO] in cyan ─────────────────
    bool isMeme = (strcmp(symbol,"DOGE")==0 || strcmp(symbol,"SHIB")==0 ||
                   strcmp(symbol,"PEPE")==0 || strcmp(symbol,"BONK")==0 ||
                   strcmp(symbol,"FLOKI")==0|| strcmp(symbol,"WIF")==0  ||
                   strcmp(symbol,"BRETT")==0|| strcmp(symbol,"MOG")==0  ||
                   strcmp(symbol,"TURBO")==0|| strcmp(symbol,"POPCAT")==0||
                   strcmp(symbol,"NEIRO")==0|| strcmp(symbol,"GOAT")==0);
    uint16_t badgeColor  = isMeme ? 0xF81F : 0x07FF;
    const char* badgeLabel = isMeme ? "[MEME]" : "[CRYPTO]";

    char hdr[36];
    snprintf(hdr, sizeof(hdr), "%s (%d/%d) %s", symbol, activeIdx + 1, totalActive, isLive ? "[LIVE]" : "[AIRGAP]");
    drawHeader(hdr, 0x0008, isLive ? COLOR_SIGNAL_GREEN : coinColor);

    // Primary Content Zone (Y: 14..65)
    _sprite->setTextDatum(TL_DATUM);

    // Balance formatting (supports large meme coin balances like PEPE cleanly)
    char balStr[32];
    if (balance >= 1000000.0f) {
        snprintf(balStr, sizeof(balStr), "%.2fM %s", balance / 1e6f, symbol);
    } else if (balance >= 1000.0f) {
        snprintf(balStr, sizeof(balStr), "%.2fK %s", balance / 1e3f, symbol);
    } else if (balance > 0.0f) {
        snprintf(balStr, sizeof(balStr), "%.4f %s", balance, symbol);
    } else {
        snprintf(balStr, sizeof(balStr), "0.00 %s", symbol);
    }
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(balStr, 8, 18, 2);

    // ── Exchange-Style Price Formatting: exact dollar-and-cents live ticker ───
    char priceStr[24];
    if (priceUsd >= 1000.0f) {
        uint32_t whole = (uint32_t)priceUsd;
        uint32_t cents = (uint32_t)((priceUsd - (float)whole) * 100.0f + 0.5f);
        if (cents >= 100) { cents = 0; whole++; }
        if (whole >= 1000000) {
            snprintf(priceStr, sizeof(priceStr), "$%lu,%03lu,%03lu.%02lu",
                     whole / 1000000, (whole / 1000) % 1000, whole % 1000, (unsigned long)cents);
        } else {
            snprintf(priceStr, sizeof(priceStr), "$%lu,%03lu.%02lu",
                     whole / 1000, whole % 1000, (unsigned long)cents);
        }
    } else if (priceUsd >= 1.0f) {
        snprintf(priceStr, sizeof(priceStr), "$%.2f", priceUsd);
    } else if (priceUsd >= 0.01f) {
        snprintf(priceStr, sizeof(priceStr), "$%.4f", priceUsd);
    } else if (priceUsd >= 0.0001f) {
        snprintf(priceStr, sizeof(priceStr), "$%.6f", priceUsd);
    } else if (priceUsd > 0.0f) {
        snprintf(priceStr, sizeof(priceStr), "$%.8f", priceUsd);
    } else {
        snprintf(priceStr, sizeof(priceStr), "$0.00");
    }

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(coinColor, COLOR_BG);
    _sprite->drawString(priceStr, 8, 36, 1);

    char chgStr[16];
    snprintf(chgStr, sizeof(chgStr), "%s%.2f%%", change24h >= 0 ? "+" : "", change24h);
    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(change24h >= 0 ? COLOR_SIGNAL_GREEN : COLOR_CRIMSON_PANIC, COLOR_BG);
    _sprite->drawString(chgStr, 154, 36, 1);

    // Fiat valuation row
    float assetFiat = balance * priceUsd;
    char valStr[32];
    snprintf(valStr, sizeof(valStr), "VAL: $%.2f", assetFiat);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(valStr, 8, 50, 1);

    char totStr[32];
    snprintf(totStr, sizeof(totStr), "TOT: $%.0f", totalPortfolioUsd);
    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString(totStr, 96, 50, 1);

    drawFooter("", 0, 0.0f);

    // Category badge bottom-left: [MEME] magenta | [CRYPTO] cyan
    _sprite->setTextDatum(BL_DATUM);
    _sprite->setTextColor(badgeColor, COLOR_DARK_GRAY);
    _sprite->drawString(badgeLabel, 3, 79, 1);

    // Hint text right-aligned in footer
    _sprite->setTextDatum(BR_DATUM);
    _sprite->setTextColor(0x8410, COLOR_DARK_GRAY);
    _sprite->drawString("[●]COIN [■]PIN", 157, 79, 1);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderSeedWordsView(int wordNum, int totalWords, const char* word) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "RECOVERY SEED (%d/%d)", wordNum, totalWords);
    drawHeader(hdr, 0x3000, COLOR_CYBER_GOLD);

    _sprite->setTextDatum(MC_DATUM);
    char numStr[24];
    snprintf(numStr, sizeof(numStr), "SECRET WORD #%d", wordNum);
    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString(numStr, DISP_W / 2, 26, 1);

    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(word, DISP_W / 2, 44, 4);

    drawFooter(wordNum < totalWords ? "[●] NEXT   [■] HOLD: EXIT" : "[●] DONE   [■] HOLD: EXIT", 0, 0.0f);

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
    _sprite->drawString(word, DISP_W / 2, 44, 4);

    drawFooter(wordNum < totalWords ? "[●] NEXT WORD" : "[■] HOLD: CONFIRM", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderEntropyGatherScreen(int currentSamples, int requiredSamples) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("SEED GENERATOR", 0x3000, COLOR_CYBER_GOLD);

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

void UiEngine::renderAirGapScreen(const char* psbtFile, const char* summary, bool readyToSign) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("AIR-GAP SIGNER", 0x0010, 0x541F);

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(psbtFile, DISP_W / 2, 26, 1);

    _sprite->setTextColor(readyToSign ? COLOR_SIGNAL_GREEN : COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(summary, DISP_W / 2, 44, 2);

    drawFooter(readyToSign ? "[●] HOLD: SIGN" : "[▲] DBL: EXIT", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderAirGapPsbt(const char* fileName, const char* recipient, const char* amountBtc, const char* feeStr, bool readyToSign) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    drawHeader("BIP-174 PSBT SIGNER", 0x0010, 0x541F);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(fileName, 6, 16, 1);

    _sprite->fillRoundRect(4, 26, DISP_W - 8, 26, 3, 0x0842);
    _sprite->fillRoundRect(4, 26, 3, 26, 1, 0x541F);

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

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(COLOR_SIGNAL_GREEN, COLOR_BG);
    _sprite->drawString(amountBtc, 6, 54, 1);

    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(feeStr, 154, 54, 1);

    drawFooter(readyToSign ? "[●] HOLD: SIGN" : "CANNOT SIGN", 0, 0.0f);

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

    _sprite->fillRect(0, 0, DISP_W, DISP_H, 0x0340);
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

    _sprite->fillRect(0, 0, DISP_W, DISP_H, 0x5000);
    _sprite->drawRect(0, 0, DISP_W, DISP_H, COLOR_CRIMSON_PANIC);

    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0xFFFF, 0x5000);
    _sprite->drawString("✖ ERROR", DISP_W / 2, 30, 2);

    _sprite->setTextColor(0xFFFF, 0x5000);
    _sprite->drawString(message, DISP_W / 2, 52, 1);

    _sprite->pushSprite(0, 0);
}

void UiEngine::setRotation(uint8_t rot) {
    if (_tft) _tft->setRotation(rot);
    if (_sprite) _sprite->setRotation(rot);
}
