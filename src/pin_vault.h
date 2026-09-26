#pragma once
#include <Arduino.h>

/**
 * Vault PIN storage & verification.
 *
 * The master and duress PINs are kept only as salted PBKDF2-HMAC-SHA256 hashes in NVS
 * ("vault_sec": pin_salt, pin_hash, duress_hash, pin_len, pin_fails). Legacy plaintext
 * "user_pin" / "duress_pin" entries are migrated and erased on first boot.
 *
 * Wrong PINs are counted persistently; PIN_MAX_ATTEMPTS in a row reports LOCKED_OUT so
 * the caller can run the anti-hammering wipe.
 */

#define PIN_MAX_ATTEMPTS 10

class PinVault {
public:
    enum Result { OK, DURESS, WRONG, LOCKED_OUT };

    static void begin();
    static Result check(const char* pin);
    static bool setPin(const char* pin);
    // Empty or null removes the duress PIN (there is no built-in one). A duress PIN must have
    // exactly as many digits as the master PIN: the PIN screen has one slot per master digit,
    // so a shorter one could never be typed (and a different length would give it away).
    static bool setDuressPin(const char* pin);
    // Changes both in one step (the portal): validates everything before writing anything.
    // pin/duress null = keep; duress "" = remove. False + lastError() on any problem.
    static bool setPins(const char* pin, const char* duress);
    static const char* lastError();
    static bool hasDuress();
    static uint8_t length();          // digits in the master PIN (the PIN screen needs it)
    static uint8_t attemptsLeft();

private:
    static void hashPin(const char* pin, uint8_t out[32]);
};
