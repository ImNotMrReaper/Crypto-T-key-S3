/**
 * rgb_status.cpp — Implementation of APA102-2020 DotStar Status LED
 * =================================================================
 * Hardware: LilyGo T-Dongle S3 on-board RGB LED
 * Pinout:   GPIO 40 = DI (Data), GPIO 39 = CI (Clock)
 * Color Order: BGR
 */

#include "rgb_status.h"

void RgbStatus::begin(uint8_t pinData, uint8_t pinClk) {
    _pinData = pinData;
    _pinClk = pinClk;

    pinMode(_pinData, OUTPUT);
    pinMode(_pinClk, OUTPUT);
    digitalWrite(_pinData, LOW);
    digitalWrite(_pinClk, LOW);

    // Initial clear
    setPixel(0, 0, 0, 0);
}

void RgbStatus::writeByte(uint8_t byte) {
    for (int8_t i = 7; i >= 0; i--) {
        digitalWrite(_pinData, (byte & (1 << i)) ? HIGH : LOW);
        digitalWrite(_pinClk, HIGH);
        digitalWrite(_pinClk, LOW);
    }
}

void RgbStatus::sendFrame(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    // 1. Start frame: 32 zero bits
    for (uint8_t i = 0; i < 4; i++) {
        writeByte(0x00);
    }

    // 2. LED frame: 3 bits 1 (0xE0) + 5 bits global brightness (0..31)
    uint8_t brightHeader = 0xE0 | (brightness & 0x1F);
    writeByte(brightHeader);

    // 3. Color bytes in BGR order for LilyGo T-Dongle-S3
    writeByte(b);
    writeByte(g);
    writeByte(r);

    // 4. End frame: 32 one bits
    for (uint8_t i = 0; i < 4; i++) {
        writeByte(0xFF);
    }
    digitalWrite(_pinData, LOW);
}

void RgbStatus::setPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    sendFrame(r, g, b, brightness);
}

void RgbStatus::setMode(LedMode mode) {
    _currentMode = mode;
    _step = 0;
    _direction = true;
    _isFlashing = false;
    _isRainbowFlash = false;
    _lastUpdate = 0; // Force immediate update
    update();
}

RgbColor RgbStatus::wheel(uint8_t wheelPos) {
    wheelPos = 255 - wheelPos;
    if (wheelPos < 85) {
        return { (uint8_t)(255 - wheelPos * 3), 0, (uint8_t)(wheelPos * 3) };
    }
    if (wheelPos < 170) {
        wheelPos -= 85;
        return { 0, (uint8_t)(wheelPos * 3), (uint8_t)(255 - wheelPos * 3) };
    }
    wheelPos -= 170;
    return { (uint8_t)(wheelPos * 3), (uint8_t)(255 - wheelPos * 3), 0 };
}

RgbColor RgbStatus::getCoinRgb(const char* symbol) {
    if (!symbol) return { 0, 229, 255 };
    // ── CRYPTO ────────────────────────────────────────────────────────────────
    // Each entry is the closest true brand hex, unique across all 44 coins
    if (strcmp(symbol, "BTC")    == 0) return { 247, 147,  26 }; // #F7931A Bitcoin Orange
    if (strcmp(symbol, "ETH")    == 0) return {  98, 126, 234 }; // #627EEA Ethereum Indigo
    if (strcmp(symbol, "SOL")    == 0) return {  20, 241, 149 }; // #14F195 Solana Green-Teal
    if (strcmp(symbol, "BNB")    == 0) return { 243, 186,  47 }; // #F3BA2F BNB Gold
    if (strcmp(symbol, "XRP")    == 0) return {   0, 136, 255 }; // #0088FF XRP Blue
    if (strcmp(symbol, "ADA")    == 0) return {   0,  51, 173 }; // #0033AD Cardano Cobalt
    if (strcmp(symbol, "AVAX")   == 0) return { 232,  65,  66 }; // #E84142 Avalanche Red
    if (strcmp(symbol, "DOT")    == 0) return { 230,   0, 122 }; // #E6007A Polkadot Pink
    if (strcmp(symbol, "LINK")   == 0) return {  43, 110, 245 }; // #2B6EF5 Chainlink Blue
    if (strcmp(symbol, "LTC")    == 0) return { 191, 191, 191 }; // #BFBFBF Litecoin Silver
    if (strcmp(symbol, "BCH")    == 0) return {   0, 168,  77 }; // #00A84D Bitcoin Cash Green
    if (strcmp(symbol, "ATOM")   == 0) return {  99,  88, 199 }; // #6358C7 Cosmos Purple
    if (strcmp(symbol, "POL")    == 0) return { 130,  71, 229 }; // #8247E5 Polygon Violet
    if (strcmp(symbol, "TRX")    == 0) return { 220,  20,  20 }; // #DC1414 TRON Red
    if (strcmp(symbol, "NEAR")   == 0) return {   0, 194, 204 }; // #00C2CC NEAR Teal
    if (strcmp(symbol, "SUI")    == 0) return {  46, 176, 228 }; // #2EB0E4 Sui Sky Blue
    if (strcmp(symbol, "APT")    == 0) return {   0, 191, 165 }; // #00BFA5 Aptos Mint
    if (strcmp(symbol, "TON")    == 0) return {   0, 136, 204 }; // #0088CC TON Blue
    if (strcmp(symbol, "XLM")    == 0) return {  14, 182, 236 }; // #0EB6EC Stellar Blue
    if (strcmp(symbol, "ALGO")   == 0) return { 100, 180, 160 }; // #64B4A0 Algorand Sage
    if (strcmp(symbol, "HBAR")   == 0) return {   0, 163, 174 }; // #00A3AE Hedera Teal
    if (strcmp(symbol, "VET")    == 0) return {  32, 201, 250 }; // #20C9FA VeChain Aqua
    if (strcmp(symbol, "FIL")    == 0) return {   0,  84, 255 }; // #0054FF Filecoin Blue
    if (strcmp(symbol, "ICP")    == 0) return { 150, 110, 229 }; // #966EE5 ICP Mid-Gradient
    if (strcmp(symbol, "TAO")    == 0) return { 128, 128, 128 }; // #808080 Bittensor Gray
    if (strcmp(symbol, "INJ")    == 0) return {   0, 148, 255 }; // #0094FF Injective Blue
    if (strcmp(symbol, "ARB")    == 0) return {  18, 120, 214 }; // #1278D6 Arbitrum Blue
    if (strcmp(symbol, "OP")     == 0) return { 255,   4,  32 }; // #FF0420 Optimism Red
    if (strcmp(symbol, "KAS")    == 0) return { 112, 221, 176 }; // #70DDB0 Kaspa Teal
    if (strcmp(symbol, "XMR")    == 0) return { 255, 102,   0 }; // #FF6600 Monero Orange
    if (strcmp(symbol, "EGLD")   == 0) return {  35, 162, 255 }; // #23A2FF MultiversX Blue
    if (strcmp(symbol, "UNI")    == 0) return { 255,   0, 122 }; // #FF007A Uniswap Pink
    // ── MEME COINS ────────────────────────────────────────────────────────────
    if (strcmp(symbol, "DOGE")   == 0) return { 255, 189,  35 }; // #FFBD23 Dogecoin Gold
    if (strcmp(symbol, "SHIB")   == 0) return { 255,  90,   0 }; // #FF5A00 Shiba Amber
    if (strcmp(symbol, "PEPE")   == 0) return {   0, 200,  50 }; // #00C832 Pepe Green
    if (strcmp(symbol, "BONK")   == 0) return { 255, 180,   0 }; // #FFB400 Bonk Yellow
    if (strcmp(symbol, "FLOKI")  == 0) return { 255, 164,   0 }; // #FFA400 Floki Gold
    if (strcmp(symbol, "WIF")    == 0) return { 180, 100,  60 }; // #B4643C dogwifhat Peach
    if (strcmp(symbol, "BRETT")  == 0) return {  66, 135, 245 }; // #4287F5 Brett Blue
    if (strcmp(symbol, "MOG")    == 0) return { 160,  40, 220 }; // #A028DC Mog Violet
    if (strcmp(symbol, "TURBO")  == 0) return { 240,  70,   0 }; // #F04600 Turbo Orange-Red
    if (strcmp(symbol, "POPCAT") == 0) return { 255, 150, 180 }; // #FF96B4 Popcat Pink
    if (strcmp(symbol, "NEIRO")  == 0) return { 255, 220, 170 }; // #FFDCAA Neiro Cream
    if (strcmp(symbol, "GOAT")   == 0) return { 100, 200, 100 }; // #64C864 Goat Sage Green
    return { 0, 229, 255 }; // Cyan fallback
}

void RgbStatus::setCoinColor(uint8_t r, uint8_t g, uint8_t b) {
    _coinR = r;
    _coinG = g;
    _coinB = b;
    setMode(LED_MODE_COIN_GLOW);
}

void RgbStatus::flashSuccess() {
    _flashUntil = millis() + 600;
    _isFlashing = true;
    _isRainbowFlash = false;
    _flashR = 0; _flashG = 255; _flashB = 60;
    setPixel(0, 255, 60, 5);
}

void RgbStatus::flashRainbow(uint16_t durationMs) {
    _flashUntil = millis() + durationMs;
    _isFlashing = true;
    _isRainbowFlash = true;
}

void RgbStatus::flashTap(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs) {
    _flashUntil = millis() + durationMs;
    _isFlashing = true;
    _isRainbowFlash = false;
    _flashR = r; _flashG = g; _flashB = b;
    setPixel(r, g, b, 6);
}

void RgbStatus::flashDoubleTap(uint8_t r, uint8_t g, uint8_t b) {
    flashTap(r, g, b, 120);
}

void RgbStatus::setEntropyJitter(uint32_t jitterHash) {
    _step = (uint16_t)(jitterHash & 0xFF);
    RgbColor c = wheel((uint8_t)_step);
    flashTap(c.r, c.g, c.b, 90);
}

void RgbStatus::setHoldProgress(float progress0to1) {
    _holdProgress = progress0to1;
    if (_currentMode != LED_MODE_HOLD_RAMP) {
        _currentMode = LED_MODE_HOLD_RAMP;
    }
}

void RgbStatus::update() {
    uint32_t now = millis();

    // Priority flash handling (tactile tap, rainbow success, error strobe)
    if (_isFlashing) {
        if (now < _flashUntil) {
            if (_isRainbowFlash) {
                uint8_t pos = (uint8_t)((now * 2) & 0xFF);
                RgbColor c = wheel(pos);
                setPixel(c.r, c.g, c.b, 6);
            } else {
                setPixel(_flashR, _flashG, _flashB, 5);
            }
            return;
        } else {
            _isFlashing = false;
            _isRainbowFlash = false;
        }
    }

    if (now - _lastUpdate < 20) return; // 50 Hz smooth refresh
    _lastUpdate = now;

    switch (_currentMode) {
        case LED_MODE_SOLID_AMBER:
            // Crisp warm amber / gold (Locked / PIN entry)
            setPixel(255, 130, 0, 4);
            break;

        case LED_MODE_BREATHE_CYAN: {
            // Smooth Antigravity Cyan breathing effect
            if (_direction) {
                _step += 4;
                if (_step >= 220) _direction = false;
            } else {
                if (_step > 20) _step -= 4;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            setPixel(0, val, val, 4);
            break;
        }

        case LED_MODE_BREATHE_GREEN: {
            // Smooth emerald green breathing for Passkey Hub Ready state
            if (_direction) {
                _step += 4;
                if (_step >= 220) _direction = false;
            } else {
                if (_step > 20) _step -= 4;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            setPixel(0, val, (uint8_t)(val * 0.25f), 4);
            break;
        }

        case LED_MODE_PULSE_GREEN: {
            // Rhythmic heartbeat pulse awaiting biometric / FIDO2 User Presence confirmation
            _step = (_step + 8) % 360;
            // Pseudo sine wave breathing
            uint8_t val = (uint8_t)(128 + 120 * sin(_step * 3.14159f / 180.0f));
            setPixel(0, val > 30 ? val : 30, (uint8_t)(val * 0.3f), 5);
            break;
        }

        case LED_MODE_COIN_GLOW: {
            // Organic glowing brand color of the currently selected cryptocurrency
            if (_direction) {
                _step += 3;
                if (_step >= 240) _direction = false;
            } else {
                if (_step > 50) _step -= 3;
                else _direction = true;
            }
            float scale = (float)_step / 255.0f;
            uint8_t r = (uint8_t)(_coinR * scale);
            uint8_t g = (uint8_t)(_coinG * scale);
            uint8_t b = (uint8_t)(_coinB * scale);
            setPixel(r, g, b, 4);
            break;
        }

        case LED_MODE_SOFTAP_PULSE: {
            // Radiant Cyberpunk Violet / Neon Magenta pulse during SoftAP Captive Portal
            if (_direction) {
                _step += 6;
                if (_step >= 240) _direction = false;
            } else {
                if (_step > 30) _step -= 6;
                else _direction = true;
            }
            float scale = (float)_step / 255.0f;
            setPixel((uint8_t)(220 * scale), 0, (uint8_t)(200 * scale), 4);
            break;
        }

        case LED_MODE_RAINBOW_CYCLE: {
            _step = (_step + 2) & 0xFF;
            RgbColor c = wheel((uint8_t)_step);
            setPixel(c.r, c.g, c.b, 4);
            break;
        }

        case LED_MODE_HOLD_RAMP: {
            // Crescendo from ambient to brilliant emerald green as button is held
            float p = _holdProgress;
            if (p < 0.0f) p = 0.0f;
            if (p > 1.0f) p = 1.0f;
            uint8_t r = (uint8_t)(255 * (1.0f - p));
            uint8_t g = (uint8_t)(255 * p);
            uint8_t b = (uint8_t)(50 * (1.0f - p));
            setPixel(r, g, b, 5);
            break;
        }

        case LED_MODE_SOLID_BLUE:
            // Crisp sapphire blue (Air-Gap SD mode)
            setPixel(0, 80, 255, 4);
            break;

        case LED_MODE_PULSE_PURPLE: {
            // BLE companion pulse
            if (_direction) {
                _step += 6;
                if (_step >= 220) _direction = false;
            } else {
                if (_step > 25) _step -= 6;
                else _direction = true;
            }
            uint8_t val = (uint8_t)_step;
            setPixel(val, 0, val, 4);
            break;
        }

        case LED_MODE_STROBE_RED:
            _step++;
            if ((_step % 6) < 3) {
                setPixel(255, 0, 0, 7); // Full strobe red (Duress Wipe)
            } else {
                setPixel(0, 0, 0, 0);
            }
            break;

        case LED_MODE_FLASH_GREEN_OK:
            setPixel(0, 255, 50, 5);
            break;

        case LED_MODE_ENTROPY_CHURN: {
            _step = (_step + 5) & 0xFF;
            RgbColor c = wheel((uint8_t)_step);
            setPixel(c.r, c.g, c.b, 4);
            break;
        }

        case LED_MODE_OFF:
        default:
            setPixel(0, 0, 0, 0);
            break;
    }
}

