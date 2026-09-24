/**
 * rgb_status.cpp — Implementation of APA102-2020 DotStar Status LED
 * =================================================================
 * Hardware: LilyGo T-Dongle S3 on-board RGB LED
 * Pinout:   GPIO 40 = DI (Data), GPIO 39 = CI (Clock)
 * Color Order: BGR
 */

#include "rgb_status.h"
#include "ui_theme.h"

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

const CatalogCoin* RgbStatus::findCoin(const char* symbol) {
    if (!symbol) return nullptr;
    char sym[12];   // base symbol up to the first space, '(' or '/'
    size_t i = 0;
    while (symbol[i] && symbol[i] != ' ' && symbol[i] != '(' && symbol[i] != '/' && i < sizeof(sym) - 1) {
        sym[i] = toupper(symbol[i]);
        i++;
    }
    sym[i] = '\0';
    for (int k = 0; k < CATALOG_COUNT; k++) {
        if (strcmp(CATALOG[k].symbol, sym) == 0) return &CATALOG[k];
    }
    return nullptr;
}

RgbColor RgbStatus::getCoinRgb(const char* symbol) {
    const CatalogCoin* c = findCoin(symbol);
    if (!c) return { 0, 229, 255 };
    return { c->r, c->g, c->b };
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

    // Price-tick flash rides on top of the home and coin looks
    if (_tickDur && now - _tickStart < _tickDur && (_currentMode == LED_MODE_HOME || _currentMode == LED_MODE_COIN)) {
        float k = 1.0f - (float)(now - _tickStart) / _tickDur;   // fast attack, linear decay
        emit(_tickR, _tickG, _tickB, _tickLevel * k + 0.08f);
        return;
    }

    switch (_currentMode) {
        case LED_MODE_SOLID_AMBER:
            // Crisp warm amber / gold (Locked / PIN entry)
            setPixel(255, 130, 0, 4);
            break;

        case LED_MODE_HOME:
            renderHome(now);
            break;

        case LED_MODE_COIN:
            renderCoin(now);
            break;

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


// ─── Home look & chain rhythms ───────────────────────────────────────────────

// Perceptual output: stretch the colour to full saturation brightness (dim brand colours such
// as navy would otherwise vanish), then apply level² so fades look even to the eye.
void RgbStatus::emit(uint8_t r, uint8_t g, uint8_t b, float level) {
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    uint8_t mx = max(r, max(g, b));
    if (mx == 0) { setPixel(0, 0, 0, 0); return; }
    float k = (255.0f / mx) * level * level;
    setPixel((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k), 4);
}

void RgbStatus::renderHome(uint32_t now) {
    switch (homeTheme.fx) {
        case HOME_FX_SOLID:
            emit(homeTheme.r, homeTheme.g, homeTheme.b, 0.8f);
            break;
        case HOME_FX_RAINBOW: {
            RgbColor c = wheel((uint8_t)(now / 24));
            emit(c.r, c.g, c.b, 0.8f);
            break;
        }
        case HOME_FX_BREATHE:
        default: {
            float s = 0.5f - 0.5f * cosf((now % 4000) * (2 * PI / 4000.0f));
            emit(homeTheme.r, homeTheme.g, homeTheme.b, 0.25f + 0.75f * s);
            break;
        }
    }
}

void RgbStatus::setCoin(const char* symbol) {
    const CatalogCoin* c = findCoin(symbol);
    if (!c) {
        RgbColor f = getCoinRgb(symbol);
        setCoinColor(f.r, f.g, f.b);
        return;
    }
    if (_currentMode == LED_MODE_COIN && _coin == c) return;   // keep the rhythm's phase
    _coin = c;
    _coinStart = millis();
    setMode(LED_MODE_COIN);
}

void RgbStatus::priceTick(float pctMove) {
    uint32_t now = millis();
    if (pctMove == 0 || now - _lastTick < 400) return;   // don't strobe on bursts of updates
    _lastTick = now;
    float m = fabsf(pctMove);
    if (pctMove > 0) { _tickR = 0; _tickG = 255; _tickB = 90; }
    else             { _tickR = 255; _tickG = 30; _tickB = 30; }
    _tickLevel = 0.55f + min(m, 2.0f) * 0.225f;          // 0.55 .. 1.0
    _tickDur = 140 + (uint32_t)(min(m, 3.0f) * 90.0f);   // 140 .. 410 ms
    _tickStart = now;
}

// Visible period from the chain's block time: log-scaled 250 ms .. 10 min -> 0.7 .. 4.2 s,
// so Bitcoin beats slowly, Ethereum breathes, Solana shimmers.
static uint32_t rhythmPeriod(uint32_t blockMs) {
    float x = (logf((float)max<uint32_t>(blockMs, 250)) - logf(250.0f)) / (logf(600000.0f) - logf(250.0f));
    if (x > 1) x = 1;
    return 700 + (uint32_t)(x * 3500.0f);
}

static inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}

void RgbStatus::renderCoin(uint32_t now) {
    if (!_coin) { setPixel(0, 0, 0, 0); return; }
    const CatalogCoin* c = _coin;
    uint32_t P = rhythmPeriod(c->blockMs);
    uint32_t t = now - _coinStart;
    uint32_t ph = t % P;
    float u = (float)ph / P;
    uint8_t r = c->r, g = c->g, b = c->b;
    float level;

    switch (c->pattern) {
        case LEDP_POW: {
            // Mining: a dim ember flickering with hash attempts, then a "block found" double beat
            float ember = 0.3f + ((hash32(t / 60 + (uint32_t)(uintptr_t)c) & 0xFF) / 255.0f) * 0.14f;   // visible after level²
            // lub (0..260 ms) and dub (330..560 ms): quick attack, slower decay
            float beat = 0;
            if (ph < 40) beat = ph / 40.0f;
            else if (ph < 260) beat = 1.0f - (ph - 40) / 220.0f;
            else if (ph >= 330 && ph < 360) beat = 0.8f * (ph - 330) / 30.0f;
            else if (ph >= 360 && ph < 560) beat = 0.8f * (1.0f - (ph - 360) / 200.0f);
            level = max(ember, beat);
            break;
        }
        case LEDP_SLOT: {
            // Proof-of-stake slots: steady breathing that drifts between the two brand colours
            float s = 0.5f - 0.5f * cosf(u * 2 * PI);
            level = 0.2f + 0.8f * s;
            float mix = 0.5f - 0.5f * cosf(((t % (2 * P)) / (float)(2 * P)) * 2 * PI);
            r = r + (int)((c->r2 - r) * mix); g = g + (int)((c->g2 - g) * mix); b = b + (int)((c->b2 - b) * mix);
            break;
        }
        case LEDP_LEDGER: {
            // Consensus rounds: three quick rising pulses, then rest
            level = 0.15f;
            for (int k = 0; k < 3; k++) {
                int32_t d = (int32_t)ph - k * 130;
                if (d >= 0 && d < 110) level = max(level, (0.5f + 0.25f * k) * (1.0f - d / 110.0f));
            }
            break;
        }
        case LEDP_SHIMMER: {
            // Fast chains: quick crossfade between both colours with a light ripple
            float mix = u < 0.5f ? u * 2 : 2 - u * 2;
            r = r + (int)((c->r2 - r) * mix); g = g + (int)((c->g2 - g) * mix); b = b + (int)((c->b2 - b) * mix);
            level = 0.6f + 0.4f * (0.5f - 0.5f * cosf(u * 4 * PI));
            break;
        }
        case LEDP_STEADY:
        default:
            // Stablecoins: pegged, so nearly constant
            level = 0.7f + 0.05f * sinf((t % 6000) * (2 * PI / 6000.0f));
            break;
    }

    if (c->category == CAT_MEME) {
        // Meme sparkle: short bright pops toward white at irregular intervals
        uint32_t slot = t / 900;
        uint32_t at = hash32(slot ^ 0x5eed) % 700;
        uint32_t d = t % 900;
        if ((hash32(slot) & 3) != 0 && d >= at && d < at + 70) {
            r = r + (255 - r) * 2 / 5; g = g + (255 - g) * 2 / 5; b = b + (255 - b) * 2 / 5;
            level = 1.0f;
        }
    }
    emit(r, g, b, level);
}
