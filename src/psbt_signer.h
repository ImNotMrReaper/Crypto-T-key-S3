/**
 * psbt_signer.h — Air-Gapped BIP-174 PSBT Parser & Offline Bitcoin Signer
 * ==============================================================================
 * Standard: Follows hardware-wallet-dev skill (BIP-174, BIP-143, BIP-84 SegWit).
 * Hardware: LilyGo T-Dongle S3 SD_MMC 1-Bit slot (CLK=12, CMD=16, D0=17).
 */

#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include "config.h"
#include "crypto_wallet.h"

struct PsbtTxDetails {
    char fileName[64];
    char recipientAddr[64];
    uint64_t sendSatoshis;
    uint64_t feeSatoshis;
    uint64_t changeSatoshis;
    uint32_t numInputs;
    uint32_t numOutputs;
    bool isValid;
    bool isSigned;
};

class PsbtSigner {
public:
    static bool initSD();
    static void closeSD();
    static bool isCardMounted();

    // Scan for pending unsigned .psbt files in root or /psbt
    static bool findPendingPsbt(char* outPath, size_t maxLen);

    // Parse PSBT file and extract recipient, send amount, and fee
    static bool parsePsbtFile(const char* filePath, PsbtTxDetails& outDetails, const CryptoWallet& wallet);

    // Sign the PSBT file with derived SegWit key and write -signed.psbt
    static bool signPsbtFile(const char* filePath, const CryptoWallet& wallet, char* outSignedPath, size_t maxLen);

    // Helper: format satoshis to human BTC string e.g. "0.04500000 BTC"
    static void formatSatoshis(uint64_t sats, char* outBuf, size_t maxLen);

    // Low-S normalization for secp256k1 ECDSA (BIP-62 / BIP-146)
    static void normalizeLowS(uint8_t s[32]);

    // DER signature encoder
    static size_t encodeDer(const uint8_t r[32], const uint8_t s[32], uint8_t* outDer);

private:
    static bool _mounted;
    static uint64_t readVarInt(const uint8_t* buf, size_t maxLen, size_t& offset);
    static void writeVarInt(uint64_t val, uint8_t* out, size_t& offset);
    static void doubleSha256(const uint8_t* data, size_t len, uint8_t out[32]);
    static bool decodeBase64(const char* in, size_t inLen, uint8_t* out, size_t& outLen, size_t maxOut);
    static bool encodeBase64(const uint8_t* in, size_t inLen, char* out, size_t maxOut);
};
