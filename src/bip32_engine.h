/**
 * bip32_engine.h — Standard BIP-32 / BIP-39 / BIP-84 / EIP-55 Cryptographic Engine
 * ==============================================================================
 * Real cryptographic address derivation for Bitcoin SegWit, Ethereum, and Solana.
 * Mathematically identical to Trezor, Ledger, MetaMask, and Sparrow Wallet.
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

struct Bip32Node {
    uint8_t privKey[32];
    uint8_t chainCode[32];
    uint8_t pubKeyCompressed[33];
    uint8_t pubKeyUncompressed[65];
};

class Bip32Engine {
public:
    // 1. BIP-39 Mnemonic -> 64-byte Master Seed
    static bool mnemonicToSeed(const char* mnemonic, const char* passphrase, uint8_t outSeed[64]);

    // 2. BIP-32 Master Node Generation (HMAC-SHA512 with "Bitcoin seed")
    static bool initMasterNode(const uint8_t seed[64], Bip32Node* outNode);

    // 3. Child Key Derivation (BIP-32 Hardened / Non-Hardened)
    static bool deriveChild(const Bip32Node* parent, uint32_t index, Bip32Node* outChild);
    static bool derivePath(const Bip32Node* master, const uint32_t* path, size_t depth, Bip32Node* outNode);

    // 4. Real Address Derivations
    // Bitcoin Native SegWit: m/84'/0'/0'/0/0 -> bc1q... (BIP-84 / BIP-173 Bech32)
    static bool deriveBtcSegwitAddress(const uint8_t seed[64], char outAddr[64], uint8_t outPrivKey[32]);

    // Ethereum EVM: m/44'/60'/0'/0/0 -> 0x... (BIP-44 / EIP-55 Checksum)
    static bool deriveEthAddress(const uint8_t seed[64], char outAddr[64], uint8_t outPrivKey[32]);

    // Solana: m/44'/501'/0'/0' -> Base58 (SLIP-0010 Ed25519)
    static bool deriveSolAddress(const uint8_t seed[64], char outAddr[64], uint8_t outPrivKey[32]);

    // Helpers
    static void bech32Encode(const char* hrp, const uint8_t* witnessProg, size_t progLen, char* outStr);
    static void eip55Encode(const uint8_t rawAddr[20], char* outStr);
    static void base58Encode(const uint8_t* data, size_t len, char* outStr, size_t maxLen);
    static void secureZero(void* ptr, size_t len);
};
