#pragma once
#include <Arduino.h>

/**
 * Resident (discoverable) credential store for passkeys.
 *
 * Only metadata lives here: the private key is re-derived from the wrapped
 * credential ID (see CryptoP256::verifyCredentialId), so NVS never holds keys.
 * Slots are cached in RAM and persisted one blob per slot in NVS "fido_rk".
 */

#define FIDO_RK_SLOTS        32
#define FIDO_RK_RPID_LEN     64
#define FIDO_RK_USERID_LEN   64
#define FIDO_RK_NAME_LEN     48

struct ResidentCred {
    uint8_t  magic;                          // FIDO_RK_MAGIC when the slot is in use
    uint8_t  credId[32];
    uint8_t  rpIdHash[32];
    char     rpId[FIDO_RK_RPID_LEN];
    uint8_t  userIdLen;
    uint8_t  userId[FIDO_RK_USERID_LEN];
    char     userName[FIDO_RK_NAME_LEN];
    char     displayName[FIDO_RK_NAME_LEN];
    uint32_t created;                        // signature counter at creation (orders newest first)
};

class FidoStore {
public:
    void begin();
    // Stores a passkey, replacing an existing one for the same rpIdHash + userId.
    bool save(const ResidentCred& cred);
    // Fills slot indices for rpIdHash, newest first. Returns the count.
    uint8_t findByRp(const uint8_t* rpIdHash, uint8_t* slotsOut, uint8_t maxOut) const;
    int findByCredId(const uint8_t* credId) const;
    const ResidentCred& slot(uint8_t i) const { return _slots[i]; }
    uint8_t count() const;
    void eraseAll();

private:
    bool persist(uint8_t i);
    ResidentCred _slots[FIDO_RK_SLOTS];
};

extern FidoStore fidoStore;
