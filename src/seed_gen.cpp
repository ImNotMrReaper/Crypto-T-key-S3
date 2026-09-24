/**
 * seed_gen.cpp — Implementation of Hybrid Entropy BIP-39 Seed Generator
 */

#include "crypto_p256.h"
#include "seed_gen.h"
#include "bip39_words.h"
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>

uint8_t  SeedGenerator::_entropyPool[64] = {0};
uint32_t SeedGenerator::_sampleCount = 0;
uint32_t SeedGenerator::_lastPressTimeUs = 0;

void SeedGenerator::init() {
    resetEntropy();
}

void SeedGenerator::resetEntropy() {
    // 1. Seed base pool with hardware TRNG
    CryptoP256::secureRandom(_entropyPool, sizeof(_entropyPool));
    _sampleCount = 0;
    _lastPressTimeUs = micros();
}

void SeedGenerator::recordButtonPressJitter(uint32_t pressDurationUs, uint32_t timeSinceLastPressUs) {
    uint32_t nowUs = micros();
    uint8_t jitterData[12];
    memcpy(jitterData, &pressDurationUs, 4);
    memcpy(jitterData + 4, &timeSinceLastPressUs, 4);
    memcpy(jitterData + 8, &nowUs, 4);

    // Mix jitter into entropy pool with SHA-256
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, _entropyPool, sizeof(_entropyPool));
    mbedtls_sha256_update(&ctx, jitterData, sizeof(jitterData));
    mbedtls_sha256_finish(&ctx, _entropyPool);
    mbedtls_sha256_free(&ctx);

    // Also fill second half with fresh hardware TRNG
    CryptoP256::secureRandom(_entropyPool + 32, 32);

    _sampleCount++;
    _lastPressTimeUs = nowUs;
    mbedtls_platform_zeroize(jitterData, sizeof(jitterData));
}

int SeedGenerator::getEntropySampleCount() {
    return _sampleCount;
}

bool SeedGenerator::isEntropySufficient() {
    return (_sampleCount >= 12);
}

const char* SeedGenerator::getWordFromIndex(uint16_t index) {
    if (index < 2048) {
        return BIP39_WORDLIST[index];
    }
    return "abandon";
}

uint16_t SeedGenerator::getWordIndex(const char* word) {
    if (!word) return 0;
    for (uint16_t i = 0; i < 2048; i++) {
        if (strcmp(BIP39_WORDLIST[i], word) == 0) return i;
    }
    return 0;
}

bool SeedGenerator::generateMnemonic12Words(char* outMnemonic, size_t maxLen) {
    if (!outMnemonic || maxLen < 120) return false;

    // 1. Take 128-bit (16-byte) conditioned entropy
    uint8_t finalEntropy[32];
    mbedtls_sha256(_entropyPool, sizeof(_entropyPool), finalEntropy, 0);

    uint8_t ent[16];
    memcpy(ent, finalEntropy, 16);

    // 2. Compute 4-bit checksum: SHA-256(ent)[0] >> 4
    uint8_t hash[32];
    mbedtls_sha256(ent, 16, hash, 0);
    uint8_t cs = hash[0] >> 4; // 4 bits

    // 3. Assemble 132 bits into 12 11-bit indices
    // Buffer with 18 bytes (padded with 0 to safely allow byteIdx + 2 read)
    uint8_t bits[18];
    memset(bits, 0, sizeof(bits));
    memcpy(bits, ent, 16);
    bits[16] = hash[0];

    outMnemonic[0] = '\0';
    for (int i = 0; i < 12; i++) {
        int bitOffset = i * 11;
        int byteIdx = bitOffset / 8;
        int bitRemainder = bitOffset % 8;

        uint32_t val = ((uint32_t)bits[byteIdx] << 16) |
                       ((uint32_t)bits[byteIdx + 1] << 8) |
                       ((uint32_t)bits[byteIdx + 2]);

        uint16_t wordIdx = (val >> (24 - 11 - bitRemainder)) & 0x07FF;

        const char* word = getWordFromIndex(wordIdx);
        if (i > 0) strncat(outMnemonic, " ", maxLen - strlen(outMnemonic) - 1);
        strncat(outMnemonic, word, maxLen - strlen(outMnemonic) - 1);
    }

    // Zeroize sensitive stack memory
    mbedtls_platform_zeroize(finalEntropy, sizeof(finalEntropy));
    mbedtls_platform_zeroize(ent, sizeof(ent));
    mbedtls_platform_zeroize(hash, sizeof(hash));
    mbedtls_platform_zeroize(bits, sizeof(bits));
    return true;
}

bool SeedGenerator::generateMnemonic24Words(char* outMnemonic, size_t maxLen) {
    if (!outMnemonic || maxLen < 240) return false;

    // 1. Take 256-bit (32-byte) conditioned entropy
    uint8_t ent[32];
    mbedtls_sha256(_entropyPool, sizeof(_entropyPool), ent, 0);

    // 2. Compute 8-bit checksum: SHA-256(ent)[0]
    uint8_t hash[32];
    mbedtls_sha256(ent, 32, hash, 0);
    uint8_t cs = hash[0]; // 8 bits

    // 3. Assemble 264 bits into 24 11-bit indices
    // Buffer with 35 bytes (padded with 0 to safely allow byteIdx + 2 read)
    uint8_t bits[35];
    memset(bits, 0, sizeof(bits));
    memcpy(bits, ent, 32);
    bits[32] = cs;

    outMnemonic[0] = '\0';
    for (int i = 0; i < 24; i++) {
        int bitOffset = i * 11;
        int byteIdx = bitOffset / 8;
        int bitRemainder = bitOffset % 8;

        uint32_t val = ((uint32_t)bits[byteIdx] << 16) |
                       ((uint32_t)bits[byteIdx + 1] << 8) |
                       ((uint32_t)bits[byteIdx + 2]);

        uint16_t wordIdx = (val >> (24 - 11 - bitRemainder)) & 0x07FF;

        const char* word = getWordFromIndex(wordIdx);
        if (i > 0) strncat(outMnemonic, " ", maxLen - strlen(outMnemonic) - 1);
        strncat(outMnemonic, word, maxLen - strlen(outMnemonic) - 1);
    }

    mbedtls_platform_zeroize(ent, sizeof(ent));
    mbedtls_platform_zeroize(hash, sizeof(hash));
    mbedtls_platform_zeroize(bits, sizeof(bits));
    return true;
}
