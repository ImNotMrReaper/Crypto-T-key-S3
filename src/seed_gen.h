/**
 * seed_gen.h — Hybrid Entropy BIP-39 Seed Generation Engine
 * ==============================================================================
 * Combines ESP32-S3 Hardware TRNG with physical button timing jitter (human entropy)
 * conditioned through SHA-256 to generate verifiable 12-word or 24-word BIP-39 mnemonics.
 */

#pragma once

#include <Arduino.h>

class SeedGenerator {
public:
    static void init();

    // Entropy Accumulation
    static void resetEntropy();
    static void recordButtonPressJitter(uint32_t pressDurationUs, uint32_t timeSinceLastPressUs);
    static int  getEntropySampleCount();
    static bool isEntropySufficient(); // True when >= 12 physical samples collected

    // BIP-39 Mnemonic Generation
    static bool generateMnemonic12Words(char* outMnemonic, size_t maxLen);
    static bool generateMnemonic24Words(char* outMnemonic, size_t maxLen);
    static const char* getWordFromIndex(uint16_t index);
    static uint16_t getWordIndex(const char* word);

private:
    static uint8_t  _entropyPool[64];
    static uint32_t _sampleCount;
    static uint32_t _lastPressTimeUs;
};
