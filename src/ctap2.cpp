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

Ctap2Engine::Ctap2Engine() : _upPrompt(nullptr) {
    memcpy(_aaguid, TKEY_AAGUID, 16);
}

void Ctap2Engine::begin() {
    cryptoP256.begin();
    ctapHid.setCborHandler([](uint32_t cid, const uint8_t* req, uint16_t reqLen) {
        ctap2Engine.handleCborRequest(cid, req, reqLen);
    });
    ctapHid.setMsgHandler([](uint32_t cid, const uint8_t* req, uint16_t reqLen) {
        ctap2Engine.handleCtap1Msg(cid, req, reqLen);
    });
}

void Ctap2Engine::handleCborRequest(uint32_t cid, const uint8_t* req, uint16_t reqLen) {
    if (!req || reqLen == 0) {
        ctapHid.sendError(cid, CTAP1_ERR_INVALID_LENGTH);
        return;
    }

    uint8_t cmd = req[0];
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
        default:
            uint8_t errResp = CTAP2_ERR_INVALID_CMD;
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &errResp, 1);
            break;
    }
}

void Ctap2Engine::handleGetInfo(uint32_t cid) {
    uint8_t respBuf[512];
    respBuf[0] = CTAP2_OK; // Status byte

    CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
    
    // Map with 6 entries (CTAP 2.1 Standard):
    // 0x01: versions (["FIDO_2_0", "FIDO_2_1", "U2F_V2"])
    // 0x02: extensions (["hmac-secret"])
    // 0x03: aaguid (bytes)
    // 0x04: options (map: rk: true, up: true, plat: false)
    // 0x05: maxMsgSize (1024)
    // 0x07: maxCredentialCountInList (8)
    
    enc.encodeMapHeader(6);

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
    enc.encodeMapHeader(3);
    enc.encodeText("rk");
    enc.encodeBool(true);
    enc.encodeText("up");
    enc.encodeBool(true);
    enc.encodeText("plat");
    enc.encodeBool(false);

    // 0x05: maxMsgSize
    enc.encodeUnsigned(0x05);
    enc.encodeUnsigned(1024);

    // 0x07: maxCredentialCountInList
    enc.encodeUnsigned(0x07);
    enc.encodeUnsigned(8);

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

    authData[adOffset++] = AUTHDATA_FLAG_UP | AUTHDATA_FLAG_AT; // UP=1, AT=1

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
    authData[32] = AUTHDATA_FLAG_UP; // UP=1

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
