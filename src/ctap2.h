#pragma once
#include <Arduino.h>
#include "cbor_codec.h"
#include "crypto_p256.h"

/**
 * CTAP2 Command Dispatcher & Authenticator Engine
 * Handles authenticatorGetInfo, authenticatorMakeCredential, authenticatorGetAssertion
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
#define CTAP2_ERR_UNSUPPORTED_ALG   0x26
#define CTAP2_ERR_OPERATION_DENIED  0x27
#define CTAP2_ERR_KEY_NOT_FOUND     0x2E
#define CTAP2_ERR_UNSUPPORTED_OPTION 0x2B
#define CTAP2_ERR_NOT_ALLOWED       0x30
#define CTAP2_ERR_OTHER             0x7F

// AuthData Flags
#define AUTHDATA_FLAG_UP            0x01 // User Present
#define AUTHDATA_FLAG_UV            0x04 // User Verified
#define AUTHDATA_FLAG_AT            0x40 // Attested Credential Data Present
#define AUTHDATA_FLAG_ED            0x80 // Extension Data Present

class Ctap2Engine {
public:
    Ctap2Engine();
    void begin();

    // Main entry point from CTAPHID
    void handleCborRequest(uint32_t cid, const uint8_t* req, uint16_t reqLen);

    // Callbacks to UI and Button verification
    typedef bool (*UserPresencePrompt)(uint32_t cid, const char* rpId, bool isRegistration);
    void setUserPresencePrompt(UserPresencePrompt prompt) { _upPrompt = prompt; }

private:
    void handleGetInfo(uint32_t cid);
    void handleMakeCredential(uint32_t cid, CborDecoder& dec);
    void handleGetAssertion(uint32_t cid, CborDecoder& dec);

    UserPresencePrompt _upPrompt;
    uint8_t _aaguid[16];
};

extern Ctap2Engine ctap2Engine;
