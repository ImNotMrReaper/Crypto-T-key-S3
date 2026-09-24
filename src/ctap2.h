#pragma once
#include <Arduino.h>
#include "cbor_codec.h"
#include "crypto_p256.h"
#include "fido_store.h"

/**
 * CTAP2.0 Authenticator Engine (+ CTAP1/U2F)
 * authenticatorMakeCredential, GetAssertion, GetNextAssertion, GetInfo, ClientPIN, Reset
 * Resident credentials (passkeys) are indexed by FidoStore.
 */

// CTAP2 Command Byte Codes
#define CTAP2_CMD_MAKE_CREDENTIAL   0x01
#define CTAP2_CMD_GET_ASSERTION     0x02
#define CTAP2_CMD_GET_INFO          0x04
#define CTAP2_CMD_CLIENT_PIN        0x06
#define CTAP2_CMD_RESET             0x07
#define CTAP2_CMD_GET_NEXT_ASSERTION 0x08

// CTAP2 Response Status Codes
#define CTAP2_OK                    0x00
#define CTAP2_ERR_INVALID_CMD       0x01
#define CTAP2_ERR_INVALID_PARAM     0x02
#define CTAP2_ERR_INVALID_LENGTH    0x03
#define CTAP2_ERR_CBOR_UNEXPECTED_TYPE 0x11
#define CTAP2_ERR_INVALID_CBOR      0x12
#define CTAP2_ERR_MISSING_PARAMETER 0x14
#define CTAP2_ERR_CREDENTIAL_EXCLUDED 0x19
#define CTAP2_ERR_UNSUPPORTED_ALG   0x26
#define CTAP2_ERR_OPERATION_DENIED  0x27
#define CTAP2_ERR_KEY_STORE_FULL    0x28
#define CTAP2_ERR_UNSUPPORTED_OPTION 0x2B
#define CTAP2_ERR_INVALID_OPTION    0x2C
#define CTAP2_ERR_KEEPALIVE_CANCEL  0x2D
#define CTAP2_ERR_NO_CREDENTIALS    0x2E
#define CTAP2_ERR_USER_ACTION_TIMEOUT 0x2F
#define CTAP2_ERR_NOT_ALLOWED       0x30
#define CTAP2_ERR_PIN_INVALID       0x31
#define CTAP2_ERR_PIN_BLOCKED       0x32
#define CTAP2_ERR_PIN_AUTH_INVALID  0x33
#define CTAP2_ERR_PIN_AUTH_BLOCKED  0x34
#define CTAP2_ERR_PIN_NOT_SET       0x35
#define CTAP2_ERR_PIN_REQUIRED      0x36
#define CTAP2_ERR_PIN_POLICY_VIOLATION 0x37
#define CTAP2_ERR_INVALID_SUBCOMMAND 0x3E
#define CTAP2_ERR_OTHER             0x7F

// SubCommands for authenticatorClientPIN (0x06)
#define CTAP2_PIN_SUBCMD_GET_PIN_RETRIES         0x01
#define CTAP2_PIN_SUBCMD_GET_KEY_AGREEMENT       0x02
#define CTAP2_PIN_SUBCMD_SET_PIN                 0x03
#define CTAP2_PIN_SUBCMD_CHANGE_PIN              0x04
#define CTAP2_PIN_SUBCMD_GET_PIN_TOKEN           0x05

// AuthData Flags
#define AUTHDATA_FLAG_UP            0x01 // User Present
#define AUTHDATA_FLAG_UV            0x04 // User Verified
#define AUTHDATA_FLAG_AT            0x40 // Attested Credential Data Present
#define AUTHDATA_FLAG_ED            0x80 // Extension Data Present

#define FIDO_PIN_MAX_RETRIES        8

// hmac-secret extension request (getAssertion): platform key agreement key, encrypted salts
struct HmacSecretReq {
    bool     present;
    uint8_t  platformKey[64];   // X || Y
    const uint8_t* saltEnc;     // 32 or 64 bytes
    size_t   saltEncLen;
    const uint8_t* saltAuth;    // 16 bytes
    size_t   saltAuthLen;
};

class Ctap2Engine {
public:
    Ctap2Engine();
    void begin();

    // Main entry points from CTAPHID
    void handleCborRequest(uint32_t cid, const uint8_t* req, uint16_t reqLen);
    void handleCtap1Msg(uint32_t cid, const uint8_t* req, uint16_t reqLen);

    // Callbacks to UI and Button verification
    typedef bool (*UserPresencePrompt)(uint32_t cid, const char* rpId, bool isRegistration);
    void setUserPresencePrompt(UserPresencePrompt prompt) { _upPrompt = prompt; }

    // FIDO2 clientPIN state (set from the browser / fido2-token, stored as LEFT(SHA-256(PIN), 16))
    bool isFidoPinSet() const { return _pinSet; }
    uint8_t getPinRetries() const { return _pinRetries; }
    void resetPinRetries();

    // Marks the moment the key starts servicing requests (end of setup()). The
    // authenticatorReset window counts from here, not from chip power-on.
    void markReady() { _readyAt = millis(); }

private:
    void handleGetInfo(uint32_t cid);
    void handleMakeCredential(uint32_t cid, CborDecoder& dec);
    void handleGetAssertion(uint32_t cid, CborDecoder& dec);
    void handleGetNextAssertion(uint32_t cid);
    void handleClientPin(uint32_t cid, CborDecoder& dec);
    void handleReset(uint32_t cid);

    bool waitForUser(uint32_t cid, const char* label, bool isRegistration);
    void answerSelectionProbe(uint32_t cid, const char* rpId);
    bool checkPinAuth(const uint8_t* pinAuth, size_t len, const uint8_t* clientDataHash);
    void sendAssertion(uint32_t cid, const char* rpId, const uint8_t* clientDataHash,
                       const uint8_t* credId, const ResidentCred* rk, uint8_t flags,
                       bool withUserDetails, uint8_t numberOfCredentials,
                       const struct HmacSecretReq* hmac = nullptr);
    bool hmacSecretOutput(const uint8_t* credId, const struct HmacSecretReq& req,
                          uint8_t* extCbor, size_t* extLen);
    void storePinHash(const uint8_t* hash16);
    void setPinRetries(uint8_t retries);
    uint8_t pinFailure();

    UserPresencePrompt _upPrompt;
    uint8_t _aaguid[16];
    uint32_t _readyAt;

    // ClientPIN (PIN protocol 1)
    bool    _pinSet;
    uint8_t _pinHash[16];
    uint8_t _pinRetries;
    uint8_t _consecutivePinFails;   // 3 in a row => PIN_AUTH_BLOCKED until replug
    uint8_t _ephemPrivKey[32];
    uint8_t _ephemPubKeyRaw[64];
    bool    _hasEphemKey;
    uint8_t _pinToken[32];
    bool    _pinTokenValid;

    // authenticatorGetNextAssertion state
    struct {
        bool     active;
        uint32_t expiresAt;
        char     rpId[128];
        uint8_t  clientDataHash[32];
        uint8_t  flags;
        bool     withUserDetails;
        uint8_t  slots[FIDO_RK_SLOTS];
        uint8_t  count;
        uint8_t  next;
    } _pending;
};

extern Ctap2Engine ctap2Engine;
