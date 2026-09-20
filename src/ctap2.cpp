#include "ctap2.h"
#include "ctaphid.h"
#include <string.h>

Ctap2Engine ctap2Engine;

// Custom AAGUID for Crypto TKey S3 (UUID v4)
static const uint8_t TKEY_AAGUID[16] = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x40, 0x00,
    0x80, 0x00, 0x74, 0x6b, 0x65, 0x79, 0x73, 0x33
};

Ctap2Engine::Ctap2Engine() : _upPrompt(nullptr) {
    memcpy(_aaguid, TKEY_AAGUID, 16);
}

void Ctap2Engine::begin() {
    cryptoP256.begin();
    ctapHid.setCborHandler([](uint32_t cid, const uint8_t* req, uint16_t reqLen) {
        ctap2Engine.handleCborRequest(cid, req, reqLen);
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
    
    // Map with 6 entries:
    // 0x01: versions (["FIDO_2_0"])
    // 0x03: aaguid (bytes)
    // 0x04: options (map: rk: true, up: true, plat: false)
    // 0x05: maxMsgSize (1024)
    // 0x06: pinProtocols ([1])
    // 0x07: maxCredentialCountInList (8)
    
    enc.encodeMapHeader(5);

    // 0x01: versions
    enc.encodeUnsigned(0x01);
    enc.encodeArrayHeader(1);
    enc.encodeText("FIDO_2_0");

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
    if (!_upPrompt || !_upPrompt(rpId, true)) {
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
    if (!_upPrompt || !_upPrompt(rpId, false)) {
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
    if (!cryptoP256.signDigest(privKey, digest, sigDer, &sigLen)) {
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
