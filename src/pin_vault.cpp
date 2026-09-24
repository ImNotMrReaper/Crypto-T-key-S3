#include "crypto_p256.h"
#include "pin_vault.h"
#include "config.h"
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>

#define PIN_PBKDF2_ITERATIONS 10000

static uint8_t s_salt[16];
static uint8_t s_pinHash[32];
static uint8_t s_duressHash[32];
static bool    s_hasDuress = false;
static uint8_t s_len = 4;
static uint8_t s_fails = 0;

static bool ctEqual(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
    return d == 0;
}

void PinVault::hashPin(const char* pin, uint8_t out[32]) {
    mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char*)pin, strlen(pin),
                                  s_salt, sizeof(s_salt), PIN_PBKDF2_ITERATIONS, 32, out);
}

void PinVault::begin() {
    Preferences prefs;
    prefs.begin("vault_sec", false);

    if (prefs.getBytesLength("pin_salt") == sizeof(s_salt)) {
        prefs.getBytes("pin_salt", s_salt, sizeof(s_salt));
    } else {
        CryptoP256::secureRandom(s_salt, sizeof(s_salt));
        prefs.putBytes("pin_salt", s_salt, sizeof(s_salt));
    }
    s_fails = prefs.getUChar("pin_fails", 0);

    if (prefs.getBytesLength("pin_hash") == sizeof(s_pinHash)) {
        prefs.getBytes("pin_hash", s_pinHash, sizeof(s_pinHash));
        s_len = prefs.getUChar("pin_len", 4);
    } else {
        // Migrate the legacy plaintext PIN (or the factory default) to a hash.
        String legacy = prefs.getString("user_pin", DEFAULT_MASTER_PIN);
        s_len = (uint8_t)legacy.length();
        hashPin(legacy.c_str(), s_pinHash);
        prefs.putBytes("pin_hash", s_pinHash, sizeof(s_pinHash));
        prefs.putUChar("pin_len", s_len);
        legacy = "";
    }
    if (prefs.isKey("user_pin")) prefs.remove("user_pin");

    if (prefs.getBytesLength("duress_hash") == sizeof(s_duressHash)) {
        prefs.getBytes("duress_hash", s_duressHash, sizeof(s_duressHash));
        s_hasDuress = true;
    } else if (prefs.isKey("duress_pin")) {
        String legacy = prefs.getString("duress_pin", "");
        if (legacy.length() >= PIN_MIN_LENGTH) {
            hashPin(legacy.c_str(), s_duressHash);
            prefs.putBytes("duress_hash", s_duressHash, sizeof(s_duressHash));
            s_hasDuress = true;
        }
        legacy = "";
    }
    if (prefs.isKey("duress_pin")) prefs.remove("duress_pin");
    prefs.end();
}

PinVault::Result PinVault::check(const char* pin) {
    if (!pin) return WRONG;
    if (s_fails >= PIN_MAX_ATTEMPTS) return LOCKED_OUT;

    uint8_t h[32];
    hashPin(pin, h);
    bool master = ctEqual(h, s_pinHash, 32);
    bool duress = s_hasDuress && ctEqual(h, s_duressHash, 32);   // no duress PIN unless the user set one
    mbedtls_platform_zeroize(h, sizeof(h));

    Preferences prefs;
    prefs.begin("vault_sec", false);
    Result r;
    if (master) {
        s_fails = 0;
        r = OK;
    } else if (duress) {
        r = DURESS;
    } else {
        s_fails++;
        r = (s_fails >= PIN_MAX_ATTEMPTS) ? LOCKED_OUT : WRONG;
    }
    prefs.putUChar("pin_fails", s_fails);
    prefs.end();
    return r;
}

bool PinVault::setPin(const char* pin) {
    size_t n = pin ? strlen(pin) : 0;
    if (n < PIN_MIN_LENGTH || n > PIN_MAX_LENGTH) return false;
    for (size_t i = 0; i < n; i++) if (pin[i] < '0' || pin[i] > '9') return false;
    hashPin(pin, s_pinHash);
    s_len = (uint8_t)n;
    s_fails = 0;
    Preferences prefs;
    prefs.begin("vault_sec", false);
    prefs.putBytes("pin_hash", s_pinHash, sizeof(s_pinHash));
    prefs.putUChar("pin_len", s_len);
    prefs.putUChar("pin_fails", 0);
    prefs.end();
    return true;
}

bool PinVault::setDuressPin(const char* pin) {
    Preferences prefs;
    prefs.begin("vault_sec", false);
    size_t n = pin ? strlen(pin) : 0;
    if (n == 0) {
        s_hasDuress = false;
        prefs.remove("duress_hash");
        prefs.end();
        return true;
    }
    if (n < PIN_MIN_LENGTH || n > PIN_MAX_LENGTH) {
        prefs.end();
        return false;
    }
    uint8_t h[32];
    hashPin(pin, h);
    if (ctEqual(h, s_pinHash, 32)) {  // a duress PIN equal to the master PIN would wipe on every unlock
        prefs.end();
        return false;
    }
    memcpy(s_duressHash, h, 32);
    s_hasDuress = true;
    prefs.putBytes("duress_hash", s_duressHash, sizeof(s_duressHash));
    prefs.end();
    return true;
}

bool PinVault::hasDuress() {
    return s_hasDuress;
}

uint8_t PinVault::length() {
    return s_len;
}

uint8_t PinVault::attemptsLeft() {
    return s_fails >= PIN_MAX_ATTEMPTS ? 0 : PIN_MAX_ATTEMPTS - s_fails;
}
