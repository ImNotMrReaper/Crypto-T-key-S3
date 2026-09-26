/**
 * psbt_signer.h — Air-Gapped BIP-174 PSBT Parser & Offline Bitcoin Signer
 * ==============================================================================
 * Standard: Follows hardware-wallet-dev skill (BIP-174, BIP-143, BIP-84 SegWit).
 * Hardware: LilyGo T-Dongle S3 SD_MMC 1-Bit slot (CLK=12, CMD=16, D0=17).
 */

#pragma once

#define PSBT_MAX_FILE_BYTES 32768   // largest PSBT file read from MicroSD

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include "config.h"
#include "crypto_wallet.h"

#include "psbt_core.h"

// Everything the approval screens show, straight from PsbtCore::analyze()
struct PsbtTxDetails {
    char fileName[64];
    PsbtCore::Result r;      // every non-change output, change total, exact fee, input counts
    PsbtCore::Error error;
    bool isValid;
    // Legacy summary fields (serial `psbt parse`): the first external output
    char recipientAddr[PsbtCore::MAX_ADDRESS_LEN];
    uint64_t sendSatoshis;
    uint64_t feeSatoshis;
    uint64_t changeSatoshis;
    uint32_t numInputs;
    uint32_t numOutputs;
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

private:
    static bool _mounted;
    // Reads a PSBT file (binary or base64) into a malloc'd buffer the caller frees
    static uint8_t* loadPsbt(const char* filePath, size_t* len, bool* wasBase64);
    static bool fillOptions(const CryptoWallet& wallet, PsbtCore::Options* opt);
    static bool decodeBase64(const char* in, size_t inLen, uint8_t* out, size_t& outLen, size_t maxOut);
    static bool encodeBase64(const uint8_t* in, size_t inLen, char* out, size_t maxOut);
};
