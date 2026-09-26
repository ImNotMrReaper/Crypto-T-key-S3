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
static uint8_t s_duressLen = 0;          // 0 = unknown (set before lengths were recorded)
static const char* s_lastError = "";
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

    s_hasDuress = false;   // only what NVS holds: never state left over from before
    s_duressLen = prefs.getUChar("duress_len", 0);
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

    // Count the attempt as failed in NVS before hashing: cutting power mid-check can't save a guess
    Preferences prefs;
    prefs.begin("vault_sec", false);
    uint8_t before = s_fails;
    prefs.putUChar("pin_fails", before + 1);

    uint8_t h[32];
    hashPin(pin, h);
    bool master = ctEqual(h, s_pinHash, 32);
    bool duress = s_hasDuress && ctEqual(h, s_duressHash, 32);   // no duress PIN unless the user set one
    mbedtls_platform_zeroize(h, sizeof(h));

    Result r;
    if (master) {
        s_fails = 0;
        r = OK;
    } else if (duress) {
        s_fails = before;   // the duress PIN isn't a wrong guess
        r = DURESS;
    } else {
        s_fails = before + 1;
        r = (s_fails >= PIN_MAX_ATTEMPTS) ? LOCKED_OUT : WRONG;
    }
    if (s_fails != before + 1) prefs.putUChar("pin_fails", s_fails);
    prefs.end();
    return r;
}

bool PinVault::setPin(const char* pin) {
    size_t n = pin ? strlen(pin) : 0;
    if (n < PIN_MIN_LENGTH || n > PIN_MAX_LENGTH) return false;
    for (size_t i = 0; i < n; i++) if (pin[i] < '0' || pin[i] > '9') return false;
    if (s_hasDuress && s_duressLen && n != s_duressLen) {
        s_lastError = "The duress PIN must have as many digits as the PIN: change both together.";
        return false;
    }
    uint8_t h[32];
    hashPin(pin, h);
    // A master PIN equal to the duress PIN would silently disable the duress wipe (check() tests master first)
    bool clash = s_hasDuress && ctEqual(h, s_duressHash, 32);
    if (!clash) memcpy(s_pinHash, h, 32);
    mbedtls_platform_zeroize(h, sizeof(h));
    if (clash) return false;
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
        s_duressLen = 0;
        prefs.remove("duress_hash");
        prefs.remove("duress_len");
        prefs.end();
        return true;
    }
    bool digits = true;
    for (size_t i = 0; i < n; i++) digits &= pin[i] >= '0' && pin[i] <= '9';
    if (!digits || n != s_len) {
        s_lastError = "The duress PIN must have the same number of digits as the PIN.";
        prefs.end();
        return false;
    }
    uint8_t h[32];
    hashPin(pin, h);
    if (ctEqual(h, s_pinHash, 32)) {  // a duress PIN equal to the master PIN would wipe on every unlock
        s_lastError = "The duress PIN must differ from the PIN.";
        mbedtls_platform_zeroize(h, sizeof(h));
        prefs.end();
        return false;
    }
    memcpy(s_duressHash, h, 32);
    mbedtls_platform_zeroize(h, sizeof(h));
    s_hasDuress = true;
    s_duressLen = (uint8_t)n;
    prefs.putBytes("duress_hash", s_duressHash, sizeof(s_duressHash));
    prefs.putUChar("duress_len", s_duressLen);
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

static bool validPin(const char* p) {
    size_t n = strlen(p);
    if (n < PIN_MIN_LENGTH || n > PIN_MAX_LENGTH) return false;
    for (size_t i = 0; i < n; i++) if (p[i] < '0' || p[i] > '9') return false;
    return true;
}

bool PinVault::setPins(const char* pin, const char* duress) {
    s_lastError = "";
    bool newPin = pin && pin[0];
    bool removeDuress = duress && !duress[0];
    bool newDuress = duress && duress[0];
    if (newPin && !validPin(pin)) {
        s_lastError = "The PIN must be 4 to 8 digits.";
        return false;
    }
    size_t pinLen = newPin ? strlen(pin) : s_len;
    if (newDuress && (!validPin(duress) || strlen(duress) != pinLen)) {
        s_lastError = "The duress PIN must have the same number of digits as the PIN.";
        return false;
    }
    if (newPin && newDuress && strcmp(pin, duress) == 0) {
        s_lastError = "The duress PIN must differ from the PIN.";
        return false;
    }
    // A new PIN length with an old duress PIN kept: that one can no longer be typed
    if (newPin && !newDuress && !removeDuress && s_hasDuress && s_duressLen != pinLen) {
        s_lastError = "The duress PIN must have as many digits as the PIN: set a new one or remove it.";
        return false;
    }
    // Validated: remove the old duress first so setPin's clash/length checks see the new state
    if (newDuress || removeDuress) setDuressPin("");
    if (newPin && !setPin(pin)) return false;
    if (newDuress && !setDuressPin(duress)) return false;
    return true;
}

const char* PinVault::lastError() {
    return s_lastError;
}
