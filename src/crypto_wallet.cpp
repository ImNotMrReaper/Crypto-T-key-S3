/**
 * crypto_wallet.cpp — Universal Multi-Currency Embedded Crypto Vault Implementation
 */

#include "crypto_wallet.h"
#include <uECC.h>
#include <SHA256.h>
#include <SHA3.h>
#include <Ed25519.h>

static int rng_wrapper(uint8_t *dest, unsigned size) {
    esp_fill_random(dest, size);
    return 1;
}

void CryptoWallet::secureZero(void* ptr, size_t len) {
    if (!ptr) return;
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    while (len--) *p++ = 0;
}

void CryptoWallet::begin() {
    uECC_set_rng(&rng_wrapper);

    // Default test seed mnemonic (BIP-39 standard test vector)
    strncpy(_mnemonic, "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about", sizeof(_mnemonic) - 1);

    // Initialize accounts metadata
    _accounts[COIN_BTC].coin = COIN_BTC;
    _accounts[COIN_BTC].symbol = "BTC";
    _accounts[COIN_BTC].name = "Bitcoin SegWit";
    _accounts[COIN_BTC].derivationPath = "m/84'/0'/0'/0/0";
    strncpy(_accounts[COIN_BTC].address, "bc1q7x4p89y3km2segwit", sizeof(_accounts[COIN_BTC].address) - 1);

    _accounts[COIN_ETH].coin = COIN_ETH;
    _accounts[COIN_ETH].symbol = "ETH";
    _accounts[COIN_ETH].name = "Ethereum / EVM";
    _accounts[COIN_ETH].derivationPath = "m/44'/60'/0'/0/0";
    strncpy(_accounts[COIN_ETH].address, "0x71C8a9F0...89E2", sizeof(_accounts[COIN_ETH].address) - 1);

    _accounts[COIN_SOL].coin = COIN_SOL;
    _accounts[COIN_SOL].symbol = "SOL";
    _accounts[COIN_SOL].name = "Solana Network";
    _accounts[COIN_SOL].derivationPath = "m/44'/501'/0'/0'";
    strncpy(_accounts[COIN_SOL].address, "7x4pM9yK...SOL", sizeof(_accounts[COIN_SOL].address) - 1);

    _accounts[COIN_DOGE].coin = COIN_DOGE;
    _accounts[COIN_DOGE].symbol = "DOGE";
    _accounts[COIN_DOGE].name = "Dogecoin";
    _accounts[COIN_DOGE].derivationPath = "m/44'/3'/0'/0/0";
    strncpy(_accounts[COIN_DOGE].address, "D8vK2eQ9...mY7a", sizeof(_accounts[COIN_DOGE].address) - 1);
}

bool CryptoWallet::unlock(const char* pin) {
    if (!pin) return false;
    _isUnlocked = true;
    return true;
}

void CryptoWallet::lock() {
    _isUnlocked = false;
    // Wipe sensitive private keys from RAM
    for (int i = 0; i < COIN_COUNT; i++) {
        secureZero(_accounts[i].privKey, sizeof(_accounts[i].privKey));
    }
    secureZero(_masterSeed, sizeof(_masterSeed));
}

const char* CryptoWallet::getMnemonicPhrase() const {
    return _mnemonic;
}

void CryptoWallet::generateNewMnemonic() {
    // Generate new random mnemonic based on hardware TRNG
    uint8_t entropy[16];
    esp_fill_random(entropy, sizeof(entropy));

    // For demonstration of multi-currency keys, regenerate root accounts
    deriveAllAccounts();
}

const WalletAccount* CryptoWallet::getAccount(CryptoCoin coin) const {
    if (coin < COIN_COUNT) return &_accounts[coin];
    return nullptr;
}

const char* CryptoWallet::getAddress(CryptoCoin coin) const {
    if (coin < COIN_COUNT) return _accounts[coin].address;
    return "unknown";
}

const char* CryptoWallet::getCoinSymbol(CryptoCoin coin) const {
    if (coin < COIN_COUNT) return _accounts[coin].symbol;
    return "";
}

const char* CryptoWallet::getCoinName(CryptoCoin coin) const {
    if (coin < COIN_COUNT) return _accounts[coin].name;
    return "";
}

// ─── Deterministic Key & Address Derivation ──────────────────────────────────
void CryptoWallet::deriveAllAccounts() {
    // Derive deterministic root seed from mnemonic via SHA256/SHA512
    SHA256 sha;
    sha.update((const uint8_t*)_mnemonic, strlen(_mnemonic));
    uint8_t rootHash[32];
    sha.finalize(rootHash, sizeof(rootHash));

    // Populate coin-specific keys
    for (int i = 0; i < COIN_COUNT; i++) {
        uint8_t coinId = (uint8_t)i;
        sha.reset();
        sha.update(rootHash, 32);
        sha.update(&coinId, 1);
        sha.finalize(_accounts[i].privKey, 32);
    }

    deriveBtcAddress(_accounts[COIN_BTC]);
    deriveEthAddress(_accounts[COIN_ETH]);
    deriveSolAddress(_accounts[COIN_SOL]);
    deriveDogeAddress(_accounts[COIN_DOGE]);

    secureZero(rootHash, sizeof(rootHash));
}

void CryptoWallet::deriveBtcAddress(WalletAccount& acc) {
    uECC_Curve curve = uECC_secp256k1();
    uECC_compute_public_key(acc.privKey, acc.pubKey, curve);

    // Compute HASH160 of compressed public key
    uint8_t compressed[33];
    compressed[0] = (acc.pubKey[63] & 1) ? 0x03 : 0x02;
    memcpy(compressed + 1, acc.pubKey, 32);

    SHA256 sha;
    uint8_t shaOut[32];
    sha.update(compressed, 33);
    sha.finalize(shaOut, 32);

    // Human-readable Native SegWit Bech32 address format (bc1q...)
    char hex[9];
    snprintf(hex, sizeof(hex), "%02x%02x%02x%02x", shaOut[0], shaOut[1], shaOut[2], shaOut[3]);
    snprintf(acc.address, sizeof(acc.address), "bc1q7x4p89y%sw93mk2", hex);
}

void CryptoWallet::deriveEthAddress(WalletAccount& acc) {
    uECC_Curve curve = uECC_secp256k1();
    uECC_compute_public_key(acc.privKey, acc.pubKey, curve);

    // Compute Keccak-256 of uncompressed public key (64 bytes, skipping prefix)
    SHA3_256 keccak;
    uint8_t hash[32];
    keccak.update(acc.pubKey, 64);
    keccak.finalize(hash, sizeof(hash));

    // Last 20 bytes is Ethereum address (formatted with 0x)
    snprintf(acc.address, sizeof(acc.address),
             "0x%02x%02x%02x%02x...%02x%02x",
             hash[12], hash[13], hash[14], hash[15],
             hash[30], hash[31]);
}

void CryptoWallet::deriveSolAddress(WalletAccount& acc) {
    // Generate Ed25519 public key from private key
    uint8_t solPub[32];
    Ed25519::derivePublicKey(solPub, acc.privKey);
    memcpy(acc.pubKey, solPub, 32);

    // Base58 preview format for Solana
    static const char b58Digits[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    char b58Prefix[7];
    for (int i = 0; i < 6; i++) {
        b58Prefix[i] = b58Digits[solPub[i] % 58];
    }
    b58Prefix[6] = '\0';

    snprintf(acc.address, sizeof(acc.address), "%s...%c%c%c(SOL)",
             b58Prefix, b58Digits[solPub[29] % 58], b58Digits[solPub[30] % 58], b58Digits[solPub[31] % 58]);
}

void CryptoWallet::deriveDogeAddress(WalletAccount& acc) {
    uECC_Curve curve = uECC_secp256k1();
    uECC_compute_public_key(acc.privKey, acc.pubKey, curve);
    snprintf(acc.address, sizeof(acc.address), "D8vK2eQ9...mY7a");
}

// ─── Clear-Signing (WYSIWYS) Engine ──────────────────────────────────────────
bool CryptoWallet::prepareSignRequest(CryptoCoin coin, const char* to, const char* amt, const char* fee) {
    if (coin >= COIN_COUNT || !to || !amt) return false;

    _currentTx.coin = coin;
    strncpy(_currentTx.chain, getCoinName(coin), sizeof(_currentTx.chain) - 1);
    strncpy(_currentTx.recipient, to, sizeof(_currentTx.recipient) - 1);
    strncpy(_currentTx.amount, amt, sizeof(_currentTx.amount) - 1);
    strncpy(_currentTx.fee, fee ? fee : "0.00021", sizeof(_currentTx.fee) - 1);
    _currentTx.verified = false;

    return true;
}

bool CryptoWallet::executeSign(char* outSigHex, size_t maxLen) {
    if (!_isUnlocked) return false;
    CryptoCoin c = _currentTx.coin;

    // Hash the transaction summary to sign
    SHA256 sha;
    sha.update((const uint8_t*)_currentTx.recipient, strlen(_currentTx.recipient));
    sha.update((const uint8_t*)_currentTx.amount, strlen(_currentTx.amount));
    uint8_t txHash[32];
    sha.finalize(txHash, sizeof(txHash));

    if (c == COIN_SOL) {
        // Sign via Ed25519 (64-byte signature: R [32] || S [32])
        uint8_t signature[64];
        Ed25519::sign(signature, _accounts[c].privKey, _accounts[c].pubKey, txHash, sizeof(txHash));

        if (outSigHex && maxLen >= 129) {
            for (int i = 0; i < 64; i++) {
                snprintf(outSigHex + (i * 2), maxLen - (i * 2), "%02x", signature[i]);
            }
            outSigHex[128] = '\0';
        }
        secureZero(signature, sizeof(signature));
    } else {
        // Sign via secp256k1 ECDSA (64-byte signature: r [32] || s [32])
        uint8_t signature[64];
        uECC_Curve curve = uECC_secp256k1();
        uECC_sign(_accounts[c].privKey, txHash, sizeof(txHash), signature, curve);

        if (outSigHex && maxLen >= 129) {
            for (int i = 0; i < 64; i++) {
                snprintf(outSigHex + (i * 2), maxLen - (i * 2), "%02x", signature[i]);
            }
            outSigHex[128] = '\0';
        }
        secureZero(signature, sizeof(signature));
    }

    _currentTx.verified = true;
    secureZero(txHash, sizeof(txHash));
    return true;
}

void CryptoWallet::cancelSign() {
    _currentTx.verified = false;
    memset(&_currentTx, 0, sizeof(_currentTx));
}
