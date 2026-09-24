#include "crypto_p256.h"
#include "ctap2.h"
#include "ctaphid.h"
#include <string.h>
#include <mbedtls/platform_util.h>
#include <Preferences.h>

Ctap2Engine ctap2Engine;

// Custom AAGUID for Crypto TKey S3 (UUID v4)
static const uint8_t TKEY_AAGUID[16] = {
    0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x40, 0x00,
    0x80, 0x00, 0x74, 0x6b, 0x65, 0x79, 0x73, 0x33
};

// "packed" / U2F batch attestation. Self-signed X.509 v3 certificate meeting the WebAuthn
// packed attestation requirements: C, O, OU="Authenticator Attestation", CN, CA:FALSE and
// the id-fido-gen-ce-aaguid (1.3.6.1.4.1.45724.1.1.4) extension carrying TKEY_AAGUID.
static const uint8_t FIDO_ATTESTATION_CERT[566] = {
    0x30, 0x82, 0x02, 0x32, 0x30, 0x82, 0x01, 0xD7, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x14, 0x6A,
    0x86, 0x83, 0xB6, 0x35, 0x26, 0x47, 0xF1, 0x3E, 0x38, 0xCF, 0xC8, 0xFB, 0xCC, 0x05, 0x0C, 0x8B,
    0x86, 0xC1, 0x52, 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02, 0x30,
    0x7E, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x20,
    0x30, 0x1E, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x0C, 0x17, 0x52, 0x65, 0x61, 0x70, 0x65, 0x72, 0x20,
    0x53, 0x65, 0x63, 0x75, 0x72, 0x69, 0x74, 0x79, 0x20, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x73,
    0x31, 0x22, 0x30, 0x20, 0x06, 0x03, 0x55, 0x04, 0x0B, 0x0C, 0x19, 0x41, 0x75, 0x74, 0x68, 0x65,
    0x6E, 0x74, 0x69, 0x63, 0x61, 0x74, 0x6F, 0x72, 0x20, 0x41, 0x74, 0x74, 0x65, 0x73, 0x74, 0x61,
    0x74, 0x69, 0x6F, 0x6E, 0x31, 0x29, 0x30, 0x27, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0C, 0x20, 0x43,
    0x72, 0x79, 0x70, 0x74, 0x6F, 0x20, 0x54, 0x4B, 0x65, 0x79, 0x20, 0x53, 0x33, 0x20, 0x46, 0x49,
    0x44, 0x4F, 0x32, 0x20, 0x41, 0x74, 0x74, 0x65, 0x73, 0x74, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x30,
    0x1E, 0x17, 0x0D, 0x32, 0x36, 0x30, 0x31, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5A,
    0x17, 0x0D, 0x34, 0x36, 0x30, 0x31, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5A, 0x30,
    0x7E, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x20,
    0x30, 0x1E, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x0C, 0x17, 0x52, 0x65, 0x61, 0x70, 0x65, 0x72, 0x20,
    0x53, 0x65, 0x63, 0x75, 0x72, 0x69, 0x74, 0x79, 0x20, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x73,
    0x31, 0x22, 0x30, 0x20, 0x06, 0x03, 0x55, 0x04, 0x0B, 0x0C, 0x19, 0x41, 0x75, 0x74, 0x68, 0x65,
    0x6E, 0x74, 0x69, 0x63, 0x61, 0x74, 0x6F, 0x72, 0x20, 0x41, 0x74, 0x74, 0x65, 0x73, 0x74, 0x61,
    0x74, 0x69, 0x6F, 0x6E, 0x31, 0x29, 0x30, 0x27, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0C, 0x20, 0x43,
    0x72, 0x79, 0x70, 0x74, 0x6F, 0x20, 0x54, 0x4B, 0x65, 0x79, 0x20, 0x53, 0x33, 0x20, 0x46, 0x49,
    0x44, 0x4F, 0x32, 0x20, 0x41, 0x74, 0x74, 0x65, 0x73, 0x74, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x30,
    0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A, 0x86,
    0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0xDD, 0xA7, 0x2E, 0xAD, 0xD6, 0x93,
    0x20, 0xB5, 0xA7, 0x03, 0xB4, 0xD5, 0xC2, 0x5B, 0xA1, 0x17, 0xA4, 0x9C, 0x00, 0xDE, 0x5A, 0xD7,
    0xE1, 0xFA, 0x16, 0xD7, 0x49, 0xD8, 0x5C, 0x0C, 0xB0, 0x1F, 0x2D, 0xAE, 0xFB, 0xA8, 0x5F, 0x9D,
    0x92, 0xD0, 0xD1, 0xC8, 0xC5, 0xDA, 0xB4, 0xB7, 0x42, 0x8E, 0x95, 0xE5, 0xE1, 0x00, 0xD4, 0xA9,
    0xF8, 0x4A, 0x25, 0x66, 0xDA, 0x65, 0x75, 0xB3, 0x91, 0xD3, 0xA3, 0x33, 0x30, 0x31, 0x30, 0x0C,
    0x06, 0x03, 0x55, 0x1D, 0x13, 0x01, 0x01, 0xFF, 0x04, 0x02, 0x30, 0x00, 0x30, 0x21, 0x06, 0x0B,
    0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0xE5, 0x1C, 0x01, 0x01, 0x04, 0x04, 0x12, 0x04, 0x10, 0xDE,
    0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0x40, 0x00, 0x80, 0x00, 0x74, 0x6B, 0x65, 0x79, 0x73, 0x33, 0x30,
    0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02, 0x03, 0x49, 0x00, 0x30, 0x46,
    0x02, 0x21, 0x00, 0xDE, 0xEC, 0x67, 0x6D, 0x53, 0xEC, 0x25, 0xB6, 0x4D, 0xCB, 0xE3, 0xC6, 0xC2,
    0xA5, 0x90, 0xDC, 0x46, 0x74, 0x72, 0x2E, 0x73, 0xAD, 0xDA, 0x11, 0x27, 0xB5, 0xEF, 0x69, 0x5A,
    0x29, 0x6B, 0x62, 0x02, 0x21, 0x00, 0xC6, 0xAF, 0x2F, 0xD2, 0xEA, 0x4E, 0x82, 0xD0, 0x4C, 0xBF,
    0xA8, 0x45, 0x0D, 0x7C, 0xEF, 0xC4, 0x63, 0x4F, 0xCD, 0xEE, 0x56, 0xEE, 0xB1, 0x0D, 0xC7, 0xA1,
    0x4E, 0x76, 0x4B, 0x41, 0x1A, 0xFC
};

static const uint8_t FIDO_ATTESTATION_PRIVKEY[32] = {
    0x7A, 0xD1, 0xD6, 0x3C, 0x9F, 0x41, 0xA1, 0x29, 0x04, 0x04, 0xF7, 0xEC, 0x8C, 0x9B, 0x82, 0xA8,
    0x93, 0x80, 0xD1, 0x30, 0x5C, 0xFF, 0x95, 0xAE, 0x4A, 0xDF, 0x9C, 0xDF, 0xD3, 0x13, 0xC6, 0x7D
};
#define FIDO_MAX_LIST        16     // credential IDs examined per allowList / excludeList
#define FIDO_MAX_MSG_SIZE    2048
#define FIDO_UP_TIMEOUT_MS   30000
#define FIDO_RESET_WINDOW_MS 10000  // authenticatorReset only within 10 s of the key becoming ready

static void sendStatus(uint32_t cid, uint8_t status) {
    ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, &status, 1);
}

// Reads a text map key. Any other key type: skips key and value, returns false.
static bool readTextKey(CborDecoder& dec, char* out, size_t outLen) {
    uint8_t mt = 0;
    if (dec.peekType(&mt) && mt == 3 && dec.readText(out, outLen)) return true;
    dec.skipValue();
    dec.skipValue();
    return false;
}

// Parses [{id: bytes, type: text, ...}], keeping pointers to 32-byte IDs (ours are always 32).
static bool parseCredList(CborDecoder& dec, const uint8_t** ids, uint8_t* count, size_t* total) {
    size_t n = 0;
    if (!dec.readArrayHeader(&n)) return false;
    *total = n;
    for (size_t i = 0; i < n; i++) {
        size_t m = 0;
        if (!dec.readMapHeader(&m)) return false;
        for (size_t j = 0; j < m; j++) {
            char k[16] = {0};
            if (!readTextKey(dec, k, sizeof(k))) continue;
            if (strcmp(k, "id") == 0) {
                const uint8_t* p = nullptr;
                size_t len = 0;
                if (!dec.readBytes(&p, &len)) return false;
                if (len == 32 && *count < FIDO_MAX_LIST) ids[(*count)++] = p;
            } else if (!dec.skipValue()) {
                return false;
            }
        }
    }
    return true;
}

struct CtapOptions {
    int8_t rk = -1, up = -1, uv = -1;  // -1 = absent
};

static bool parseOptions(CborDecoder& dec, CtapOptions& o) {
    size_t m = 0;
    if (!dec.readMapHeader(&m)) return false;
    for (size_t i = 0; i < m; i++) {
        char k[16] = {0};
        if (!readTextKey(dec, k, sizeof(k))) continue;
        bool v = false;
        if (!dec.readBool(&v)) return false;
        if (strcmp(k, "rk") == 0) o.rk = v;
        else if (strcmp(k, "up") == 0) o.up = v;
        else if (strcmp(k, "uv") == 0) o.uv = v;
    }
    return true;
}

static bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

Ctap2Engine::Ctap2Engine()
    : _upPrompt(nullptr), _readyAt(0), _pinSet(false), _pinRetries(FIDO_PIN_MAX_RETRIES), _consecutivePinFails(0),
      _hasEphemKey(false), _pinTokenValid(false) {
    memcpy(_aaguid, TKEY_AAGUID, 16);
    mbedtls_platform_zeroize(_pinHash, sizeof(_pinHash));
    mbedtls_platform_zeroize(_ephemPrivKey, sizeof(_ephemPrivKey));
    mbedtls_platform_zeroize(_ephemPubKeyRaw, sizeof(_ephemPubKeyRaw));
    mbedtls_platform_zeroize(_pinToken, sizeof(_pinToken));
    memset(&_pending, 0, sizeof(_pending));
}

void Ctap2Engine::begin() {
    cryptoP256.begin();
    fidoStore.begin();

    Preferences prefs;
    prefs.begin("fido_vault", false);
    _pinRetries = (uint8_t)prefs.getUInt("pin_retries", FIDO_PIN_MAX_RETRIES);
    _pinSet = prefs.getBytesLength("pin_hash") == sizeof(_pinHash) &&
              prefs.getBytes("pin_hash", _pinHash, sizeof(_pinHash)) == sizeof(_pinHash);
    prefs.end();

    ctapHid.setCborHandler([](uint32_t cid, const uint8_t* req, uint16_t reqLen) {
        ctap2Engine.handleCborRequest(cid, req, reqLen);
    });
    ctapHid.setMsgHandler([](uint32_t cid, const uint8_t* req, uint16_t reqLen) {
        ctap2Engine.handleCtap1Msg(cid, req, reqLen);
    });
    Serial.printf("[CTAP2] Ready: %u passkey(s), FIDO PIN %s\n", fidoStore.count(), _pinSet ? "set" : "not set");
}

void Ctap2Engine::setPinRetries(uint8_t retries) {
    _pinRetries = retries;
    Preferences prefs;
    prefs.begin("fido_vault", false);
    prefs.putUInt("pin_retries", _pinRetries);
    prefs.end();
}

void Ctap2Engine::resetPinRetries() {
    _consecutivePinFails = 0;
    setPinRetries(FIDO_PIN_MAX_RETRIES);
}

void Ctap2Engine::storePinHash(const uint8_t* hash16) {
    memcpy(_pinHash, hash16, sizeof(_pinHash));
    _pinSet = true;
    Preferences prefs;
    prefs.begin("fido_vault", false);
    prefs.putBytes("pin_hash", _pinHash, sizeof(_pinHash));
    prefs.end();
}

// Wrong PIN: rotate the key agreement key and report the right error.
uint8_t Ctap2Engine::pinFailure() {
    _hasEphemKey = false;
    mbedtls_platform_zeroize(_ephemPrivKey, sizeof(_ephemPrivKey));
    _consecutivePinFails++;
    if (_pinRetries == 0) return CTAP2_ERR_PIN_BLOCKED;
    if (_consecutivePinFails >= 3) return CTAP2_ERR_PIN_AUTH_BLOCKED;
    return CTAP2_ERR_PIN_INVALID;
}

// Blocks on the device button. On refusal, sends the matching CTAP2 error and returns false.
bool Ctap2Engine::waitForUser(uint32_t cid, const char* label, bool isRegistration) {
    uint32_t start = millis();
    if (_upPrompt && _upPrompt(cid, label, isRegistration)) return true;
    if (ctapHid.isCancelRequested()) sendStatus(cid, CTAP2_ERR_KEEPALIVE_CANCEL);
    else if (millis() - start >= FIDO_UP_TIMEOUT_MS - 500) sendStatus(cid, CTAP2_ERR_USER_ACTION_TIMEOUT);
    else sendStatus(cid, CTAP2_ERR_OPERATION_DENIED);
    return false;
}

// An empty pinUvAuthParam is the client's "touch the key you want to use" probe.
void Ctap2Engine::answerSelectionProbe(uint32_t cid, const char* rpId) {
    if (!waitForUser(cid, rpId, false)) return;
    sendStatus(cid, _pinSet ? CTAP2_ERR_PIN_INVALID : CTAP2_ERR_PIN_NOT_SET);
}

bool Ctap2Engine::checkPinAuth(const uint8_t* pinAuth, size_t len, const uint8_t* clientDataHash) {
    if (!_pinTokenValid || len != 16) return false;
    uint8_t expected[32];
    CryptoP256::hmacSha256(_pinToken, 32, clientDataHash, 32, expected);
    bool ok = constantTimeEqual(pinAuth, expected, 16);
    mbedtls_platform_zeroize(expected, sizeof(expected));
    return ok;
}

void Ctap2Engine::handleCborRequest(uint32_t cid, const uint8_t* req, uint16_t reqLen) {
    if (!req || reqLen == 0) {
        ctapHid.sendError(cid, CTAP1_ERR_INVALID_LENGTH);
        return;
    }

    uint8_t cmd = req[0];
    Serial.printf("[CTAP2] cmd=0x%02X len=%u cid=0x%08X\n", cmd, reqLen, cid);
    CborDecoder dec(req + 1, reqLen - 1);
    if (cmd != CTAP2_CMD_GET_NEXT_ASSERTION) _pending.active = false;

    switch (cmd) {
        case CTAP2_CMD_GET_INFO:            handleGetInfo(cid); break;
        case CTAP2_CMD_MAKE_CREDENTIAL:     handleMakeCredential(cid, dec); break;
        case CTAP2_CMD_GET_ASSERTION:       handleGetAssertion(cid, dec); break;
        case CTAP2_CMD_GET_NEXT_ASSERTION:  handleGetNextAssertion(cid); break;
        case CTAP2_CMD_CLIENT_PIN:          handleClientPin(cid, dec); break;
        case CTAP2_CMD_RESET:               handleReset(cid); break;
        default:                            sendStatus(cid, CTAP2_ERR_INVALID_CMD); break;
    }
}

void Ctap2Engine::handleGetInfo(uint32_t cid) {
    uint8_t respBuf[256];
    respBuf[0] = CTAP2_OK;
    CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);

    enc.encodeMapHeader(8);

    enc.encodeUnsigned(0x01);  // versions
    enc.encodeArrayHeader(2);
    enc.encodeText("U2F_V2");
    enc.encodeText("FIDO_2_0");

    enc.encodeUnsigned(0x02);  // extensions (LUKS / systemd-cryptenroll, KeePassXC, PRF)
    enc.encodeArrayHeader(1);
    enc.encodeText("hmac-secret");

    enc.encodeUnsigned(0x03);  // aaguid
    enc.encodeBytes(_aaguid, 16);

    enc.encodeUnsigned(0x04);  // options (canonical key order)
    enc.encodeMapHeader(4);
    enc.encodeText("rk");
    enc.encodeBool(true);
    enc.encodeText("up");
    enc.encodeBool(true);
    enc.encodeText("plat");
    enc.encodeBool(false);
    enc.encodeText("clientPin");
    enc.encodeBool(_pinSet);

    enc.encodeUnsigned(0x05);  // maxMsgSize
    enc.encodeUnsigned(FIDO_MAX_MSG_SIZE);

    enc.encodeUnsigned(0x06);  // pinUvAuthProtocols
    enc.encodeArrayHeader(1);
    enc.encodeUnsigned(1);

    enc.encodeUnsigned(0x07);  // maxCredentialCountInList
    enc.encodeUnsigned(FIDO_MAX_LIST);

    enc.encodeUnsigned(0x0A);  // algorithms
    enc.encodeArrayHeader(1);
    enc.encodeMapHeader(2);
    enc.encodeText("alg");
    enc.encodeInt(-7);
    enc.encodeText("type");
    enc.encodeText("public-key");

    ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
}

void Ctap2Engine::handleMakeCredential(uint32_t cid, CborDecoder& dec) {
    size_t mapCount = 0;
    if (!dec.readMapHeader(&mapCount)) return sendStatus(cid, CTAP2_ERR_INVALID_CBOR);

    const uint8_t* clientDataHash = nullptr;
    size_t clientDataHashLen = 0;
    char rpId[128] = {0};
    const uint8_t* userId = nullptr;
    size_t userIdLen = 0;
    char userName[FIDO_RK_NAME_LEN] = {0};
    char displayName[FIDO_RK_NAME_LEN] = {0};
    bool haveRp = false, haveUser = false, haveParams = false, es256 = false;
    const uint8_t* exclude[FIDO_MAX_LIST];
    uint8_t excludeCount = 0;
    size_t excludeTotal = 0;
    CtapOptions opts;
    const uint8_t* pinAuth = nullptr;
    size_t pinAuthLen = 0;
    bool havePinAuth = false;
    uint64_t pinProtocol = 0;
    bool wantHmacSecret = false;

    for (size_t i = 0; i < mapCount; i++) {
        uint64_t key = 0;
        if (!dec.readUnsigned(&key)) return sendStatus(cid, CTAP2_ERR_INVALID_CBOR);
        bool ok = true;
        switch (key) {
            case 0x01:
                ok = dec.readBytes(&clientDataHash, &clientDataHashLen);
                break;
            case 0x06: {  // extensions {"hmac-secret": true, ...}
                size_t m = 0;
                ok = dec.readMapHeader(&m);
                for (size_t e = 0; ok && e < m; e++) {
                    char k[24] = {0};
                    if (!readTextKey(dec, k, sizeof(k))) continue;
                    if (strcmp(k, "hmac-secret") == 0) ok = dec.readBool(&wantHmacSecret);
                    else ok = dec.skipValue();
                }
                break;
            }
            case 0x02: {  // rp {id, name}
                size_t m = 0;
                ok = haveRp = dec.readMapHeader(&m);
                for (size_t r = 0; ok && r < m; r++) {
                    char k[16] = {0};
                    if (!readTextKey(dec, k, sizeof(k))) continue;
                    ok = strcmp(k, "id") == 0 ? dec.readText(rpId, sizeof(rpId)) : dec.skipValue();
                }
                break;
            }
            case 0x03: {  // user {id, name, displayName}
                size_t m = 0;
                ok = haveUser = dec.readMapHeader(&m);
                for (size_t u = 0; ok && u < m; u++) {
                    char k[16] = {0};
                    if (!readTextKey(dec, k, sizeof(k))) continue;
                    if (strcmp(k, "id") == 0) ok = dec.readBytes(&userId, &userIdLen);
                    else if (strcmp(k, "name") == 0) ok = dec.readText(userName, sizeof(userName));
                    else if (strcmp(k, "displayName") == 0) ok = dec.readText(displayName, sizeof(displayName));
                    else ok = dec.skipValue();
                }
                break;
            }
            case 0x04: {  // pubKeyCredParams [{alg, type}]
                size_t n = 0;
                ok = haveParams = dec.readArrayHeader(&n);
                for (size_t p = 0; ok && p < n; p++) {
                    size_t m = 0;
                    if (!(ok = dec.readMapHeader(&m))) break;
                    int64_t alg = 0;
                    char type[16] = {0};
                    for (size_t e = 0; ok && e < m; e++) {
                        char k[8] = {0};
                        if (!readTextKey(dec, k, sizeof(k))) continue;
                        if (strcmp(k, "alg") == 0) ok = dec.readInt(&alg);
                        else if (strcmp(k, "type") == 0) ok = dec.readText(type, sizeof(type));
                        else ok = dec.skipValue();
                    }
                    if (alg == -7 && strcmp(type, "public-key") == 0) es256 = true;
                }
                break;
            }
            case 0x05:
                ok = parseCredList(dec, exclude, &excludeCount, &excludeTotal);
                break;
            case 0x07:
                ok = parseOptions(dec, opts);
                break;
            case 0x08:
                ok = havePinAuth = dec.readBytes(&pinAuth, &pinAuthLen);
                break;
            case 0x09:
                ok = dec.readUnsigned(&pinProtocol);
                break;
            default:
                ok = dec.skipValue();
                break;
        }
        if (!ok) return sendStatus(cid, CTAP2_ERR_CBOR_UNEXPECTED_TYPE);
    }

    if (!clientDataHash || !haveRp || !haveUser || !haveParams || !userId || rpId[0] == 0) {
        return sendStatus(cid, CTAP2_ERR_MISSING_PARAMETER);
    }
    if (clientDataHashLen != 32) return sendStatus(cid, CTAP2_ERR_INVALID_PARAM);
    if (havePinAuth && pinAuthLen == 0) return answerSelectionProbe(cid, rpId);
    if (havePinAuth && pinProtocol != 1) return sendStatus(cid, CTAP2_ERR_PIN_AUTH_INVALID);
    if (!es256) return sendStatus(cid, CTAP2_ERR_UNSUPPORTED_ALG);
    if (opts.uv == 1) return sendStatus(cid, CTAP2_ERR_UNSUPPORTED_OPTION);  // no built-in UV
    if (opts.up == 0) return sendStatus(cid, CTAP2_ERR_INVALID_OPTION);
    bool rk = opts.rk == 1;
    if (rk && userIdLen > FIDO_RK_USERID_LEN) return sendStatus(cid, CTAP2_ERR_INVALID_LENGTH);

    // excludeList: this key is already registered for the account
    for (uint8_t i = 0; i < excludeCount; i++) {
        uint8_t sk[32];
        bool mine = cryptoP256.verifyCredentialId(rpId, exclude[i], sk);
        mbedtls_platform_zeroize(sk, sizeof(sk));
        if (mine) {
            if (!waitForUser(cid, rpId, true)) return;
            return sendStatus(cid, CTAP2_ERR_CREDENTIAL_EXCLUDED);
        }
    }

    bool uvVerified = false;
    if (havePinAuth) {
        if (!checkPinAuth(pinAuth, pinAuthLen, clientDataHash)) return sendStatus(cid, CTAP2_ERR_PIN_AUTH_INVALID);
        uvVerified = true;
    } else if (_pinSet) {
        return sendStatus(cid, CTAP2_ERR_PIN_REQUIRED);
    }

    if (!waitForUser(cid, rpId, true)) return;

    // 1. Derive credential keypair (credential ID wraps the key, bound to rpId)
    uint8_t privKey[32];
    uint8_t credId[32];
    cryptoP256.deriveCredentialKey(rpId, userId, userIdLen, privKey, credId);
    uint8_t pubKeyRaw[64];
    cryptoP256.generateKeypair(privKey, pubKeyRaw);
    mbedtls_platform_zeroize(privKey, sizeof(privKey));

    uint32_t counter = cryptoP256.incrementSignatureCounter();

    if (rk) {
        ResidentCred rc;
        memset(&rc, 0, sizeof(rc));
        memcpy(rc.credId, credId, 32);
        CryptoP256::sha256((const uint8_t*)rpId, strlen(rpId), rc.rpIdHash);
        strncpy(rc.rpId, rpId, sizeof(rc.rpId) - 1);
        rc.userIdLen = (uint8_t)userIdLen;
        memcpy(rc.userId, userId, userIdLen);
        strncpy(rc.userName, userName, sizeof(rc.userName) - 1);
        strncpy(rc.displayName, displayName, sizeof(rc.displayName) - 1);
        rc.created = counter;
        if (!fidoStore.save(rc)) return sendStatus(cid, CTAP2_ERR_KEY_STORE_FULL);
        Serial.printf("[CTAP2] Passkey stored for %s (%u total)\n", rpId, fidoStore.count());
    }

    // 2. COSE public key (canonical key order: 1, 3, -1, -2, -3)
    uint8_t coseKey[128];
    CborEncoder coseEnc(coseKey, sizeof(coseKey));
    coseEnc.encodeMapHeader(5);
    coseEnc.encodeInt(1);
    coseEnc.encodeInt(2);   // kty: EC2
    coseEnc.encodeInt(3);
    coseEnc.encodeInt(-7);  // alg: ES256
    coseEnc.encodeInt(-1);
    coseEnc.encodeInt(1);   // crv: P-256
    coseEnc.encodeInt(-2);
    coseEnc.encodeBytes(pubKeyRaw, 32);
    coseEnc.encodeInt(-3);
    coseEnc.encodeBytes(pubKeyRaw + 32, 32);

    // 3. authData: rpIdHash || flags || signCount || AAGUID || credIdLen || credId || coseKey
    uint8_t authData[256];
    size_t adOffset = 0;
    CryptoP256::sha256((const uint8_t*)rpId, strlen(rpId), authData);
    adOffset += 32;
    authData[adOffset++] = AUTHDATA_FLAG_UP | (uvVerified ? AUTHDATA_FLAG_UV : 0) | AUTHDATA_FLAG_AT;
    authData[adOffset++] = (uint8_t)(counter >> 24);
    authData[adOffset++] = (uint8_t)(counter >> 16);
    authData[adOffset++] = (uint8_t)(counter >> 8);
    authData[adOffset++] = (uint8_t)counter;
    memcpy(authData + adOffset, _aaguid, 16);
    adOffset += 16;
    authData[adOffset++] = 0x00;
    authData[adOffset++] = 32;
    memcpy(authData + adOffset, credId, 32);
    adOffset += 32;
    memcpy(authData + adOffset, coseKey, coseEnc.getLength());
    adOffset += coseEnc.getLength();
    if (wantHmacSecret) {  // CredRandom is derived on demand from the master secret + credId
        CborEncoder ext(authData + adOffset, sizeof(authData) - adOffset);
        ext.encodeMapHeader(1);
        ext.encodeText("hmac-secret");
        ext.encodeBool(true);
        adOffset += ext.getLength();
        authData[32] |= AUTHDATA_FLAG_ED;
    }

    // 4. packed attestation: sig = ECDSA(attestation key, SHA-256(authData || clientDataHash))
    uint8_t sigInput[256 + 32];
    memcpy(sigInput, authData, adOffset);
    memcpy(sigInput + adOffset, clientDataHash, 32);
    uint8_t digest[32];
    CryptoP256::sha256(sigInput, adOffset + 32, digest);

    uint8_t sigDer[80];
    size_t sigLen = sizeof(sigDer);
    if (!cryptoP256.signDigest(FIDO_ATTESTATION_PRIVKEY, digest, sigDer, &sigLen) || sigLen == 0) {
        return sendStatus(cid, CTAP2_ERR_OTHER);
    }

    // 5. {1: "packed", 2: authData, 3: {alg: -7, sig, x5c: [cert]}}
    static uint8_t respBuf[1200];
    respBuf[0] = CTAP2_OK;
    CborEncoder respEnc(respBuf + 1, sizeof(respBuf) - 1);
    respEnc.encodeMapHeader(3);
    respEnc.encodeUnsigned(0x01);
    respEnc.encodeText("packed");
    respEnc.encodeUnsigned(0x02);
    respEnc.encodeBytes(authData, adOffset);
    respEnc.encodeUnsigned(0x03);
    respEnc.encodeMapHeader(3);
    respEnc.encodeText("alg");
    respEnc.encodeInt(-7);
    respEnc.encodeText("sig");
    respEnc.encodeBytes(sigDer, sigLen);
    respEnc.encodeText("x5c");
    respEnc.encodeArrayHeader(1);
    respEnc.encodeBytes(FIDO_ATTESTATION_CERT, sizeof(FIDO_ATTESTATION_CERT));

    ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + respEnc.getLength());
}

bool Ctap2Engine::hmacSecretOutput(const uint8_t* credId, const HmacSecretReq& req,
                                   uint8_t* extCbor, size_t* extLen) {
    if (!_hasEphemKey || req.saltAuthLen != 16 || (req.saltEncLen != 32 && req.saltEncLen != 64)) return false;
    uint8_t shared[32];
    if (!cryptoP256.computeSharedSecretP256(_ephemPrivKey, req.platformKey, shared)) return false;

    uint8_t mac[32];
    CryptoP256::hmacSha256(shared, 32, req.saltEnc, req.saltEncLen, mac);
    bool authOk = constantTimeEqual(mac, req.saltAuth, 16);
    uint8_t salts[64], out[64], credRandom[32];
    bool ok = false;
    if (authOk) {
        CryptoP256::aes256CbcDecrypt(shared, NULL, req.saltEnc, req.saltEncLen, salts);
        cryptoP256.deriveCredRandom(credId, 32, credRandom);
        CryptoP256::hmacSha256(credRandom, 32, salts, 32, out);
        if (req.saltEncLen == 64) CryptoP256::hmacSha256(credRandom, 32, salts + 32, 32, out + 32);
        uint8_t outEnc[64];
        CryptoP256::aes256CbcEncrypt(shared, NULL, out, req.saltEncLen, outEnc);
        CborEncoder ext(extCbor, *extLen);
        ext.encodeMapHeader(1);
        ext.encodeText("hmac-secret");
        ext.encodeBytes(outEnc, req.saltEncLen);
        *extLen = ext.getLength();
        mbedtls_platform_zeroize(outEnc, sizeof(outEnc));
        ok = true;
    }
    mbedtls_platform_zeroize(shared, sizeof(shared));
    mbedtls_platform_zeroize(salts, sizeof(salts));
    mbedtls_platform_zeroize(out, sizeof(out));
    mbedtls_platform_zeroize(credRandom, sizeof(credRandom));
    return ok;
}

void Ctap2Engine::sendAssertion(uint32_t cid, const char* rpId, const uint8_t* clientDataHash,
                                const uint8_t* credId, const ResidentCred* rk, uint8_t flags,
                                bool withUserDetails, uint8_t numberOfCredentials,
                                const HmacSecretReq* hmac) {
    uint8_t ext[96];
    size_t extLen = 0;
    if (hmac && hmac->present) {
        extLen = sizeof(ext);
        if (!hmacSecretOutput(credId, *hmac, ext, &extLen)) return sendStatus(cid, CTAP2_ERR_INVALID_PARAM);
        flags |= AUTHDATA_FLAG_ED;
    }

    uint8_t privKey[32];
    if (!cryptoP256.verifyCredentialId(rpId, credId, privKey)) {
        mbedtls_platform_zeroize(privKey, sizeof(privKey));
        return sendStatus(cid, CTAP2_ERR_NO_CREDENTIALS);
    }

    uint8_t authData[37 + sizeof(ext)];
    size_t adLen = 37 + extLen;
    CryptoP256::sha256((const uint8_t*)rpId, strlen(rpId), authData);
    authData[32] = flags;
    uint32_t counter = cryptoP256.incrementSignatureCounter();
    authData[33] = (uint8_t)(counter >> 24);
    authData[34] = (uint8_t)(counter >> 16);
    authData[35] = (uint8_t)(counter >> 8);
    authData[36] = (uint8_t)counter;
    memcpy(authData + 37, ext, extLen);

    uint8_t signInput[sizeof(authData) + 32];
    memcpy(signInput, authData, adLen);
    memcpy(signInput + adLen, clientDataHash, 32);
    uint8_t digest[32];
    CryptoP256::sha256(signInput, adLen + 32, digest);

    uint8_t sigDer[80];
    size_t sigLen = sizeof(sigDer);
    bool signOk = cryptoP256.signDigest(privKey, digest, sigDer, &sigLen);
    mbedtls_platform_zeroize(privKey, sizeof(privKey));
    if (!signOk) return sendStatus(cid, CTAP2_ERR_OTHER);

    bool details = rk && withUserDetails;
    uint8_t respBuf[512];
    respBuf[0] = CTAP2_OK;
    CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
    enc.encodeMapHeader(3 + (rk ? 1 : 0) + (numberOfCredentials > 1 ? 1 : 0));

    enc.encodeUnsigned(0x01);  // credential {id, type}
    enc.encodeMapHeader(2);
    enc.encodeText("id");
    enc.encodeBytes(credId, 32);
    enc.encodeText("type");
    enc.encodeText("public-key");

    enc.encodeUnsigned(0x02);
    enc.encodeBytes(authData, adLen);
    enc.encodeUnsigned(0x03);
    enc.encodeBytes(sigDer, sigLen);

    if (rk) {  // user {id[, name, displayName]} (canonical order)
        bool hasName = details && rk->userName[0];
        bool hasDisplay = details && rk->displayName[0];
        enc.encodeUnsigned(0x04);
        enc.encodeMapHeader(1 + hasName + hasDisplay);
        enc.encodeText("id");
        enc.encodeBytes(rk->userId, rk->userIdLen);
        if (hasName) {
            enc.encodeText("name");
            enc.encodeText(rk->userName);
        }
        if (hasDisplay) {
            enc.encodeText("displayName");
            enc.encodeText(rk->displayName);
        }
    }
    if (numberOfCredentials > 1) {
        enc.encodeUnsigned(0x05);
        enc.encodeUnsigned(numberOfCredentials);
    }

    ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
}

void Ctap2Engine::handleGetAssertion(uint32_t cid, CborDecoder& dec) {
    size_t mapCount = 0;
    if (!dec.readMapHeader(&mapCount)) return sendStatus(cid, CTAP2_ERR_INVALID_CBOR);

    char rpId[128] = {0};
    const uint8_t* clientDataHash = nullptr;
    size_t clientDataHashLen = 0;
    const uint8_t* allow[FIDO_MAX_LIST];
    uint8_t allowCount = 0;
    size_t allowTotal = 0;
    CtapOptions opts;
    const uint8_t* pinAuth = nullptr;
    size_t pinAuthLen = 0;
    bool havePinAuth = false;
    uint64_t pinProtocol = 0;
    HmacSecretReq hmacReq;
    memset(&hmacReq, 0, sizeof(hmacReq));

    for (size_t i = 0; i < mapCount; i++) {
        uint64_t key = 0;
        if (!dec.readUnsigned(&key)) return sendStatus(cid, CTAP2_ERR_INVALID_CBOR);
        bool ok = true;
        switch (key) {
            case 0x01: ok = dec.readText(rpId, sizeof(rpId)); break;
            case 0x02: ok = dec.readBytes(&clientDataHash, &clientDataHashLen); break;
            case 0x03: ok = parseCredList(dec, allow, &allowCount, &allowTotal); break;
            case 0x04: {  // extensions {"hmac-secret": {1: keyAgreement, 2: saltEnc, 3: saltAuth}}
                size_t m = 0;
                ok = dec.readMapHeader(&m);
                for (size_t e = 0; ok && e < m; e++) {
                    char k[24] = {0};
                    if (!readTextKey(dec, k, sizeof(k))) continue;
                    if (strcmp(k, "hmac-secret") != 0) { ok = dec.skipValue(); continue; }
                    size_t hm = 0;
                    if (!(ok = dec.readMapHeader(&hm))) break;
                    bool haveX = false, haveY = false;
                    for (size_t h = 0; ok && h < hm; h++) {
                        uint64_t hk = 0;
                        if (!(ok = dec.readUnsigned(&hk))) break;
                        if (hk == 1) {  // COSE_Key
                            size_t cm = 0;
                            if (!(ok = dec.readMapHeader(&cm))) break;
                            for (size_t c = 0; ok && c < cm; c++) {
                                int64_t ck = 0;
                                if (!(ok = dec.readInt(&ck))) break;
                                if (ck == -2 || ck == -3) {
                                    const uint8_t* p = nullptr;
                                    size_t len = 0;
                                    if (!(ok = dec.readBytes(&p, &len))) break;
                                    if (len == 32) {
                                        memcpy(hmacReq.platformKey + (ck == -2 ? 0 : 32), p, 32);
                                        (ck == -2 ? haveX : haveY) = true;
                                    }
                                } else {
                                    ok = dec.skipValue();
                                }
                            }
                        } else if (hk == 2) {
                            ok = dec.readBytes(&hmacReq.saltEnc, &hmacReq.saltEncLen);
                        } else if (hk == 3) {
                            ok = dec.readBytes(&hmacReq.saltAuth, &hmacReq.saltAuthLen);
                        } else {
                            ok = dec.skipValue();
                        }
                    }
                    hmacReq.present = haveX && haveY && hmacReq.saltEnc && hmacReq.saltAuth;
                    if (!hmacReq.present) return sendStatus(cid, CTAP2_ERR_MISSING_PARAMETER);
                }
                break;
            }
            case 0x05: ok = parseOptions(dec, opts); break;
            case 0x06: ok = havePinAuth = dec.readBytes(&pinAuth, &pinAuthLen); break;
            case 0x07: ok = dec.readUnsigned(&pinProtocol); break;
            default:   ok = dec.skipValue(); break;
        }
        if (!ok) return sendStatus(cid, CTAP2_ERR_CBOR_UNEXPECTED_TYPE);
    }

    if (!clientDataHash || rpId[0] == 0) return sendStatus(cid, CTAP2_ERR_MISSING_PARAMETER);
    if (clientDataHashLen != 32) return sendStatus(cid, CTAP2_ERR_INVALID_PARAM);
    if (havePinAuth && pinAuthLen == 0) return answerSelectionProbe(cid, rpId);
    if (havePinAuth && pinProtocol != 1) return sendStatus(cid, CTAP2_ERR_PIN_AUTH_INVALID);
    if (opts.rk != -1 || opts.uv == 1) return sendStatus(cid, CTAP2_ERR_UNSUPPORTED_OPTION);
    bool up = opts.up != 0;

    bool uvVerified = false;
    if (havePinAuth) {
        if (!checkPinAuth(pinAuth, pinAuthLen, clientDataHash)) return sendStatus(cid, CTAP2_ERR_PIN_AUTH_INVALID);
        uvVerified = true;
    }

    const uint8_t* credId = nullptr;
    const ResidentCred* rk = nullptr;
    uint8_t slots[FIDO_RK_SLOTS];
    uint8_t found = 0;

    if (allowTotal > 0) {
        for (uint8_t i = 0; i < allowCount && !credId; i++) {
            uint8_t sk[32];
            if (cryptoP256.verifyCredentialId(rpId, allow[i], sk)) {
                credId = allow[i];
                int slot = fidoStore.findByCredId(credId);
                if (slot >= 0) rk = &fidoStore.slot(slot);
            }
            mbedtls_platform_zeroize(sk, sizeof(sk));
        }
    } else {
        uint8_t rpIdHash[32];
        CryptoP256::sha256((const uint8_t*)rpId, strlen(rpId), rpIdHash);
        found = fidoStore.findByRp(rpIdHash, slots, FIDO_RK_SLOTS);
        if (found > 0) {
            rk = &fidoStore.slot(slots[0]);
            credId = rk->credId;
        }
    }

    if (!credId) {
        // Not registered here. With UP requested, still wait for the touch so the user
        // can tell which key they picked (and so presence can't be probed silently).
        if (up && !waitForUser(cid, rpId, false)) return;
        return sendStatus(cid, CTAP2_ERR_NO_CREDENTIALS);
    }

    if (up && !waitForUser(cid, rpId, false)) return;
    uint8_t flags = (up ? AUTHDATA_FLAG_UP : 0) | (uvVerified ? AUTHDATA_FLAG_UV : 0);

    bool multiple = found > 1;
    sendAssertion(cid, rpId, clientDataHash, credId, rk, flags, multiple && uvVerified, multiple ? found : 0,
                  &hmacReq);

    if (multiple) {
        _pending.active = true;
        _pending.expiresAt = millis() + 30000;
        strncpy(_pending.rpId, rpId, sizeof(_pending.rpId) - 1);
        _pending.rpId[sizeof(_pending.rpId) - 1] = '\0';
        memcpy(_pending.clientDataHash, clientDataHash, 32);
        _pending.flags = flags;
        _pending.withUserDetails = uvVerified;
        memcpy(_pending.slots, slots, found);
        _pending.count = found;
        _pending.next = 1;
    }
}

void Ctap2Engine::handleGetNextAssertion(uint32_t cid) {
    if (!_pending.active || (int32_t)(millis() - _pending.expiresAt) > 0 || _pending.next >= _pending.count) {
        _pending.active = false;
        return sendStatus(cid, CTAP2_ERR_NOT_ALLOWED);
    }
    const ResidentCred* rk = &fidoStore.slot(_pending.slots[_pending.next++]);
    sendAssertion(cid, _pending.rpId, _pending.clientDataHash, rk->credId, rk,
                  _pending.flags, _pending.withUserDetails, 0);
}

void Ctap2Engine::handleReset(uint32_t cid) {
    if (millis() - _readyAt > FIDO_RESET_WINDOW_MS) return sendStatus(cid, CTAP2_ERR_NOT_ALLOWED);
    if (!waitForUser(cid, "RESET ALL PASSKEYS", true)) return;

    fidoStore.eraseAll();
    cryptoP256.rotateMasterSecret();
    Preferences prefs;
    prefs.begin("fido_vault", false);
    prefs.remove("pin_hash");
    prefs.end();
    _pinSet = false;
    mbedtls_platform_zeroize(_pinHash, sizeof(_pinHash));
    _pinTokenValid = false;
    mbedtls_platform_zeroize(_pinToken, sizeof(_pinToken));
    resetPinRetries();
    Serial.println("[CTAP2] authenticatorReset: all FIDO credentials destroyed");
    sendStatus(cid, CTAP2_OK);
}

void Ctap2Engine::handleClientPin(uint32_t cid, CborDecoder& dec) {
    size_t mapCount = 0;
    if (!dec.readMapHeader(&mapCount)) return sendStatus(cid, CTAP2_ERR_INVALID_CBOR);

    uint64_t pinProtocol = 0;
    uint64_t subCommand = 0;
    bool haveProtocol = false, haveSub = false;
    const uint8_t* pinAuth = nullptr;
    size_t pinAuthLen = 0;
    const uint8_t* newPinEnc = nullptr;
    size_t newPinEncLen = 0;
    const uint8_t* pinHashEnc = nullptr;
    size_t pinHashEncLen = 0;
    uint8_t peerPubRaw[64] = {0};
    bool hasPeerKey = false;

    for (size_t i = 0; i < mapCount; i++) {
        uint64_t key = 0;
        if (!dec.readUnsigned(&key)) return sendStatus(cid, CTAP2_ERR_INVALID_CBOR);
        bool ok = true;
        switch (key) {
            case 0x01: ok = haveProtocol = dec.readUnsigned(&pinProtocol); break;
            case 0x02: ok = haveSub = dec.readUnsigned(&subCommand); break;
            case 0x03: {  // keyAgreement (COSE_Key)
                size_t m = 0;
                if (!(ok = dec.readMapHeader(&m))) break;
                bool haveX = false, haveY = false;
                for (size_t c = 0; ok && c < m; c++) {
                    int64_t coseKey = 0;
                    if (!(ok = dec.readInt(&coseKey))) break;
                    if (coseKey == -2 || coseKey == -3) {
                        const uint8_t* p = nullptr;
                        size_t len = 0;
                        if (!(ok = dec.readBytes(&p, &len))) break;
                        if (len == 32) {
                            memcpy(peerPubRaw + (coseKey == -2 ? 0 : 32), p, 32);
                            (coseKey == -2 ? haveX : haveY) = true;
                        }
                    } else {
                        ok = dec.skipValue();
                    }
                }
                hasPeerKey = haveX && haveY;
                break;
            }
            case 0x04: ok = dec.readBytes(&pinAuth, &pinAuthLen); break;
            case 0x05: ok = dec.readBytes(&newPinEnc, &newPinEncLen); break;
            case 0x06: ok = dec.readBytes(&pinHashEnc, &pinHashEncLen); break;
            default:   ok = dec.skipValue(); break;
        }
        if (!ok) return sendStatus(cid, CTAP2_ERR_CBOR_UNEXPECTED_TYPE);
    }

    if (!haveProtocol || !haveSub) return sendStatus(cid, CTAP2_ERR_MISSING_PARAMETER);
    if (pinProtocol != 1) return sendStatus(cid, CTAP2_ERR_INVALID_PARAM);

    switch (subCommand) {
        case CTAP2_PIN_SUBCMD_GET_PIN_RETRIES: {
            uint8_t respBuf[16];
            respBuf[0] = CTAP2_OK;
            CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
            enc.encodeMapHeader(1);
            enc.encodeUnsigned(0x03);
            enc.encodeUnsigned(_pinRetries);
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
            return;
        }

        case CTAP2_PIN_SUBCMD_GET_KEY_AGREEMENT: {
            if (!_hasEphemKey) {
                cryptoP256.getRandomBytes(_ephemPrivKey, 32);
                cryptoP256.generateKeypair(_ephemPrivKey, _ephemPubKeyRaw);
                _hasEphemKey = true;
            }
            uint8_t respBuf[128];
            respBuf[0] = CTAP2_OK;
            CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
            enc.encodeMapHeader(1);
            enc.encodeUnsigned(0x01);
            enc.encodeMapHeader(5);
            enc.encodeInt(1);
            enc.encodeInt(2);    // kty: EC2
            enc.encodeInt(3);
            enc.encodeInt(-25);  // alg: ECDH-ES+HKDF-256 (as CTAP2 specifies)
            enc.encodeInt(-1);
            enc.encodeInt(1);    // crv: P-256
            enc.encodeInt(-2);
            enc.encodeBytes(_ephemPubKeyRaw, 32);
            enc.encodeInt(-3);
            enc.encodeBytes(_ephemPubKeyRaw + 32, 32);
            ctapHid.sendResponse(cid, CTAPHID_CMD_CBOR, respBuf, 1 + enc.getLength());
            return;
        }

        case CTAP2_PIN_SUBCMD_SET_PIN:
        case CTAP2_PIN_SUBCMD_CHANGE_PIN:
        case CTAP2_PIN_SUBCMD_GET_PIN_TOKEN:
            break;

        default:
            return sendStatus(cid, CTAP2_ERR_INVALID_SUBCOMMAND);
    }

    bool isSet = subCommand == CTAP2_PIN_SUBCMD_SET_PIN;
    bool isChange = subCommand == CTAP2_PIN_SUBCMD_CHANGE_PIN;
    if (!hasPeerKey) return sendStatus(cid, CTAP2_ERR_MISSING_PARAMETER);
    if ((isSet || isChange) && (!newPinEnc || !pinAuth)) return sendStatus(cid, CTAP2_ERR_MISSING_PARAMETER);
    if (!isSet && !pinHashEnc) return sendStatus(cid, CTAP2_ERR_MISSING_PARAMETER);
    if ((newPinEnc && newPinEncLen != 64) || (pinHashEnc && pinHashEncLen != 16) || (pinAuth && pinAuthLen != 16)) {
        return sendStatus(cid, CTAP2_ERR_INVALID_PARAM);
    }
    if (isSet && _pinSet) return sendStatus(cid, CTAP2_ERR_NOT_ALLOWED);
    if (!isSet && !_pinSet) return sendStatus(cid, CTAP2_ERR_PIN_NOT_SET);
    if (!isSet && _pinRetries == 0) return sendStatus(cid, CTAP2_ERR_PIN_BLOCKED);
    if (!isSet && _consecutivePinFails >= 3) return sendStatus(cid, CTAP2_ERR_PIN_AUTH_BLOCKED);
    if (!_hasEphemKey) return sendStatus(cid, CTAP2_ERR_PIN_AUTH_INVALID);

    uint8_t sharedKey[32];
    if (!cryptoP256.computeSharedSecretP256(_ephemPrivKey, peerPubRaw, sharedKey)) {
        return sendStatus(cid, CTAP2_ERR_INVALID_PARAM);
    }

    // setPin: pinAuth = LEFT(HMAC(shared, newPinEnc), 16)
    // changePin: pinAuth = LEFT(HMAC(shared, newPinEnc || pinHashEnc), 16)
    if (isSet || isChange) {
        uint8_t macInput[64 + 16];
        memcpy(macInput, newPinEnc, 64);
        if (isChange) memcpy(macInput + 64, pinHashEnc, 16);
        uint8_t mac[32];
        CryptoP256::hmacSha256(sharedKey, 32, macInput, isChange ? 80 : 64, mac);
        bool ok = constantTimeEqual(mac, pinAuth, 16);
        if (!ok) {
            mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));
            return sendStatus(cid, CTAP2_ERR_PIN_AUTH_INVALID);
        }
    }

    // Verify the current PIN (changePin / getPinToken). A retry is spent before comparing.
    if (!isSet) {
        setPinRetries(_pinRetries - 1);
        uint8_t candHash[16];
        CryptoP256::aes256CbcDecrypt(sharedKey, NULL, pinHashEnc, 16, candHash);
        bool match = constantTimeEqual(candHash, _pinHash, 16);
        mbedtls_platform_zeroize(candHash, sizeof(candHash));
        if (!match) {
            mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));
            return sendStatus(cid, pinFailure());
        }
        resetPinRetries();
    }

    if (isSet || isChange) {
        uint8_t decPin[64];
        CryptoP256::aes256CbcDecrypt(sharedKey, NULL, newPinEnc, 64, decPin);
        mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));
        size_t pinLen = 0;
        while (pinLen < 64 && decPin[pinLen] != 0) pinLen++;
        if (pinLen < 4 || pinLen > 63) {
            mbedtls_platform_zeroize(decPin, sizeof(decPin));
            return sendStatus(cid, CTAP2_ERR_PIN_POLICY_VIOLATION);
        }
        uint8_t full[32];
        CryptoP256::sha256(decPin, pinLen, full);
        mbedtls_platform_zeroize(decPin, sizeof(decPin));
        storePinHash(full);
        mbedtls_platform_zeroize(full, sizeof(full));
        resetPinRetries();
        _pinTokenValid = false;
        Serial.println(isSet ? "[CTAP2] FIDO PIN set" : "[CTAP2] FIDO PIN changed");
        return sendStatus(cid, CTAP2_OK);
    }

    // getPinToken
    cryptoP256.getRandomBytes(_pinToken, 32);
    _pinTokenValid = true;
    uint8_t encToken[32];
    CryptoP256::aes256CbcEncrypt(sharedKey, NULL, _pinToken, 32, encToken);
    mbedtls_platform_zeroize(sharedKey, sizeof(sharedKey));

    uint8_t respBuf[64];
    respBuf[0] = CTAP2_OK;
    CborEncoder enc(respBuf + 1, sizeof(respBuf) - 1);
    enc.encodeMapHeader(1);
    enc.encodeUnsigned(0x02);
    enc.encodeBytes(encToken, 32);
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

    // Payload: extended-length APDU (00 Lc1 Lc2 data [Le Le], what browsers and libfido2
    // send) or short APDU (Lc data [Le]).
    const uint8_t* data = req + 4;
    uint16_t dataLen = 0;
    if (reqLen >= 7 && req[4] == 0x00) {
        uint16_t lc = ((uint16_t)req[5] << 8) | req[6];
        data = req + 7;
        dataLen = (lc <= reqLen - 7) ? lc : (reqLen - 7);
    } else if (reqLen >= 5) {
        uint8_t lc = req[4];
        data = req + 5;
        dataLen = (lc <= reqLen - 5) ? lc : (reqLen - 5);
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
        cryptoP256.signDigest(FIDO_ATTESTATION_PRIVKEY, sigHash, derSig, &derSigLen);

        // Assemble registration response
        uint8_t respBuf[1024];
        size_t rOffset = 0;
        respBuf[rOffset++] = 0x05;
        respBuf[rOffset++] = 0x04;
        memcpy(respBuf + rOffset, pubKeyRaw, 64);
        rOffset += 64;
        respBuf[rOffset++] = 32; // Key handle len
        memcpy(respBuf + rOffset, credId, 32);
        rOffset += 32;
        memcpy(respBuf + rOffset, FIDO_ATTESTATION_CERT, sizeof(FIDO_ATTESTATION_CERT));
        rOffset += sizeof(FIDO_ATTESTATION_CERT);
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
