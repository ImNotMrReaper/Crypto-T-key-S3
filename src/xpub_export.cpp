#include "xpub_export.h"
#include "bip32_engine.h"
#include <mbedtls/platform_util.h>
#include <mbedtls/ripemd160.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t HARD = 0x80000000u;
constexpr uint32_t XPUB_VERSION = 0x0488B21Eu;
constexpr uint32_t ZPUB_VERSION = 0x04B24746u;
constexpr size_t SERIALIZED_KEY_LEN = 78;
constexpr size_t CHECKED_KEY_LEN = 82;

static void writeBE32(uint8_t out[4], uint32_t value) {
    out[0] = static_cast<uint8_t>(value >> 24);
    out[1] = static_cast<uint8_t>(value >> 16);
    out[2] = static_cast<uint8_t>(value >> 8);
    out[3] = static_cast<uint8_t>(value);
}

static void writeHex(const uint8_t* data, size_t len, char* out) {
    static const char DIGITS[] = "0123456789abcdef";   // not "HEX": Arduino #defines HEX
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = DIGITS[data[i] >> 4];
        out[2 * i + 1] = DIGITS[data[i] & 0x0f];
    }
    out[2 * len] = '\0';
}

static bool hash160(const uint8_t pub[33], uint8_t out[20]) {
    uint8_t sha[32] = {};
    bool ok = mbedtls_sha256(pub, 33, sha, 0) == 0 && mbedtls_ripemd160(sha, sizeof(sha), out) == 0;
    mbedtls_platform_zeroize(sha, sizeof(sha));
    return ok;
}

static bool base58Check(const uint8_t payload[SERIALIZED_KEY_LEN], char* out, size_t outCap) {
    uint8_t checked[CHECKED_KEY_LEN] = {};
    uint8_t firstHash[32] = {};
    uint8_t secondHash[32] = {};
    uint8_t work[CHECKED_KEY_LEN] = {};
    char encoded[120] = {};
    size_t encodedLen = 0;
    size_t zeroes = 0;
    size_t start = 0;
    bool ok = false;
    static const char ALPHABET[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    memcpy(checked, payload, SERIALIZED_KEY_LEN);
    if (mbedtls_sha256(checked, SERIALIZED_KEY_LEN, firstHash, 0) != 0 ||
        mbedtls_sha256(firstHash, sizeof(firstHash), secondHash, 0) != 0) {
        goto cleanup;
    }
    memcpy(checked + SERIALIZED_KEY_LEN, secondHash, 4);
    memcpy(work, checked, sizeof(work));

    while (zeroes < sizeof(work) && work[zeroes] == 0) zeroes++;
    start = zeroes;
    while (start < sizeof(work)) {
        uint32_t remainder = 0;
        for (size_t i = start; i < sizeof(work); i++) {
            uint32_t accumulator = (remainder << 8) | work[i];
            work[i] = static_cast<uint8_t>(accumulator / 58);
            remainder = accumulator % 58;
        }
        if (encodedLen >= sizeof(encoded)) goto cleanup;
        encoded[encodedLen++] = ALPHABET[remainder];
        while (start < sizeof(work) && work[start] == 0) start++;
    }
    for (size_t i = 0; i < zeroes; i++) {
        if (encodedLen >= sizeof(encoded)) goto cleanup;
        encoded[encodedLen++] = ALPHABET[0];
    }
    if (encodedLen + 1 > outCap) goto cleanup;
    for (size_t i = 0; i < encodedLen; i++) out[i] = encoded[encodedLen - i - 1];
    out[encodedLen] = '\0';
    ok = true;

cleanup:
    mbedtls_platform_zeroize(checked, sizeof(checked));
    mbedtls_platform_zeroize(firstHash, sizeof(firstHash));
    mbedtls_platform_zeroize(secondHash, sizeof(secondHash));
    mbedtls_platform_zeroize(work, sizeof(work));
    mbedtls_platform_zeroize(encoded, sizeof(encoded));
    return ok;
}

static bool encodeExtendedKey(uint32_t version, const Bip32Node& account,
                              const uint8_t parentFingerprint[4], char* out, size_t outCap) {
    uint8_t payload[SERIALIZED_KEY_LEN] = {};
    writeBE32(payload, version);
    payload[4] = 3;  // account node depth: purpose, coin type, account
    memcpy(payload + 5, parentFingerprint, 4);
    writeBE32(payload + 9, HARD);  // child number 0'
    memcpy(payload + 13, account.chainCode, sizeof(account.chainCode));
    memcpy(payload + 45, account.pubKeyCompressed, sizeof(account.pubKeyCompressed));
    bool ok = base58Check(payload, out, outCap);
    mbedtls_platform_zeroize(payload, sizeof(payload));
    return ok;
}

}  // namespace

bool XpubExport::exportBip84Account(const uint8_t seed[64], char* xpub, size_t xpubCap,
                                    char* zpub, size_t zpubCap, char* origin, size_t originCap) {
    if (xpub && xpubCap) xpub[0] = '\0';
    if (zpub && zpubCap) zpub[0] = '\0';
    if (origin && originCap) origin[0] = '\0';
    if (!seed || !xpub || !zpub || !origin || xpubCap < EXTENDED_KEY_TEXT_LEN ||
        zpubCap < EXTENDED_KEY_TEXT_LEN || originCap < KEY_ORIGIN_TEXT_LEN) {
        return false;
    }

    const uint32_t parentPath[] = {84u | HARD, 0u | HARD};
    const uint32_t accountPath[] = {84u | HARD, 0u | HARD, 0u | HARD};
    Bip32Node master = {};
    Bip32Node parent = {};
    Bip32Node account = {};
    uint8_t masterFingerprintHash[20] = {};
    uint8_t parentFingerprintHash[20] = {};
    char fingerprint[9] = {};
    int originLen = 0;
    bool ok = false;

    if (!Bip32Engine::initMasterNode(seed, &master) ||
        !Bip32Engine::derivePath(&master, parentPath, 2, &parent) ||
        !Bip32Engine::derivePath(&master, accountPath, 3, &account) ||
        !hash160(master.pubKeyCompressed, masterFingerprintHash) ||
        !hash160(parent.pubKeyCompressed, parentFingerprintHash)) {
        goto cleanup;
    }

    writeHex(masterFingerprintHash, 4, fingerprint);
    if (!encodeExtendedKey(XPUB_VERSION, account, parentFingerprintHash, xpub, xpubCap) ||
        !encodeExtendedKey(ZPUB_VERSION, account, parentFingerprintHash, zpub, zpubCap)) {
        goto cleanup;
    }
    originLen = snprintf(origin, originCap, "[%s/84h/0h/0h]", fingerprint);
    if (originLen < 0 || static_cast<size_t>(originLen) >= originCap) goto cleanup;
    ok = true;

cleanup:
    if (!ok) {
        xpub[0] = '\0';
        zpub[0] = '\0';
        origin[0] = '\0';
    }
    mbedtls_platform_zeroize(&master, sizeof(master));
    mbedtls_platform_zeroize(&parent, sizeof(parent));
    mbedtls_platform_zeroize(&account, sizeof(account));
    mbedtls_platform_zeroize(masterFingerprintHash, sizeof(masterFingerprintHash));
    mbedtls_platform_zeroize(parentFingerprintHash, sizeof(parentFingerprintHash));
    mbedtls_platform_zeroize(fingerprint, sizeof(fingerprint));
    return ok;
}
