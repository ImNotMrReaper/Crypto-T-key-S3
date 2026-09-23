/**
 * bip32_engine.cpp — Production BIP-32 / BIP-39 / BIP-84 / EIP-55 Implementation
 * Standard: Follows hardware-wallet-dev skill.
 */

#include "bip32_engine.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"
#include "mbedtls/ripemd160.h"
#include "mbedtls/sha256.h"
#include "mbedtls/bignum.h"
#include <Crypto.h>
#include <SHA3.h>
#include <KeccakCore.h>
#include <Ed25519.h>
#include <uECC.h>

static void keccak256(const void* data, size_t len, uint8_t hash[32]) {
    KeccakCore core;
    core.setCapacity(512); // Keccak-256 rate is 1600 - 512 = 1088 bits (136 bytes)
    core.reset();
    core.update(data, len);
    core.pad(0x01); // 0x01 is canonical Ethereum Keccak padding (vs 0x06 for NIST SHA-3)
    core.extract(hash, 32);
    core.clear();
}

void Bip32Engine::secureZero(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    while (len--) *p++ = 0;
}

// ─── 1. BIP-39 Mnemonic to 64-byte Binary Seed ───────────────────────────────
bool Bip32Engine::mnemonicToSeed(const char* mnemonic, const char* passphrase, uint8_t outSeed[64]) {
    if (!mnemonic || !outSeed) return false;

    char salt[128] = "mnemonic";
    if (passphrase && strlen(passphrase) > 0) {
        strncat(salt, passphrase, sizeof(salt) - strlen(salt) - 1);
    }

    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA512,
                                            (const unsigned char*)mnemonic, strlen(mnemonic),
                                            (const unsigned char*)salt, strlen(salt),
                                            2048, 64, outSeed);
    return (ret == 0);
}

// ─── 2. BIP-32 Master Node Generation ─────────────────────────────────────────
bool Bip32Engine::initMasterNode(const uint8_t seed[64], Bip32Node* outNode) {
    if (!seed || !outNode) return false;

    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    uint8_t hmacOut[64];

    int ret = mbedtls_md_hmac(mdInfo,
                              (const unsigned char*)"Bitcoin seed", 12,
                              seed, 64,
                              hmacOut);
    if (ret != 0) return false;

    memcpy(outNode->privKey, hmacOut, 32);
    memcpy(outNode->chainCode, hmacOut + 32, 32);
    secureZero(hmacOut, sizeof(hmacOut));

    // Derive public keys
    uint8_t pub64[64];
    uECC_compute_public_key(outNode->privKey, pub64, uECC_secp256k1());

    outNode->pubKeyCompressed[0] = (pub64[63] & 1) ? 0x03 : 0x02;
    memcpy(outNode->pubKeyCompressed + 1, pub64, 32);

    outNode->pubKeyUncompressed[0] = 0x04;
    memcpy(outNode->pubKeyUncompressed + 1, pub64, 64);
    return true;
}

// ─── 3. BIP-32 Child Key Derivation ──────────────────────────────────────────
bool Bip32Engine::deriveChild(const Bip32Node* parent, uint32_t index, Bip32Node* outChild) {
    if (!parent || !outChild) return false;

    uint8_t data[37];
    size_t dataLen = 0;

    if (index >= 0x80000000) {
        // Hardened: 0x00 || privKey || index (big-endian)
        data[0] = 0x00;
        memcpy(data + 1, parent->privKey, 32);
        data[33] = (index >> 24) & 0xFF;
        data[34] = (index >> 16) & 0xFF;
        data[35] = (index >> 8) & 0xFF;
        data[36] = index & 0xFF;
        dataLen = 37;
    } else {
        // Non-hardened: compressed pubKey || index (big-endian)
        memcpy(data, parent->pubKeyCompressed, 33);
        data[33] = (index >> 24) & 0xFF;
        data[34] = (index >> 16) & 0xFF;
        data[35] = (index >> 8) & 0xFF;
        data[36] = index & 0xFF;
        dataLen = 37;
    }

    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    uint8_t hmacOut[64];
    if (mbedtls_md_hmac(mdInfo, parent->chainCode, 32, data, dataLen, hmacOut) != 0) {
        return false;
    }

    // child_privKey = (hmacOut[0..31] + parent->privKey) mod N
    mbedtls_mpi a, b, n, res;
    mbedtls_mpi_init(&a);
    mbedtls_mpi_init(&b);
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&res);

    mbedtls_mpi_read_string(&n, 16, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
    mbedtls_mpi_read_binary(&a, hmacOut, 32);
    mbedtls_mpi_read_binary(&b, parent->privKey, 32);

    mbedtls_mpi_add_mpi(&res, &a, &b);
    mbedtls_mpi_mod_mpi(&res, &res, &n);

    mbedtls_mpi_write_binary(&res, outChild->privKey, 32);
    memcpy(outChild->chainCode, hmacOut + 32, 32);

    mbedtls_mpi_free(&a);
    mbedtls_mpi_free(&b);
    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&res);
    secureZero(hmacOut, sizeof(hmacOut));

    // Compute public keys
    uint8_t pub64[64];
    uECC_compute_public_key(outChild->privKey, pub64, uECC_secp256k1());

    outChild->pubKeyCompressed[0] = (pub64[63] & 1) ? 0x03 : 0x02;
    memcpy(outChild->pubKeyCompressed + 1, pub64, 32);

    outChild->pubKeyUncompressed[0] = 0x04;
    memcpy(outChild->pubKeyUncompressed + 1, pub64, 64);
    return true;
}

bool Bip32Engine::derivePath(const Bip32Node* master, const uint32_t* path, size_t depth, Bip32Node* outNode) {
    if (!master || !path || depth == 0 || !outNode) return false;

    Bip32Node current = *master;
    for (size_t i = 0; i < depth; i++) {
        Bip32Node next;
        if (!deriveChild(&current, path[i], &next)) {
            return false;
        }
        current = next;
    }
    *outNode = current;
    return true;
}

// ─── 4. Bitcoin Native SegWit Bech32 (BIP-84: m/84'/0'/0'/0/0) ───────────────
bool Bip32Engine::deriveBtcSegwitAddress(const uint8_t seed[64], char outAddr[64], uint8_t outPrivKey[32]) {
    Bip32Node master;
    if (!initMasterNode(seed, &master)) return false;

    // m/84'/0'/0'/0/0
    const uint32_t path[5] = {
        84 | 0x80000000,
        0  | 0x80000000,
        0  | 0x80000000,
        0,
        0
    };

    Bip32Node child;
    if (!derivePath(&master, path, 5, &child)) return false;

    if (outPrivKey) memcpy(outPrivKey, child.privKey, 32);

    // HASH160: RIPEMD160(SHA256(pubKeyCompressed))
    uint8_t shaOut[32];
    mbedtls_sha256(child.pubKeyCompressed, 33, shaOut, 0);

    uint8_t hash160[20];
    mbedtls_ripemd160(shaOut, 32, hash160);

    bech32Encode("bc", hash160, 20, outAddr);
    secureZero(&master, sizeof(master));
    secureZero(&child, sizeof(child));
    return true;
}

// ─── 5. Ethereum EVM Address (BIP-44: m/44'/60'/0'/0/0) ──────────────────────
bool Bip32Engine::deriveEthAddress(const uint8_t seed[64], char outAddr[64], uint8_t outPrivKey[32]) {
    Bip32Node master;
    if (!initMasterNode(seed, &master)) return false;

    // m/44'/60'/0'/0/0
    const uint32_t path[5] = {
        44 | 0x80000000,
        60 | 0x80000000,
        0  | 0x80000000,
        0,
        0
    };

    Bip32Node child;
    if (!derivePath(&master, path, 5, &child)) return false;

    if (outPrivKey) memcpy(outPrivKey, child.privKey, 32);

    // Keccak-256 of uncompressed public key (64 bytes, skipping initial 0x04)
    uint8_t hash[32];
    keccak256(child.pubKeyUncompressed + 1, 64, hash);

    // Last 20 bytes is raw address -> EIP-55 Checksum
    eip55Encode(hash + 12, outAddr);

    secureZero(&master, sizeof(master));
    secureZero(&child, sizeof(child));
    return true;
}

// ─── 6. Solana Address (SLIP-0010 Ed25519: m/44'/501'/0'/0') ────────────────
bool Bip32Engine::deriveSolAddress(const uint8_t seed[64], char outAddr[64], uint8_t outPrivKey[32]) {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    uint8_t hmacOut[64];

    // Master node for Ed25519 uses key "ed25519 seed" per SLIP-0010
    int ret = mbedtls_md_hmac(mdInfo,
                              (const unsigned char*)"ed25519 seed", 12,
                              seed, 64,
                              hmacOut);
    if (ret != 0) return false;

    uint8_t k[32];
    uint8_t c[32];
    memcpy(k, hmacOut, 32);
    memcpy(c, hmacOut + 32, 32);
    secureZero(hmacOut, sizeof(hmacOut));

    // SLIP-0010 path for Solana: m/44'/501'/0'/0' (all hardened indices)
    const uint32_t solPath[4] = {
        44  | 0x80000000,
        501 | 0x80000000,
        0   | 0x80000000,
        0   | 0x80000000
    };

    for (int step = 0; step < 4; step++) {
        uint32_t index = solPath[step];
        // Hardened child derivation data: 0x00 || parent_k || ser32(index) (37 bytes)
        uint8_t data[37];
        data[0] = 0x00;
        memcpy(data + 1, k, 32);
        data[33] = (index >> 24) & 0xFF;
        data[34] = (index >> 16) & 0xFF;
        data[35] = (index >> 8) & 0xFF;
        data[36] = index & 0xFF;

        ret = mbedtls_md_hmac(mdInfo, c, 32, data, sizeof(data), hmacOut);
        secureZero(data, sizeof(data));
        if (ret != 0) {
            secureZero(k, sizeof(k));
            secureZero(c, sizeof(c));
            secureZero(hmacOut, sizeof(hmacOut));
            return false;
        }

        // In SLIP-0010 for Ed25519: child_k = I_L, child_c = I_R
        memcpy(k, hmacOut, 32);
        memcpy(c, hmacOut + 32, 32);
        secureZero(hmacOut, sizeof(hmacOut));
    }

    if (outPrivKey) memcpy(outPrivKey, k, 32);

    uint8_t pubKey[32];
    Ed25519::derivePublicKey(pubKey, k);

    base58Encode(pubKey, 32, outAddr, 64);
    secureZero(k, sizeof(k));
    secureZero(c, sizeof(c));
    return true;
}

// ─── Helper: EIP-55 Checksum Encoder ──────────────────────────────────────────
void Bip32Engine::eip55Encode(const uint8_t rawAddr[20], char* outStr) {
    char hex[41];
    for (int i = 0; i < 20; i++) {
        sprintf(hex + (i * 2), "%02x", rawAddr[i]);
    }
    hex[40] = '\0';

    uint8_t hash[32];
    keccak256((const uint8_t*)hex, 40, hash);

    outStr[0] = '0';
    outStr[1] = 'x';
    for (int i = 0; i < 40; i++) {
        uint8_t hashByte = hash[i / 2];
        uint8_t nibble = (i % 2 == 0) ? (hashByte >> 4) : (hashByte & 0x0F);
        if (nibble >= 8 && hex[i] >= 'a' && hex[i] <= 'f') {
            outStr[2 + i] = toupper(hex[i]);
        } else {
            outStr[2 + i] = hex[i];
        }
    }
    outStr[42] = '\0';
}

// ─── Helper: BIP-173 Bech32 Encoder (Native SegWit) ───────────────────────────
static const char* BECH32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static uint32_t bech32PolymodStep(uint32_t pre) {
    uint8_t b = pre >> 25;
    return ((pre & 0x1FFFFFF) << 5) ^
        (-((b >> 0) & 1) & 0x3b6a57b2UL) ^
        (-((b >> 1) & 1) & 0x26508e6dUL) ^
        (-((b >> 2) & 1) & 0x1ea119faUL) ^
        (-((b >> 3) & 1) & 0x3d4233ddUL) ^
        (-((b >> 4) & 1) & 0x2a1462b3UL);
}

void Bip32Engine::bech32Encode(const char* hrp, const uint8_t* witnessProg, size_t progLen, char* outStr) {
    uint8_t values[65];
    size_t valLen = 0;

    // Witness version 0
    values[valLen++] = 0;

    // Convert 8-bit to 5-bit
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < progLen; i++) {
        acc = (acc << 8) | witnessProg[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            values[valLen++] = (acc >> bits) & 31;
        }
    }
    if (bits > 0) {
        values[valLen++] = (acc << (5 - bits)) & 31;
    }

    // Polymod over HRP
    uint32_t chk = 1;
    for (size_t i = 0; hrp[i]; i++) {
        chk = bech32PolymodStep(chk) ^ (hrp[i] >> 5);
    }
    chk = bech32PolymodStep(chk);
    for (size_t i = 0; hrp[i]; i++) {
        chk = bech32PolymodStep(chk) ^ (hrp[i] & 0x1f);
    }

    for (size_t i = 0; i < valLen; i++) {
        chk = bech32PolymodStep(chk) ^ values[i];
    }
    for (size_t i = 0; i < 6; i++) {
        chk = bech32PolymodStep(chk);
    }
    chk ^= 1;

    // Assemble string: hrp + '1' + data + checksum
    size_t outIdx = 0;
    for (size_t i = 0; hrp[i]; i++) {
        outStr[outIdx++] = hrp[i];
    }
    outStr[outIdx++] = '1';
    for (size_t i = 0; i < valLen; i++) {
        outStr[outIdx++] = BECH32_CHARSET[values[i]];
    }
    for (size_t i = 0; i < 6; i++) {
        outStr[outIdx++] = BECH32_CHARSET[(chk >> ((5 - i) * 5)) & 31];
    }
    outStr[outIdx] = '\0';
}

// ─── Helper: Base58 Encoder (Solana) ──────────────────────────────────────────
static const char* B58_DIGITS = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

void Bip32Engine::base58Encode(const uint8_t* data, size_t len, char* outStr, size_t maxLen) {
    uint8_t digits[128] = {0};
    size_t digitsLen = 1;

    for (size_t i = 0; i < len; i++) {
        uint32_t carry = data[i];
        for (size_t j = 0; j < digitsLen; j++) {
            carry += (uint32_t)(digits[j]) << 8;
            digits[j] = carry % 58;
            carry /= 58;
        }
        while (carry > 0) {
            digits[digitsLen++] = carry % 58;
            carry /= 58;
        }
    }

    size_t zeroes = 0;
    while (zeroes < len && data[zeroes] == 0) zeroes++;

    size_t outIdx = 0;
    for (size_t i = 0; i < zeroes && outIdx < maxLen - 1; i++) {
        outStr[outIdx++] = '1';
    }
    for (size_t i = 0; i < digitsLen && outIdx < maxLen - 1; i++) {
        outStr[outIdx++] = B58_DIGITS[digits[digitsLen - 1 - i]];
    }
    outStr[outIdx] = '\0';
}
