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
#include <qrcode.h>   // ESP-IDF espressif__qrcode component
#include "rgb_status.h"
#include "ui_theme.h"
#include "crypto_coins.h"
#include "USB.h"

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3);
}

// Brand colour, lifted toward white when too dark to read on black (e.g. navy logos)
static void readable(uint8_t& r, uint8_t& g, uint8_t& b) {
    float l = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
    if (l >= 0.28f) return;
    float k = (0.28f - l) / (1.0f - l);
    r += (uint8_t)((255 - r) * k); g += (uint8_t)((255 - g) * k); b += (uint8_t)((255 - b) * k);
}

uint16_t getCoinColor565(const char* symbol) {
    RgbColor c = RgbStatus::getCoinRgb(symbol);
    readable(c.r, c.g, c.b);
    return rgb565(c.r, c.g, c.b);
}

// Home accent: the theme colour, or a slowly turning hue for the rainbow effect
static uint16_t homeAccent565() {
    if (homeTheme.fx == HOME_FX_RAINBOW) {
        uint8_t h = (uint8_t)(millis() / 60);
        uint8_t r, g, b, x = h % 85 * 3;
        if (h < 85) { r = 255 - x; g = x; b = 0; }
        else if (h < 170) { r = 0; g = 255 - x; b = x; }
        else { r = x; g = 0; b = 255 - x; }
        return rgb565(r, g, b);
    }
    uint8_t r = homeTheme.r, g = homeTheme.g, b = homeTheme.b;
    readable(r, g, b);
    return rgb565(r, g, b);
}

static void formatUsd(float v, char* out, size_t n, bool cents) {
    uint32_t whole = (uint32_t)v;
    uint32_t c = (uint32_t)((v - whole) * 100.0f + 0.5f);
    if (c >= 100) { c = 0; whole++; }
    char w[20];
    if (whole >= 1000000) snprintf(w, sizeof(w), "%lu,%03lu,%03lu", whole / 1000000, (whole / 1000) % 1000, whole % 1000);
    else if (whole >= 1000) snprintf(w, sizeof(w), "%lu,%03lu", whole / 1000, whole % 1000);
    else snprintf(w, sizeof(w), "%lu", (unsigned long)whole);
    if (cents) snprintf(out, n, "$%s.%02lu", w, (unsigned long)c);
    else snprintf(out, n, "$%s", w);
}

static inline uint16_t dimColor565(uint16_t c, float factor) {
    uint8_t r = ((c >> 11) & 0x1F);
    uint8_t g = ((c >> 5) & 0x3F);
    uint8_t b = (c & 0x1F);
    r = (uint8_t)(r * factor);
    g = (uint8_t)(g * factor);
    b = (uint8_t)(b * factor);
    return (r << 11) | (g << 5) | b;
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
    uint16_t accent = homeAccent565();
    uint16_t tint = dimColor565(accent, 0.2f);

    // Header: name + live link / temperature status
    _sprite->fillRect(0, 0, DISP_W, 13, tint);
    _sprite->drawFastHLine(0, 13, DISP_W, accent);
    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(accent, tint);
    _sprite->drawString("T-KEY", 4, 3, 1);
    char st[24];
    bool online = wifiSsid && wifiSsid[0] && strcmp(wifiSsid, "AIRGAP") != 0 && strcmp(wifiSsid, "DISCONNECTED") != 0;
    const char* link = (bool)USB ? "USB" : online ? "WI-FI" : "WALL";
    snprintf(st, sizeof(st), "%s %dC", link, (int)(temperatureRead() + 0.5f));
    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(0xC618, tint);
    _sprite->drawString(st, DISP_W - 4, 3, 1);

    // Portfolio value, or READY when nothing is held yet
    int coins = 0, priced = 0;
    float weighted = 0;
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        const CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
        if (!c->enabled) continue;
        coins++;
        if (c->balance > 0 && CryptoCoinRegistry::hasLivePrice(c)) {
            priced++;
            if (totalPortfolioUsd > 0) weighted += c->change24h * (c->balance * c->priceUsd) / totalPortfolioUsd;
        }
    }
    char coinsTxt[12], upTxt[16];
    snprintf(coinsTxt, sizeof(coinsTxt), "%d COIN%s", coins, coins == 1 ? "" : "S");
    uint32_t m = uptimeSec / 60;
    if (m >= 60) snprintf(upTxt, sizeof(upTxt), "UP %luH%02luM", m / 60, m % 60);
    else snprintf(upTxt, sizeof(upTxt), "UP %luM", (unsigned long)m);

    // Left: the headline number (or READY) and what it means; right: coins / uptime
    _sprite->setTextDatum(TL_DATUM);
    const char* rightTxt = upTxt;
    if (totalPortfolioUsd > 0 && priced) {
        char v[24];
        formatUsd(totalPortfolioUsd, v, sizeof(v), totalPortfolioUsd < 100000);
        _sprite->setTextColor(0xFFFF, COLOR_BG);
        _sprite->drawString(v, 4, 19, _sprite->textWidth(v, 4) <= DISP_W - 8 ? 4 : 2);
        char ch[16];
        snprintf(ch, sizeof(ch), "%s%.2f%% 24H", weighted >= 0 ? "+" : "", weighted);
        _sprite->setTextColor(weighted >= 0 ? COLOR_SIGNAL_GREEN : COLOR_CRIMSON_PANIC, COLOR_BG);
        _sprite->drawString(ch, 4, 46, 1);
        rightTxt = coinsTxt;
    } else {
        _sprite->setTextColor(0xFFFF, COLOR_BG);
        _sprite->drawString("READY", 4, 19, 4);
        _sprite->setTextColor(0x8410, COLOR_BG);
        _sprite->drawString(coinsTxt, 4, 46, 1);
    }
    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString(rightTxt, DISP_W - 4, 46, 1);

    // One dot per selected coin, in its brand colour
    int x = 5;
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount() && x < DISP_W - 4; i++) {
        const CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
        if (!c->enabled) continue;
        _sprite->fillCircle(x, 59, 2, getCoinColor565(c->symbol));
        x += 7;
    }

    drawFooter("TAP:KEY 2X:FLIP HOLD:OFF", 0, 0.0f);
    _sprite->drawFastHLine(0, 66, 40, accent);
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

        drawFooter("TAP:COINS 2X:HOME", 0, 0.0f);
    } else {
        drawHeader("PASSKEY // AUTH REQ", 0x0340, COLOR_SIGNAL_GREEN);

        _sprite->setTextDatum(MC_DATUM);
        _sprite->setTextColor(0xFFFF, COLOR_BG);
        _sprite->drawString("LOGIN REQUEST FOR:", DISP_W / 2, 26, 1);

        char dom[20];
        formatRpDomain(rpId ? rpId : "webauthn", dom, sizeof(dom));
        _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
        _sprite->drawString(dom, DISP_W / 2, 44, 2);

        drawFooter("PRESS TO APPROVE", COLOR_SIGNAL_GREEN, progress0to1);
    }

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderReadyDashboard(uint32_t uptimeSec, bool fidoReady, bool vaultUnlocked, int activeCoinsCount) {
    renderHomeDashboard(uptimeSec, "AIRGAP", CryptoCoinRegistry::getTotalPortfolioValueUsd(), false);
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

    drawFooter("PRESS TO APPROVE", COLOR_SIGNAL_GREEN, progress0to1);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderCryptoSignPrompt(const char* chain, const char* recipient, const char* amount) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    uint16_t chainColor = getCoinColor565(chain);
    uint16_t darkTint   = dimColor565(chainColor, 0.22f);

    // Left glowing brand pillar
    _sprite->fillRect(0, 0, 2, DISP_H, chainColor);

    // Header Zone with themed accent
    _sprite->fillRect(2, 0, DISP_W - 2, 13, darkTint);
    _sprite->drawFastHLine(0, 13, DISP_W, chainColor);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(chainColor, darkTint);
    _sprite->drawString("SIGN TRANSACTION", 6, 2, 1);

    char addrTrunc[18];
    truncateAddress(recipient, addrTrunc, sizeof(addrTrunc));

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(chainColor, COLOR_BG);
    _sprite->drawString(chain, 8, 18, 1);

    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(amount, 60, 18, 2);

    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString("TO:", 8, 36, 1);
    _sprite->setTextColor(COLOR_NEON_CYAN, COLOR_BG);
    _sprite->drawString(addrTrunc, 32, 36, 1);

    _sprite->drawFastHLine(0, 66, DISP_W, darkTint);
    _sprite->drawFastHLine(0, 66, 36, chainColor);
    drawFooter("PREVIEW ONLY", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

// esp_qrcode_generate() renders through a callback; these carry the target box.
static TFT_eSprite* s_qrSprite = nullptr;
static int s_qrBox = 80;
static bool s_qrDrawn = false;

static void drawQrToSprite(esp_qrcode_handle_t qr) {
    int size = esp_qrcode_get_size(qr);
    int scale = s_qrBox / (size + 4);          // keep >= 2 modules of white quiet zone
    if (scale < 1) scale = 1;
    int px = size * scale;
    int off = (s_qrBox - px) / 2;
    s_qrSprite->fillRect(0, 0, s_qrBox, s_qrBox, TFT_WHITE);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (esp_qrcode_get_module(qr, x, y)) {
                s_qrSprite->fillRect(off + x * scale, off + y * scale, scale, scale, TFT_BLACK);
            }
        }
    }
    s_qrDrawn = true;
}

void UiEngine::renderReceiveScreen(const char* symbol, const char* network, const char* address,
                                   const char* qrText, const char* hint) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);
    uint16_t coinColor = getCoinColor565(symbol);

    bool haveAddr = address && address[0];
    if (haveAddr) {
        s_qrSprite = _sprite;
        s_qrBox = DISP_H;  // 80x80 white square on the left
        s_qrDrawn = false;
        esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
        cfg.display_func = drawQrToSprite;
        cfg.max_qrcode_version = 6;
        cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;  // smallest code => largest modules
        esp_qrcode_generate(&cfg, qrText && qrText[0] ? qrText : address);
    }
    if (!haveAddr || !s_qrDrawn) {
        _sprite->drawRect(0, 0, DISP_H, DISP_H, 0x4208);
        _sprite->setTextDatum(MC_DATUM);
        _sprite->setTextColor(0x8410, COLOR_BG);
        _sprite->drawString(haveAddr ? "QR ERROR" : "NO ADDRESS", DISP_H / 2, DISP_H / 2, 1);
    }

    // Right column: symbol pill, network, wrapped address, hint
    const int x0 = DISP_H + 3;
    const int colW = DISP_W - x0 - 1;
    _sprite->fillRoundRect(x0, 1, 40, 11, 2, coinColor);
    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0x0000, coinColor);
    _sprite->drawString(symbol, x0 + 20, 6, 1);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(coinColor, COLOR_BG);
    char net[14];
    strncpy(net, network ? network : "", sizeof(net) - 1);
    net[sizeof(net) - 1] = '\0';
    _sprite->drawString(net, x0, 14, 1);

    _sprite->setTextColor(0xFFFF, COLOR_BG);
    const int perLine = colW / 6;  // font 1 is 6 px wide
    if (haveAddr) {
        size_t n = strlen(address);
        int y = 25;
        for (size_t i = 0; i < n && y <= 58; i += perLine, y += 10) {
            char line[20];
            size_t k = (n - i < (size_t)perLine) ? n - i : (size_t)perLine;
            memcpy(line, address + i, k);
            line[k] = '\0';
            _sprite->drawString(line, x0, y, 1);
        }
    } else {
        _sprite->drawString("Not on this", x0, 30, 1);
        _sprite->drawString("device", x0, 40, 1);
    }

    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString(hint ? hint : "", x0, 70, 1);
    _sprite->pushSprite(0, 0);
}

void UiEngine::renderPortalScreen(const char* ssid, const char* pass, const char* qrText, const char* hint) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);
    s_qrSprite = _sprite;
    s_qrBox = DISP_H;
    s_qrDrawn = false;
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = drawQrToSprite;
    cfg.max_qrcode_version = 5;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    esp_qrcode_generate(&cfg, qrText);
    if (!s_qrDrawn) _sprite->drawRect(0, 0, DISP_H, DISP_H, 0x4208);

    const int x0 = DISP_H + 3;
    _sprite->fillRoundRect(x0, 1, 44, 11, 2, COLOR_NEON_CYAN);
    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0x0000, COLOR_NEON_CYAN);
    _sprite->drawString("SETUP", x0 + 22, 6, 1);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString("WI-FI", x0, 15, 1);
    _sprite->setTextColor(0xFFFF, COLOR_BG);
    _sprite->drawString(ssid, x0, 24, 1);
    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString("PASSWORD", x0, 36, 1);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(pass, x0, 45, 1);
    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString("192.168.4.1", x0, 58, 1);
    _sprite->drawString(hint ? hint : "", x0, 70, 1);
    _sprite->pushSprite(0, 0);
}

void UiEngine::renderWalletScreen(const char* coinName, const char* symbol, const char* address, const char* path) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    // Determine coin branding color (matching master brand palette)
    uint16_t coinColor = getCoinColor565(symbol);
    uint16_t darkTint  = dimColor565(coinColor, 0.22f);

    // ── Left Accent Pillar: glowing brand bar ────────────────────────────────
    _sprite->fillRect(0, 0, 2, DISP_H, coinColor);

    // ── Header Zone with branded tint and accent divider ─────────────────────
    _sprite->fillRect(2, 0, DISP_W - 2, 13, darkTint);
    _sprite->drawFastHLine(0, 13, DISP_W, coinColor);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(coinColor, darkTint);
    _sprite->drawString(coinName, 6, 2, 1);

    // Top metadata row: Coin Symbol (Left) & Derivation Path (Right)
    _sprite->fillRoundRect(6, 16, 44, 11, 2, coinColor);
    _sprite->setTextDatum(MC_DATUM);
    _sprite->setTextColor(0x0000, coinColor);
    _sprite->drawString(symbol, 28, 21, 1);

    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString(path, 156, 17, 1);

    // Address Display Card (Y: 29..64) with branded vertical accent bar
    _sprite->fillRoundRect(6, 29, DISP_W - 10, 35, 3, 0x0842);
    _sprite->fillRoundRect(6, 29, 3, 35, 1, coinColor);

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(0xFFFF, 0x0842);

    size_t addrLen = address ? strlen(address) : 0;
    if (addrLen <= 23) {
        _sprite->drawString(address ? address : "No Address", 13, 41, 1);
    } else {
        char line1[24];
        char line2[32];
        size_t split = (addrLen > 21) ? 21 : addrLen;
        strncpy(line1, address, split);
        line1[split] = '\0';
        strncpy(line2, address + split, sizeof(line2) - 1);
        line2[sizeof(line2) - 1] = '\0';

        _sprite->drawString(line1, 13, 34, 1);
        _sprite->setTextColor(0xDEFB, 0x0842);
        _sprite->drawString(line2, 13, 48, 1);
    }

    // ── Footer divider and hints ─────────────────────────────────────────────
    _sprite->drawFastHLine(0, 66, DISP_W, darkTint);
    _sprite->drawFastHLine(0, 66, 36, coinColor);
    drawFooter("TAP:NEXT 2X:SEED 3s:SD", 0, 0.0f);

    _sprite->pushSprite(0, 0);
}

void UiEngine::renderPortfolioCard(const char* symbol, const char* name, float balance, float priceUsd, float change24h, int activeIdx, int totalActive, float totalPortfolioUsd, bool isLive) {
    if (!_sprite) return;
    _sprite->fillSprite(COLOR_BG);

    // ── Brand Colors: exact RGB565 derived from master brand palette ─────────
    uint16_t coinColor = getCoinColor565(symbol);
    uint16_t darkTint  = dimColor565(coinColor, 0.22f);

    // ── Left Accent Pillar: glowing brand bar across whole screen ────────────
    _sprite->fillRect(0, 0, 2, DISP_H, coinColor);

    const CoinAsset* asset = CryptoCoinRegistry::findBySymbol(symbol);
    bool live = CryptoCoinRegistry::hasLivePrice(asset);
    bool stale = CryptoCoinRegistry::isPriceStale(asset);

    // ── Header Zone (Y: 0..13) with Branded Backdrop & Accent Divider ────────
    _sprite->fillRect(2, 0, DISP_W - 2, 13, darkTint);
    _sprite->drawFastHLine(0, 13, DISP_W, coinColor);

    char hdr[24];
    snprintf(hdr, sizeof(hdr), "%s %d/%d", symbol, activeIdx + 1, totalActive);
    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(coinColor, darkTint);
    _sprite->drawString(hdr, 6, 3, 1);
    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(!live ? 0x8410 : stale ? COLOR_CYBER_GOLD : COLOR_SIGNAL_GREEN, darkTint);
    _sprite->drawString(!live ? "NO PRICE" : stale ? "STALE" : "LIVE", DISP_W - 4, 3, 1);

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
        snprintf(priceStr, sizeof(priceStr), "$ --");
    }
    if (!live) snprintf(priceStr, sizeof(priceStr), "$ --");

    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(coinColor, COLOR_BG);
    _sprite->drawString(priceStr, 8, 36, 1);

    char chgStr[16];
    snprintf(chgStr, sizeof(chgStr), "%s%.2f%%", change24h >= 0 ? "+" : "", change24h);
    if (!live) chgStr[0] = '\0';
    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(change24h >= 0 ? COLOR_SIGNAL_GREEN : COLOR_CRIMSON_PANIC, COLOR_BG);
    _sprite->drawString(chgStr, 154, 36, 1);

    // Fiat valuation row
    float assetFiat = balance * priceUsd;
    char valStr[32];
    char fiat[20];
    formatUsd(assetFiat, fiat, sizeof(fiat), true);
    snprintf(valStr, sizeof(valStr), "VAL %s", live ? fiat : "$ --");
    _sprite->setTextDatum(TL_DATUM);
    _sprite->setTextColor(COLOR_CYBER_GOLD, COLOR_BG);
    _sprite->drawString(valStr, 8, 50, 1);

    char totStr[32];
    char tot[20];
    formatUsd(totalPortfolioUsd, tot, sizeof(tot), false);
    snprintf(totStr, sizeof(totStr), "ALL %s", tot);
    _sprite->setTextDatum(TR_DATUM);
    _sprite->setTextColor(0xAD55, COLOR_BG);
    _sprite->drawString(totStr, 156, 50, 1);

    // ── Footer Zone (Y: 66..79) with Brand Divider ──────────────────────────
    _sprite->drawFastHLine(0, 66, DISP_W, darkTint);
    _sprite->drawFastHLine(0, 66, 36, coinColor); // glowing accent notch

    // Bottom-left: what kind of coin / which network; bottom-right: controls
    char kind[14];
    const CatalogCoin* meta = asset ? asset->meta : nullptr;
    if (meta && meta->category == CAT_MEME) snprintf(kind, sizeof(kind), "MEME");
    else if (meta && meta->category == CAT_STABLE) snprintf(kind, sizeof(kind), "STABLE");
    else snprintf(kind, sizeof(kind), "%.10s", meta ? meta->network : "");
    _sprite->setTextDatum(BL_DATUM);
    _sprite->setTextColor(meta && meta->category == CAT_MEME ? COLOR_CYBER_GOLD :
                          meta && meta->category == CAT_STABLE ? COLOR_SIGNAL_GREEN : 0x8410, COLOR_BG);
    _sprite->drawString(kind, 6, 78, 1);

    _sprite->setTextDatum(BR_DATUM);
    _sprite->setTextColor(0x8410, COLOR_BG);
    _sprite->drawString("TAP:NEXT 3s:QR", 157, 78, 1);

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

    drawFooter(wordNum < totalWords ? "TAP:NEXT 2X:BK HOLD:EXIT" : "2X:BACK  HOLD:EXIT", 0, 0.0f);

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

    drawFooter(wordNum < totalWords ? "TAP: NEXT WORD" : "HOLD: CONFIRM", 0, 0.0f);

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

    drawFooter(readyToSign ? "HOLD:SIGN  2X:EXIT" : "2X: EXIT", 0, 0.0f);

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

    drawFooter(readyToSign ? "HOLD:SIGN  2X:EXIT" : "CANNOT SIGN  2X:EXIT", 0, 0.0f);

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
    // Drawn check mark (the built-in fonts have no ✔ glyph)
    _sprite->drawWideLine(DISP_W / 2 - 8, 22, DISP_W / 2 - 2, 28, 3, 0xFFFF, 0x0340);
    _sprite->drawWideLine(DISP_W / 2 - 2, 28, DISP_W / 2 + 9, 15, 3, 0xFFFF, 0x0340);

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
    _sprite->drawString("ERROR", DISP_W / 2, 30, 2);

    _sprite->setTextColor(0xFFFF, 0x5000);
    _sprite->drawString(message, DISP_W / 2, 52, 1);

    _sprite->pushSprite(0, 0);
}

void UiEngine::setRotation(uint8_t rot) {
    if (_tft) _tft->setRotation(rot);
    if (_sprite) _sprite->setRotation(rot);
}
