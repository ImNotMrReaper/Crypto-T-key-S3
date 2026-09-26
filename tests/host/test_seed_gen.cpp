// BIP-39 seed generator unit tests.
#include "test.h"
#include "seed_gen.h"
#include "crypto_p256.h"
#include "bip39_words.h"
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>
#include <string.h>

// Deterministic host stand-in for the hardware TRNG entry point.
void CryptoP256::secureRandom(uint8_t* out, size_t len) {
    static uint32_t state = 0x6D2B79F5u;
    for (size_t i = 0; i < len; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        out[i] = (uint8_t)state;
    }
}

static bool validMnemonicAndChecksum(const char* phrase, size_t expectedWords) {
    char copy[240];
    strncpy(copy, phrase, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    uint16_t indices[24] = {};
    size_t count = 0;
    for (char* word = strtok(copy, " "); word; word = strtok(nullptr, " ")) {
        if (count >= 24) return false;
        uint16_t index = SeedGenerator::getWordIndex(word);
        if (index >= 2048 || strcmp(SeedGenerator::getWordFromIndex(index), word) != 0) return false;
        indices[count++] = index;
    }
    if (count != expectedWords) return false;

    uint8_t bits[33] = {};
    for (size_t w = 0; w < count; w++) {
        for (size_t b = 0; b < 11; b++) {
            if (indices[w] & (1u << (10 - b))) {
                size_t pos = w * 11 + b;
                bits[pos / 8] |= (uint8_t)(0x80u >> (pos % 8));
            }
        }
    }
    size_t entropyBytes = expectedWords == 12 ? 16 : 32;
    size_t entropyBits = entropyBytes * 8;
    size_t checksumBits = entropyBytes / 4;
    uint8_t digest[32] = {};
    bool ok = mbedtls_sha256(bits, entropyBytes, digest, 0) == 0;
    for (size_t b = 0; ok && b < checksumBits; b++) {
        uint8_t actual = (bits[(entropyBits + b) / 8] >> (7 - ((entropyBits + b) % 8))) & 1;
        uint8_t expected = (digest[b / 8] >> (7 - (b % 8))) & 1;
        if (actual != expected) ok = false;
    }
    mbedtls_platform_zeroize(bits, sizeof(bits));
    mbedtls_platform_zeroize(indices, sizeof(indices));
    mbedtls_platform_zeroize(digest, sizeof(digest));
    mbedtls_platform_zeroize(copy, sizeof(copy));
    return ok;
}

TEST(generated_mnemonics_have_valid_word_count_and_checksum) {
    char phrase12[240] = {};
    char phrase24[240] = {};
    SeedGenerator::resetEntropy();
    CHECK(SeedGenerator::generateMnemonic12Words(phrase12, sizeof(phrase12)));
    CHECK(validMnemonicAndChecksum(phrase12, 12));
    CHECK(SeedGenerator::entropyPoolIsZeroForTest());

    SeedGenerator::resetEntropy();
    CHECK(SeedGenerator::generateMnemonic24Words(phrase24, sizeof(phrase24)));
    CHECK(validMnemonicAndChecksum(phrase24, 24));
    CHECK(SeedGenerator::entropyPoolIsZeroForTest());
    mbedtls_platform_zeroize(phrase12, sizeof(phrase12));
    mbedtls_platform_zeroize(phrase24, sizeof(phrase24));
}

TEST(successive_generations_differ) {
    char first[240] = {};
    char second[240] = {};
    SeedGenerator::resetEntropy();
    CHECK(SeedGenerator::generateMnemonic12Words(first, sizeof(first)));
    SeedGenerator::resetEntropy();
    CHECK(SeedGenerator::generateMnemonic12Words(second, sizeof(second)));
    CHECK(strcmp(first, second) != 0);
    CHECK(SeedGenerator::entropyPoolIsZeroForTest());
    mbedtls_platform_zeroize(first, sizeof(first));
    mbedtls_platform_zeroize(second, sizeof(second));
}

TEST(wipe_is_idempotent_and_failed_generation_wipes) {
    SeedGenerator::init();
    CHECK(SeedGenerator::entropyPoolIsZeroForTest());
    SeedGenerator::resetEntropy();
    CHECK(!SeedGenerator::entropyPoolIsZeroForTest());
    SeedGenerator::wipe();
    CHECK(SeedGenerator::entropyPoolIsZeroForTest());
    CHECK(SeedGenerator::getEntropySampleCount() == 0);
    SeedGenerator::wipe();
    CHECK(SeedGenerator::entropyPoolIsZeroForTest());
    char tooSmall[8] = {};
    SeedGenerator::resetEntropy();
    CHECK(!SeedGenerator::entropyPoolIsZeroForTest());
    CHECK(!SeedGenerator::generateMnemonic12Words(tooSmall, sizeof(tooSmall)));
    CHECK(SeedGenerator::entropyPoolIsZeroForTest());
    SeedGenerator::resetEntropy();
    CHECK(!SeedGenerator::entropyPoolIsZeroForTest());
    CHECK(!SeedGenerator::generateMnemonic24Words(tooSmall, sizeof(tooSmall)));
    CHECK(SeedGenerator::entropyPoolIsZeroForTest());
}

// Lead addition: a wiped pool (resetEntropy() never called) must not produce a seed
TEST(unseeded_pool_is_refused) {
    char phrase[240];
    SeedGenerator::wipe();
    CHECK(!SeedGenerator::generateMnemonic12Words(phrase, sizeof(phrase)));
    CHECK(phrase[0] == '\0');
    SeedGenerator::wipe();
    CHECK(!SeedGenerator::generateMnemonic24Words(phrase, sizeof(phrase)));
    CHECK(phrase[0] == '\0');
}

int main() {
    RUN(generated_mnemonics_have_valid_word_count_and_checksum);
    RUN(successive_generations_differ);
    RUN(wipe_is_idempotent_and_failed_generation_wipes);
    RUN(unseeded_pool_is_refused);
    DONE();
}
