#include "ctap2.h"
#include "ctaphid.h"
#include <string.h>
#include <mbedtls/platform_util.h>

Ctap2Engine ctap2Engine;

// Custom AAGUID for Crypto TKey S3 (UUID v4)
static const uint8_t TKEY_AAGUID[16] = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x40, 0x00,
    0x80, 0x00, 0x74, 0x6b, 0x65, 0x79, 0x73, 0x33
};

// Standard X.509 DER Attestation Certificate for U2F Registration
static const uint8_t U2F_ATTESTATION_CERT[320] = {
    0x30, 0x82, 0x01, 0x3C, 0x30, 0x81, 0xE2, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x02, 0x05, 0x39,
    0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02, 0x30, 0x27, 0x31, 0x25,
    0x30, 0x23, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0C, 0x1C, 0x43, 0x72, 0x79, 0x70, 0x74, 0x6F, 0x20,
    0x54, 0x4B, 0x65, 0x79, 0x20, 0x53, 0x33, 0x20, 0x41, 0x75, 0x74, 0x68, 0x65, 0x6E, 0x74, 0x69,
    0x63, 0x61, 0x74, 0x6F, 0x72, 0x30, 0x1E, 0x17, 0x0D, 0x32, 0x34, 0x30, 0x31, 0x30, 0x31, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x5A, 0x17, 0x0D, 0x34, 0x34, 0x30, 0x31, 0x30, 0x31, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x5A, 0x30, 0x27, 0x31, 0x25, 0x30, 0x23, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x0C, 0x1C, 0x43, 0x72, 0x79, 0x70, 0x74, 0x6F, 0x20, 0x54, 0x4B, 0x65, 0x79, 0x20, 0x53, 0x33,
    0x20, 0x41, 0x75, 0x74, 0x68, 0x65, 0x6E, 0x74, 0x69, 0x63, 0x61, 0x74, 0x6F, 0x72, 0x30, 0x59,
    0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A, 0x86, 0x48,
    0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x2C, 0xAA, 0xEF, 0x8D, 0x70, 0xEE, 0x81,
    0x64, 0xDF, 0xAA, 0xBA, 0xD0, 0x07, 0x25, 0xA1, 0x69, 0xD2, 0xE6, 0xFB, 0xDD, 0x5B, 0xEB, 0x22,
    0xCE, 0xCE, 0xD1, 0x3F, 0x63, 0x35, 0x78, 0x38, 0xB4, 0x83, 0x08, 0x4E, 0xB4, 0x9B, 0x26, 0x5D,
    0xC8, 0x1C, 0x56, 0xC9, 0xAF, 0x3D, 0xD9, 0xAD, 0x6D, 0x33, 0x09, 0xBE, 0x03, 0x78, 0xD2, 0x76,
    0x52, 0xFF, 0x0F, 0x12, 0x4A, 0x4D, 0x8D, 0xBD, 0x1A, 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48,
    0xCE, 0x3D, 0x04, 0x03, 0x02, 0x03, 0x49, 0x00, 0x30, 0x46, 0x02, 0x21, 0x00, 0xEF, 0x51, 0x7D,
    0xD3, 0xAE, 0xCD, 0xD5, 0xA1, 0xF8, 0x33, 0xB3, 0x48, 0xED, 0x2E, 0xC1, 0xAB, 0xBF, 0xC4, 0x7D,
    0xDA, 0xA1, 0x8C, 0x8A, 0x6E, 0xCB, 0x23, 0x93, 0x8F, 0xA2, 0x3E, 0xC5, 0x78, 0x02, 0x21, 0x00,
    0xD5, 0x84, 0x3B, 0x4F, 0x15, 0x46, 0xDC, 0x0C, 0x23, 0xB5, 0xB4, 0xFE, 0xEF, 0x63, 0x23, 0xA5,
    0x20, 0x2A, 0x0C, 0x26, 0xDB, 0x90, 0x50, 0x25, 0x79, 0xD3, 0x57, 0xC6, 0x7C, 0x5F, 0xBE, 0xD0
};

static const uint8_t U2F_ATTESTATION_PRIVKEY[32] = {
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x07
};

#include <Preferences.h>

Ctap2Engine::Ctap2Engine() 
    : _upPrompt(nullptr), _pinRetries(8), _hasEphemKey(false), _pinTokenValid(false) {
    memcpy(_aaguid, TKEY_AAGUID, 16);
    strncpy(_masterPin, "1234", sizeof(_masterPin) - 1);
    mbedtls_platform_zeroize(_ephemPrivKey, sizeof(_ephemPrivKey));
    mbedtls_platform_zeroize(_ephemPubKeyRaw, sizeof(_ephemPubKeyRaw));
    mbedtls_platform_zeroize(_pinToken, sizeof(_pinToken));
}

void Ctap2Engine::begin() {
    cryptoP256.begin();

    Preferences prefs;
    prefs.begin("fido_vault", false);
    _pinRetries = (uint8_t)prefs.getUInt("pin_retries", 8);
    prefs.end();

    prefs.begin("vault_sec", true);
    String savedPin = prefs.getString("user_pin", "1234");
    strncpy(_masterPin, savedPin.c_str(), sizeof(_masterPin) - 1);
    _masterPin[sizeof(_masterPin) - 1] = '\0';
    prefs.end();

    ctapHid.setCborHandler([](uint32_t cid, const uint8_t* req, uint16_t reqLen) {
        ctap2Engine.handleCborRequest(cid, req, reqLen);
    });
    ctapHid.setMsgHandler([](uint32_t cid, const uint8_t* req, uint16_t reqLen) {
        ctap2Engine.handleCtap1Msg(cid, req, reqLen);
    });
}

void Ctap2Engine::setMasterPin(const char* pin) {
    if (!pin) return;
    strncpy(_masterPin, pin, sizeof(_masterPin) - 1);
    _masterPin[sizeof(_masterPin) - 1] = '\0';

    Preferences prefs;
    prefs.begin("vault_sec", false);
    prefs.putString("user_pin", _masterPin);
    prefs.end();
}

void Ctap2Engine::resetPinRetries() {
    _pinRetries = 8;
    Preferences prefs;
    prefs.begin("fido_vault", false);
    prefs.putUInt("pin_retries", _pinRetries);
    prefs.end();
}

bool Ctap2Engine::isPinValid(const char* candidatePin) const {
    if (!candidatePin) return false;
    return (strcmp(_masterPin, candidatePin) == 0);
}

void Ctap2Engine::handleCborRequest(uint32_t cid, const uint8_t* req, uint16_t reqLen) {
    if (!req || reqLen == 0) {
        Serial.println("[CTAP2] ❌ Error: Empty CBOR request");
        ctapHid.sendError(cid, CTAP1_ERR_INVALID_LENGTH);
        return;
    }

    uint8_t cmd = req[0];
    Serial.printf("[CTAP2] 📥 handleCborRequest cmd=0x%02X reqLen=%u on CID 0x%08X\n", cmd, reqLen, cid);
    CborDecoder dec(req + 1, reqLen - 1);

    switch (cmd) {
        case CTAP2_CMD_GET_INFO:
            handleGetInfo(cid);
            break;
        case CTAP2_CMD_MAKE_CREDENTIAL:
            handleMakeCredential(cid, dec);
            break;
        case CTAP2_CMD_GET_ASSERTION:
            handleGetAssertion(cid, dec);
            break;
        case CTAP2_CMD_CLIENT_PIN:
            handleClientPin(cid, dec);
            break;
        default:
            Serial.printf("[CTAP2] ⚠️ Unsupported cmd=0x%02X\n", cmd);
            uint8_t errResp = CTAP2_ERR_INVALID_CMD;
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &errResp, 1);
            break;
    }
}

void Ctap2Engine::handleGetInfo(uint32_t cid) {
    Serial.printf("[CTAP2] 📋 Executing handleGetInfo on CID 0x%08X\n", cid);
    uint8_t respBuf[512];
    respBuf[0] = CTAP2_OK; // Status byte

    CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
    
    // Map with 8 entries (CTAP 2.1 Standard):
    // 0x01: versions (["FIDO_2_0", "FIDO_2_1", "U2F_V2"])
    // 0x02: extensions (["hmac-secret"])
    // 0x03: aaguid (bytes)
    // 0x04: options (map: rk: true, up: true, plat: false, clientPin: true)
    // 0x05: maxMsgSize (1024)
    // 0x06: pinUvAuthProtocols ([1])
    // 0x07: maxCredentialCountInList (8)
    // 0x0A: algorithms ([{"type": "public-key", "alg": -7}])
    
    enc.encodeMapHeader(8);

    // 0x01: versions
    enc.encodeUnsigned(0x01);
    enc.encodeArrayHeader(3);
    enc.encodeText("FIDO_2_0");
    enc.encodeText("FIDO_2_1");
    enc.encodeText("U2F_V2");

    // 0x02: extensions
    enc.encodeUnsigned(0x02);
    enc.encodeArrayHeader(1);
    enc.encodeText("hmac-secret");

    // 0x03: aaguid
    enc.encodeUnsigned(0x03);
    enc.encodeBytes(_aaguid, 16);

    // 0x04: options
    enc.encodeUnsigned(0x04);
    enc.encodeMapHeader(4);
    enc.encodeText("rk");
    enc.encodeBool(true);
    enc.encodeText("up");
    enc.encodeBool(true);
    enc.encodeText("plat");
    enc.encodeBool(false);
    enc.encodeText("clientPin");
    enc.encodeBool(true);

    // 0x05: maxMsgSize
    enc.encodeUnsigned(0x05);
    enc.encodeUnsigned(1024);

    // 0x06: pinUvAuthProtocols
    enc.encodeUnsigned(0x06);
    enc.encodeArrayHeader(1);
    enc.encodeUnsigned(1);

    // 0x07: maxCredentialCountInList
    enc.encodeUnsigned(0x07);
    enc.encodeUnsigned(8);

    // 0x0A: algorithms (COSE ES256: -7)
    enc.encodeUnsigned(0x0A);
    enc.encodeArrayHeader(1);
    enc.encodeMapHeader(2);
    enc.encodeText("alg");
    enc.encodeInt(-7);
    enc.encodeText("type");
    enc.encodeText("public-key");

    ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
}

void Ctap2Engine::handleCtap1Msg(uint32_t cid, const uint8_t* req, uint16_t reqLen) {
    if (!req || reqLen < 4) {
        uint8_t errResp[] = {0x67, 0x00}; // SW_WRONG_LENGTH
        ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, errResp, 2);
        return;
    }

    uint8_t ins = req[1];
    uint8_t p1  = req[2];

    // Instruction 0x03: U2F_VERSION
    if (ins == 0x03) {
        static const uint8_t U2F_VER_RESP[] = {'U', '2', 'F', '_', 'V', '2', 0x90, 0x00};
        ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, U2F_VER_RESP, sizeof(U2F_VER_RESP));
        return;
    }

    // Payload start and length
    const uint8_t* data = req + 4;
    uint16_t dataLen = reqLen - 4;
    if (dataLen > 0 && req[4] != 0 && reqLen >= 5) {
        uint8_t lc = req[4];
        data = req + 5;
        dataLen = (dataLen - 1 < lc) ? (dataLen - 1) : lc;
    }

    // Instruction 0x02: U2F_AUTHENTICATE
    if (ins == 0x02) {
        if (dataLen < 65) {
            uint8_t errResp[] = {0x67, 0x00};
            ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, errResp, 2);
            return;
        }

        const uint8_t* challenge = data;
        const uint8_t* appId = data + 32;
        uint8_t khLen = data[64];
        if (dataLen < 65 + khLen) {
            uint8_t errResp[] = {0x67, 0x00};
            ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, errResp, 2);
            return;
        }
        const uint8_t* keyHandle = data + 65;

        uint8_t privKey[32];
        bool valid = cryptoP256.verifyCredentialIdRaw(appId, keyHandle, privKey);

        if (!valid) {
            uint8_t errResp[] = {0x6A, 0x80}; // SW_WRONG_DATA
            ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, errResp, 2);
            return;
        }

        // Check-only mode (P1 == 0x07)
        if (p1 == 0x07) {
            mbedtls_platform_zeroize(privKey, sizeof(privKey));
            uint8_t okResp[] = {0x69, 0x85}; // SW_CONDITIONS_NOT_SATISFIED indicates valid key in check-only
            ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, okResp, 2);
            return;
        }

        // Enforce User Presence and Sign (P1 == 0x03)
        if (!_upPrompt || !_upPrompt(cid, "U2F Sign-In", false)) {
            mbedtls_platform_zeroize(privKey, sizeof(privKey));
            uint8_t errResp[] = {0x69, 0x85};
            ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, errResp, 2);
            return;
        }

        uint32_t counter = cryptoP256.incrementSignatureCounter();
        uint8_t sigData[69];
        memcpy(sigData, appId, 32);
        sigData[32] = 0x01; // User presence flag
        sigData[33] = (uint8_t)(counter >> 24);
        sigData[34] = (uint8_t)(counter >> 16);
        sigData[35] = (uint8_t)(counter >> 8);
        sigData[36] = (uint8_t)counter;
        memcpy(sigData + 37, challenge, 32);

        uint8_t sigHash[32];
        CryptoP256::sha256(sigData, 69, sigHash);

        uint8_t derSig[80];
        size_t derSigLen = sizeof(derSig);
        cryptoP256.signDigest(privKey, sigHash, derSig, &derSigLen);
        mbedtls_platform_zeroize(privKey, sizeof(privKey));

        uint8_t resp[128];
        resp[0] = 0x01; // User presence
        resp[1] = (uint8_t)(counter >> 24);
        resp[2] = (uint8_t)(counter >> 16);
        resp[3] = (uint8_t)(counter >> 8);
        resp[4] = (uint8_t)counter;
        memcpy(resp + 5, derSig, derSigLen);
        resp[5 + derSigLen] = 0x90;
        resp[5 + derSigLen + 1] = 0x00;

        ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, resp, 5 + derSigLen + 2);
        return;
    }

    // Instruction 0x01: U2F_REGISTER
    if (ins == 0x01) {
        if (dataLen < 64) {
            uint8_t errResp[] = {0x67, 0x00};
            ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, errResp, 2);
            return;
        }

        const uint8_t* challenge = data;
        const uint8_t* appId = data + 32;

        if (!_upPrompt || !_upPrompt(cid, "U2F Register", true)) {
            uint8_t errResp[] = {0x69, 0x85};
            ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, errResp, 2);
            return;
        }

        uint8_t privKey[32];
        uint8_t credId[32];
        cryptoP256.deriveCredentialKeyRaw(appId, privKey, credId);

        uint8_t pubKeyRaw[64];
        cryptoP256.generateKeypair(privKey, pubKeyRaw);
        mbedtls_platform_zeroize(privKey, sizeof(privKey));

        // Sign attestation data:
        // 0x00 (1) || appId (32) || challenge (32) || keyHandle (32) || pubKey (65)
        uint8_t toSign[1 + 32 + 32 + 32 + 65];
        toSign[0] = 0x00;
        memcpy(toSign + 1, appId, 32);
        memcpy(toSign + 33, challenge, 32);
        memcpy(toSign + 65, credId, 32);
        toSign[97] = 0x04; // Uncompressed EC point tag
        memcpy(toSign + 98, pubKeyRaw, 64);

        uint8_t sigHash[32];
        CryptoP256::sha256(toSign, sizeof(toSign), sigHash);

        uint8_t derSig[80];
        size_t derSigLen = sizeof(derSig);
        cryptoP256.signDigest(U2F_ATTESTATION_PRIVKEY, sigHash, derSig, &derSigLen);

        // Assemble registration response
        uint8_t respBuf[600];
        size_t rOffset = 0;
        respBuf[rOffset++] = 0x05;
        respBuf[rOffset++] = 0x04;
        memcpy(respBuf + rOffset, pubKeyRaw, 64);
        rOffset += 64;
        respBuf[rOffset++] = 32; // Key handle len
        memcpy(respBuf + rOffset, credId, 32);
        rOffset += 32;
        memcpy(respBuf + rOffset, U2F_ATTESTATION_CERT, sizeof(U2F_ATTESTATION_CERT));
        rOffset += sizeof(U2F_ATTESTATION_CERT);
        memcpy(respBuf + rOffset, derSig, derSigLen);
        rOffset += derSigLen;
        respBuf[rOffset++] = 0x90;
        respBuf[rOffset++] = 0x00;

        ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, respBuf, rOffset);
        return;
    }

    // Unsupported instruction
    uint8_t errResp[] = {0x6D, 0x00}; // SW_INS_NOT_SUPPORTED
    ctapHid.sendResponse(cid, CTAPHID_CMD_MSG, errResp, 2);
}

void Ctap2Engine::handleMakeCredential(uint32_t cid, CborDecoder& dec) {
    size_t mapCount = 0;
    if (!dec.readMapHeader(&mapCount)) {
        uint8_t err = CTAP2_ERR_INVALID_PARAM;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    const uint8_t* clientDataHash = nullptr;
    size_t clientDataHashLen = 0;
    char rpId[128] = {0};
    char rpName[128] = {0};
    const uint8_t* userId = nullptr;
    size_t userIdLen = 0;
    const uint8_t* pinUvAuthParam = nullptr;
    size_t pinUvAuthParamLen = 0;
    uint64_t pinUvAuthProtocol = 0;

    for (size_t i = 0; i < mapCount; i++) {
        uint64_t key = 0;
        if (!dec.readUnsigned(&key)) {
            dec.skipValue();
            continue;
        }

        switch (key) {
            case 0x01: // clientDataHash
                dec.readBytes(&clientDataHash, &clientDataHashLen);
                break;
            case 0x02: { // rp (map: id, name)
                size_t rpMap = 0;
                if (dec.readMapHeader(&rpMap)) {
                    for (size_t r = 0; r < rpMap; r++) {
                        char subKey[32] = {0};
                        dec.readText(subKey, sizeof(subKey));
                        if (strcmp(subKey, "id") == 0) {
                            dec.readText(rpId, sizeof(rpId));
                        } else if (strcmp(subKey, "name") == 0) {
                            dec.readText(rpName, sizeof(rpName));
                        } else {
                            dec.skipValue();
                        }
                    }
                }
                break;
            }
            case 0x03: { // user (map: id, name)
                size_t userMap = 0;
                if (dec.readMapHeader(&userMap)) {
                    for (size_t u = 0; u < userMap; u++) {
                        char subKey[32] = {0};
                        dec.readText(subKey, sizeof(subKey));
                        if (strcmp(subKey, "id") == 0) {
                            dec.readBytes(&userId, &userIdLen);
                        } else {
                            dec.skipValue();
                        }
                    }
                }
                break;
            }
            case 0x08: // pinUvAuthParam
                dec.readBytes(&pinUvAuthParam, &pinUvAuthParamLen);
                break;
            case 0x0A: // pinUvAuthProtocol
                dec.readUnsigned(&pinUvAuthProtocol);
                break;
            default:
                dec.skipValue();
                break;
        }
    }

    if (!clientDataHash || clientDataHashLen != 32 || strlen(rpId) == 0) {
        uint8_t err = CTAP2_ERR_INVALID_PARAM;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    // Verify PIN Auth if presented
    bool uvVerified = false;
    if (pinUvAuthParam && pinUvAuthParamLen == 16) {
        if (!_pinTokenValid) {
            uint8_t err = CTAP2_ERR_PIN_AUTH_INVALID;
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
            return;
        }
        uint8_t expAuth[32];
        CryptoP256::hmacSha256(_pinToken, 32, clientDataHash, clientDataHashLen, expAuth);
        if (memcmp(pinUvAuthParam, expAuth, 16) != 0) {
            uint8_t err = CTAP2_ERR_PIN_AUTH_INVALID;
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
            return;
        }
        uvVerified = true;
    }

    // Physical User Presence Verification (UP) - Fail Closed
    if (!_upPrompt || !_upPrompt(cid, rpId, true)) {
        uint8_t err = CTAP2_ERR_OPERATION_DENIED;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    // 1. Derive credential keypair
    uint8_t privKey[32];
    uint8_t credId[32];
    cryptoP256.deriveCredentialKey(rpId, userId, userIdLen, privKey, credId);

    uint8_t pubKeyRaw[64]; // X (32) || Y (32)
    cryptoP256.generateKeypair(privKey, pubKeyRaw);
    mbedtls_platform_zeroize(privKey, sizeof(privKey)); // Clean zeroize immediately after keygen

    // 2. Build COSE Public Key
    uint8_t coseKey[128];
    CborEncoder coseEnc(coseKey, sizeof(coseKey));
    coseEnc.encodeMapHeader(5);
    coseEnc.encodeInt(1);  // kty
    coseEnc.encodeInt(2);  // EC2
    coseEnc.encodeInt(3);  // alg
    coseEnc.encodeInt(-7); // ES256
    coseEnc.encodeInt(-1); // crv
    coseEnc.encodeInt(1);  // P-256
    coseEnc.encodeInt(-2); // x
    coseEnc.encodeBytes(pubKeyRaw, 32);
    coseEnc.encodeInt(-3); // y
    coseEnc.encodeBytes(pubKeyRaw + 32, 32);

    // 3. Construct AuthData: rpIdHash (32) || flags (1) || signCount (4) || AAGUID (16) || credIdLen (2) || credId (32) || coseKey
    uint8_t authData[256];
    size_t adOffset = 0;

    CryptoP256::sha256((const uint8_t*)rpId, strlen(rpId), authData + adOffset);
    adOffset += 32;

    authData[adOffset++] = AUTHDATA_FLAG_UP | (uvVerified ? AUTHDATA_FLAG_UV : 0) | AUTHDATA_FLAG_AT;

    uint32_t counter = cryptoP256.incrementSignatureCounter();
    authData[adOffset++] = (uint8_t)(counter >> 24);
    authData[adOffset++] = (uint8_t)(counter >> 16);
    authData[adOffset++] = (uint8_t)(counter >> 8);
    authData[adOffset++] = (uint8_t)counter;

    // Attested Credential Data
    memcpy(authData + adOffset, _aaguid, 16);
    adOffset += 16;

    authData[adOffset++] = 0x00;
    authData[adOffset++] = 32; // credId len = 32
    memcpy(authData + adOffset, credId, 32);
    adOffset += 32;

    memcpy(authData + adOffset, coseKey, coseEnc.getLength());
    adOffset += coseEnc.getLength();

    // 4. Assemble CBOR Response
    // 0x01: fmt ("packed" or "none")
    // 0x02: authData (bytes)
    // 0x03: attStmt (empty map for "none")
    uint8_t respBuf[512];
    respBuf[0] = CTAP2_OK;

    CborEncoder respEnc(respBuf + 1, sizeof(respBuf) - 1);
    respEnc.encodeMapHeader(3);
    respEnc.encodeUnsigned(0x01);
    respEnc.encodeText("none");
    respEnc.encodeUnsigned(0x02);
    respEnc.encodeBytes(authData, adOffset);
    respEnc.encodeUnsigned(0x03);
    respEnc.encodeMapHeader(0); // empty attStmt

    ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + respEnc.getLength());
}

void Ctap2Engine::handleGetAssertion(uint32_t cid, CborDecoder& dec) {
    size_t mapCount = 0;
    if (!dec.readMapHeader(&mapCount)) {
        uint8_t err = CTAP2_ERR_INVALID_PARAM;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    char rpId[128] = {0};
    const uint8_t* clientDataHash = nullptr;
    size_t clientDataHashLen = 0;
    const uint8_t* targetCredId = nullptr;
    size_t targetCredIdLen = 0;
    const uint8_t* pinUvAuthParam = nullptr;
    size_t pinUvAuthParamLen = 0;
    uint64_t pinUvAuthProtocol = 0;

    for (size_t i = 0; i < mapCount; i++) {
        uint64_t key = 0;
        if (!dec.readUnsigned(&key)) {
            dec.skipValue();
            continue;
        }

        switch (key) {
            case 0x01: // rpId
                dec.readText(rpId, sizeof(rpId));
                break;
            case 0x02: // clientDataHash
                dec.readBytes(&clientDataHash, &clientDataHashLen);
                break;
            case 0x03: { // allowList (array of credential descriptors)
                size_t arrLen = 0;
                if (dec.readArrayHeader(&arrLen)) {
                    for (size_t a = 0; a < arrLen; a++) {
                        size_t credMap = 0;
                        if (dec.readMapHeader(&credMap)) {
                            for (size_t cm = 0; cm < credMap; cm++) {
                                char credKey[32] = {0};
                                dec.readText(credKey, sizeof(credKey));
                                if (strcmp(credKey, "id") == 0) {
                                    dec.readBytes(&targetCredId, &targetCredIdLen);
                                } else {
                                    dec.skipValue();
                                }
                            }
                        }
                    }
                }
                break;
            }
            case 0x06: // pinUvAuthParam
                dec.readBytes(&pinUvAuthParam, &pinUvAuthParamLen);
                break;
            case 0x07: // pinUvAuthProtocol
                dec.readUnsigned(&pinUvAuthProtocol);
                break;
            default:
                dec.skipValue();
                break;
        }
    }

    if (!clientDataHash || clientDataHashLen != 32 || strlen(rpId) == 0) {
        uint8_t err = CTAP2_ERR_INVALID_PARAM;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    // Verify PIN Auth if presented
    bool uvVerified = false;
    if (pinUvAuthParam && pinUvAuthParamLen == 16) {
        if (!_pinTokenValid) {
            uint8_t err = CTAP2_ERR_PIN_AUTH_INVALID;
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
            return;
        }
        uint8_t expAuth[32];
        CryptoP256::hmacSha256(_pinToken, 32, clientDataHash, clientDataHashLen, expAuth);
        if (memcmp(pinUvAuthParam, expAuth, 16) != 0) {
            uint8_t err = CTAP2_ERR_PIN_AUTH_INVALID;
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
            return;
        }
        uvVerified = true;
    }

    // Verify and recover private key from credential ID
    uint8_t privKey[32];
    if (targetCredId && targetCredIdLen == 32) {
        if (!cryptoP256.verifyCredentialId(rpId, targetCredId, privKey)) {
            uint8_t err = CTAP2_ERR_KEY_NOT_FOUND;
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
            return;
        }
    } else {
        // Resident key lookup or non-resident default
        uint8_t dummyUserId[] = "user";
        uint8_t dummyCredId[32];
        cryptoP256.deriveCredentialKey(rpId, dummyUserId, 4, privKey, dummyCredId);
    }

    // Physical User Presence Verification (UP) - Fail Closed
    if (!_upPrompt || !_upPrompt(cid, rpId, false)) {
        mbedtls_platform_zeroize(privKey, sizeof(privKey));
        uint8_t err = CTAP2_ERR_OPERATION_DENIED;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    // 1. Build AuthData: rpIdHash (32) || flags (1) || signCount (4)
    uint8_t authData[37];
    CryptoP256::sha256((const uint8_t*)rpId, strlen(rpId), authData);
    authData[32] = AUTHDATA_FLAG_UP | (uvVerified ? AUTHDATA_FLAG_UV : 0);

    uint32_t counter = cryptoP256.incrementSignatureCounter();
    authData[33] = (uint8_t)(counter >> 24);
    authData[34] = (uint8_t)(counter >> 16);
    authData[35] = (uint8_t)(counter >> 8);
    authData[36] = (uint8_t)counter;

    // 2. Compute Signature over (authData || clientDataHash)
    uint8_t signInput[37 + 32];
    memcpy(signInput, authData, 37);
    memcpy(signInput + 37, clientDataHash, 32);

    uint8_t digest[32];
    CryptoP256::sha256(signInput, sizeof(signInput), digest);

    uint8_t sigDer[72];
    size_t sigLen = 0;
    bool signOk = cryptoP256.signDigest(privKey, digest, sigDer, &sigLen);
    mbedtls_platform_zeroize(privKey, sizeof(privKey)); // Clean zeroize private key immediately after signing

    if (!signOk) {
        uint8_t err = CTAP2_ERR_OTHER;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    // 3. Assemble CBOR Response
    // 0x02: authData (bytes)
    // 0x03: signature (bytes)
    uint8_t respBuf[512];
    respBuf[0] = CTAP2_OK;

    CborEncoder respEnc(respBuf + 1, sizeof(respBuf) - 1);
    respEnc.encodeMapHeader(2);
    respEnc.encodeUnsigned(0x02);
    respEnc.encodeBytes(authData, 37);
    respEnc.encodeUnsigned(0x03);
    respEnc.encodeBytes(sigDer, sigLen);

    ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + respEnc.getLength());
}

void Ctap2Engine::handleClientPin(uint32_t cid, CborDecoder& dec) {
    size_t mapCount = 0;
    if (!dec.readMapHeader(&mapCount)) {
        uint8_t err = CTAP2_ERR_INVALID_PARAM;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    uint64_t pinUvAuthProtocol = 0;
    uint64_t subCommand = 0;
    const uint8_t* pinUvAuthParam = nullptr;
    size_t pinUvAuthParamLen = 0;
    const uint8_t* newPinEnc = nullptr;
    size_t newPinEncLen = 0;
    const uint8_t* pinHashEnc = nullptr;
    size_t pinHashEncLen = 0;
    uint8_t peerPubRaw[64] = {0};
    bool hasPeerKey = false;

    for (size_t i = 0; i < mapCount; i++) {
        uint64_t key = 0;
        if (!dec.readUnsigned(&key)) {
            dec.skipValue();
            continue;
        }

        switch (key) {
            case 0x01: // pinUvAuthProtocol
                dec.readUnsigned(&pinUvAuthProtocol);
                break;
            case 0x02: // subCommand
                dec.readUnsigned(&subCommand);
                break;
            case 0x03: { // keyAgreement (COSE Key)
                size_t coseMap = 0;
                if (dec.readMapHeader(&coseMap)) {
                    bool haveX = false, haveY = false;
                    for (size_t c = 0; c < coseMap; c++) {
                        int64_t coseKey = 0;
                        if (!dec.readInt(&coseKey)) {
                            dec.skipValue();
                            continue;
                        }
                        if (coseKey == -2) { // x coordinate
                            const uint8_t* xPtr = nullptr;
                            size_t xLen = 0;
                            if (dec.readBytes(&xPtr, &xLen) && xLen == 32) {
                                memcpy(peerPubRaw, xPtr, 32);
                                haveX = true;
                            }
                        } else if (coseKey == -3) { // y coordinate
                            const uint8_t* yPtr = nullptr;
                            size_t yLen = 0;
                            if (dec.readBytes(&yPtr, &yLen) && yLen == 32) {
                                memcpy(peerPubRaw + 32, yPtr, 32);
                                haveY = true;
                            }
                        } else {
                            dec.skipValue();
                        }
                    }
                    hasPeerKey = (haveX && haveY);
                }
                break;
            }
            case 0x04: // pinUvAuthParam
                dec.readBytes(&pinUvAuthParam, &pinUvAuthParamLen);
                break;
            case 0x05: // newPinEnc
                dec.readBytes(&newPinEnc, &newPinEncLen);
                break;
            case 0x06: // pinHashEnc
                dec.readBytes(&pinHashEnc, &pinHashEncLen);
                break;
            default:
                dec.skipValue();
                break;
        }
    }

    if (pinUvAuthProtocol != 0 && pinUvAuthProtocol != 1) {
        uint8_t err = CTAP2_ERR_UNSUPPORTED_OPTION;
        ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
        return;
    }

    switch (subCommand) {
        case CTAP2_PIN_SUBCMD_GET_PIN_RETRIES: {
            uint8_t respBuf[64];
            respBuf[0] = CTAP2_OK;
            CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
            enc.encodeMapHeader(2);
            enc.encodeUnsigned(0x03); // pinRetries
            enc.encodeUnsigned(_pinRetries);
            enc.encodeUnsigned(0x05); // powerCycleState
            enc.encodeBool(false);
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
            break;
        }

        case CTAP2_PIN_SUBCMD_GET_KEY_AGREEMENT: {
            cryptoP256.getRandomBytes(_ephemPrivKey, 32);
            cryptoP256.generateKeypair(_ephemPrivKey, _ephemPubKeyRaw);
            _hasEphemKey = true;

            uint8_t respBuf[256];
            respBuf[0] = CTAP2_OK;
            CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
            enc.encodeMapHeader(1);
            enc.encodeUnsigned(0x01); // keyAgreement
            enc.encodeMapHeader(5);
            enc.encodeInt(1);  // kty
            enc.encodeInt(2);  // EC2
            enc.encodeInt(3);  // alg
            enc.encodeInt(-7); // ES256
            enc.encodeInt(-1); // crv
            enc.encodeInt(1);  // P-256
            enc.encodeInt(-2); // x
            enc.encodeBytes(_ephemPubKeyRaw, 32);
            enc.encodeInt(-3); // y
            enc.encodeBytes(_ephemPubKeyRaw + 32, 32);

            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
            break;
        }

        case CTAP2_PIN_SUBCMD_GET_PIN_TOKEN: {
            if (!_hasEphemKey || !hasPeerKey || !pinHashEnc || pinHashEncLen != 16) {
                uint8_t err = CTAP2_ERR_INVALID_PARAM;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
                return;
            }

            if (_pinRetries == 0) {
                uint8_t err = CTAP2_ERR_PIN_BLOCKED;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
                return;
            }

            uint8_t sharedKey[32];
            if (!cryptoP256.computeSharedSecretP256(_ephemPrivKey, peerPubRaw, sharedKey)) {
                uint8_t err = CTAP2_ERR_OTHER;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
                return;
            }

            uint8_t candHash[16];
            CryptoP256::aes256CbcDecrypt(sharedKey, NULL, pinHashEnc, 16, candHash);

            uint8_t fullHash[32];
            CryptoP256::sha256((const uint8_t*)_masterPin, strlen(_masterPin), fullHash);

            uint8_t diff = 0;
            for (int i = 0; i < 16; i++) {
                diff |= (candHash[i] ^ fullHash[i]);
            }

            if (diff == 0) {
                resetPinRetries();
                cryptoP256.getRandomBytes(_pinToken, 32);
                _pinTokenValid = true;

                uint8_t encToken[32];
                CryptoP256::aes256CbcEncrypt(sharedKey, NULL, _pinToken, 32, encToken);
                mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));

                uint8_t respBuf[128];
                respBuf[0] = CTAP2_OK;
                CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
                enc.encodeMapHeader(1);
                enc.encodeUnsigned(0x02); // pinToken
                enc.encodeBytes(encToken, 32);
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
            } else {
                mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));
                if (_pinRetries > 0) _pinRetries--;
                Preferences prefs;
                prefs.begin("fido_vault", false);
                prefs.putUInt("pin_retries", _pinRetries);
                prefs.end();

                uint8_t err = (_pinRetries == 0) ? CTAP2_ERR_PIN_BLOCKED : CTAP2_ERR_PIN_INVALID;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
            }
            break;
        }

        case CTAP2_PIN_SUBCMD_CHANGE_PIN: {
            if (!_hasEphemKey || !hasPeerKey || !newPinEnc || newPinEncLen != 64 ||
                !pinHashEnc || pinHashEncLen != 16 || !pinUvAuthParam || pinUvAuthParamLen != 16) {
                uint8_t err = CTAP2_ERR_INVALID_PARAM;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
                return;
            }

            uint8_t sharedKey[32];
            if (!cryptoP256.computeSharedSecretP256(_ephemPrivKey, peerPubRaw, sharedKey)) {
                uint8_t err = CTAP2_ERR_OTHER;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
                return;
            }

            uint8_t expAuth[32];
            CryptoP256::hmacSha256(sharedKey, 32, newPinEnc, 64, expAuth);
            if (memcmp(pinUvAuthParam, expAuth, 16) != 0) {
                mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));
                uint8_t err = CTAP2_ERR_PIN_AUTH_INVALID;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
                return;
            }

            uint8_t candHash[16];
            CryptoP256::aes256CbcDecrypt(sharedKey, NULL, pinHashEnc, 16, candHash);
            uint8_t fullHash[32];
            CryptoP256::sha256((const uint8_t*)_masterPin, strlen(_masterPin), fullHash);
            if (memcmp(candHash, fullHash, 16) != 0) {
                mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));
                if (_pinRetries > 0) _pinRetries--;
                uint8_t err = (_pinRetries == 0) ? CTAP2_ERR_PIN_BLOCKED : CTAP2_ERR_PIN_INVALID;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
                return;
            }

            uint8_t decPin[64];
            CryptoP256::aes256CbcDecrypt(sharedKey, NULL, newPinEnc, 64, decPin);
            mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));

            char newPinStr[16] = {0};
            int pLen = 0;
            while (pLen < 8 && decPin[pLen] >= '0' && decPin[pLen] <= '9') {
                newPinStr[pLen] = (char)decPin[pLen];
                pLen++;
            }
            mbedtls_platform_zeroize(decPin, sizeof(decPin));

            if (pLen < 4 || pLen > 8) {
                uint8_t err = CTAP2_ERR_PIN_POLICY_VIOLATION;
                ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
                return;
            }

            setMasterPin(newPinStr);
            resetPinRetries();

            uint8_t respBuf[16];
            respBuf[0] = CTAP2_OK;
            CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
            enc.encodeMapHeader(0);
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
            break;
        }

        default: {
            uint8_t err = CTAP2_ERR_UNSUPPORTED_OPTION;
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &err, 1);
            break;
        }
    }
}
