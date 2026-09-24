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
    lastR = r; lastG = g; lastB = b; lastBrightness = brightness;
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

    // Extract base symbol token up to first space, '(', or '/'
    char sym[12];
    size_t i = 0;
    while (symbol[i] && symbol[i] != ' ' && symbol[i] != '(' && symbol[i] != '/' && i < sizeof(sym) - 1) {
        sym[i] = toupper(symbol[i]);
        i++;
    }
    sym[i] = '\0';

    // ── CRYPTO (32 Assets) ────────────────────────────────────────────────────
    // Authentic brand colors verified with wide perceptual distance and zero collisions
    if (strcmp(sym, "BTC")   == 0) return { 247, 147,  26 }; // #F7931A Bitcoin Classic Orange
    if (strcmp(sym, "ETH")   == 0) return {  98, 126, 234 }; // #627EEA Ethereum Indigo
    if (strcmp(sym, "SOL")   == 0) return {  20, 241, 149 }; // #14F195 Solana Neon Teal-Green
    if (strcmp(sym, "BNB")   == 0) return { 243, 186,  47 }; // #F3BA2F Binance Canary Gold
    if (strcmp(sym, "XRP")   == 0) return {   0, 120, 230 }; // #0078E6 Ripple / XRP Cobalt Blue
    if (strcmp(sym, "ADA")   == 0) return {   0,  45, 160 }; // #002DA0 Cardano Deep Blue
    if (strcmp(sym, "AVAX")  == 0) return { 232,  65,  66 }; // #E84142 Avalanche Crimson
    if (strcmp(sym, "DOT")   == 0) return { 230,   0, 122 }; // #E6007A Polkadot Hot Pink
    if (strcmp(sym, "LINK")  == 0) return {  43, 110, 245 }; // #2B6EF5 Chainlink Blue
    if (strcmp(sym, "LTC")   == 0) return { 175, 180, 185 }; // #AFB4B9 Litecoin Silver
    if (strcmp(sym, "BCH")   == 0) return {  10, 193, 142 }; // #0AC18E Bitcoin Cash Emerald Green
    if (strcmp(sym, "ATOM")  == 0) return {  60,  65, 145 }; // #3C4191 Cosmos Deep Violet
    if (strcmp(sym, "POL")   == 0) return { 130,  71, 229 }; // #8247E5 Polygon Vivid Violet
    if (strcmp(sym, "TRX")   == 0) return { 210,  15,  15 }; // #D20F0F TRON Crimson Red
    if (strcmp(sym, "NEAR")  == 0) return {   0, 236, 151 }; // #00EC97 NEAR Electric Lime
    if (strcmp(sym, "SUI")   == 0) return {  77, 162, 255 }; // #4DA2FF Sui Water Blue
    if (strcmp(sym, "APT")   == 0) return {  45, 216, 167 }; // #2DD8A7 Aptos Mint
    if (strcmp(sym, "TON")   == 0) return {  40, 180, 245 }; // #28B4F5 TON Telegram Sky Blue
    if (strcmp(sym, "XLM")   == 0) return {  15, 135, 195 }; // #0F87C3 Stellar Deep Steel Blue
    if (strcmp(sym, "ALGO")  == 0) return { 100, 180, 160 }; // #64B4A0 Algorand Sage
    if (strcmp(sym, "HBAR")  == 0) return {   0, 160, 175 }; // #00A0AF Hedera Dark Teal
    if (strcmp(sym, "VET")   == 0) return {  32, 205, 250 }; // #20CDFA VeChain Bright Aqua
    if (strcmp(sym, "FIL")   == 0) return {   0,  70, 210 }; // #0046D2 Filecoin Deep Blue
    if (strcmp(sym, "ICP")   == 0) return { 150, 110, 229 }; // #966EE5 ICP Mid-Gradient Purple
    if (strcmp(sym, "TAO")   == 0) return { 225, 225, 230 }; // #E1E1E6 Bittensor Platinum White
    if (strcmp(sym, "INJ")   == 0) return {   0, 245, 255 }; // #00F5FF Injective Electric Cyan
    if (strcmp(sym, "ARB")   == 0) return {  18, 120, 240 }; // #1278F0 Arbitrum Ocean Blue
    if (strcmp(sym, "OP")    == 0) return { 255,   4,  32 }; // #FF0420 Optimism Bright Scarlet
    if (strcmp(sym, "KAS")   == 0) return {  73, 234, 203 }; // #49EACB Kaspa Turquoise
    if (strcmp(sym, "XMR")   == 0) return { 255, 102,   0 }; // #FF6600 Monero Flame Orange
    if (strcmp(sym, "EGLD")  == 0) return {  35, 247, 221 }; // #23F7DD MultiversX Neon Cyan
    if (strcmp(sym, "UNI")   == 0) return { 255,   4, 180 }; // #FF04B4 Uniswap Hot Unicorn Pink

    // ── MEME COINS (12 Assets) ────────────────────────────────────────────────
    if (strcmp(sym, "DOGE")   == 0) return { 205, 165,  40 }; // #CDA528 Dogecoin Warm Golden Sand
    if (strcmp(sym, "SHIB")   == 0) return { 225,  75,   0 }; // #E14B00 Shiba Inu Coat Red-Orange
    if (strcmp(sym, "PEPE")   == 0) return {   0, 200,  50 }; // #00C832 Pepe Frog Green
    if (strcmp(sym, "BONK")   == 0) return { 255, 140,   0 }; // #FF8C00 Bonk Tangerine
    if (strcmp(sym, "FLOKI")  == 0) return { 215, 130,  15 }; // #D7820F Floki Viking Bronze
    if (strcmp(sym, "WIF")    == 0) return { 201, 122,  86 }; // #C97A56 dogwifhat Beanie Tan
    if (strcmp(sym, "BRETT")  == 0) return {   0,  82, 255 }; // #0052FF Brett Base Royal Blue
    if (strcmp(sym, "MOG")    == 0) return { 176,  38, 255 }; // #B026FF Mog Electric Neon Violet
    if (strcmp(sym, "TURBO")  == 0) return { 245,  50,   0 }; // #F53200 Turbo Fire Red
    if (strcmp(sym, "POPCAT") == 0) return { 255, 128, 171 }; // #FF80AB Popcat Pastel Pink
    if (strcmp(sym, "NEIRO")  == 0) return { 255, 200, 130 }; // #FFC882 Neiro Caramel Cream
    if (strcmp(sym, "GOAT")   == 0) return { 139, 195,  74 }; // #8BC34A Goatseus Sage Green

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

