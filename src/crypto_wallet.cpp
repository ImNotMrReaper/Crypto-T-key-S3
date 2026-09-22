/**
 * crypto_wallet.cpp — Universal Multi-Currency Embedded Crypto Vault Implementation
 */

#include "crypto_wallet.h"
#include "seed_gen.h"
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
    
    // Derive real BIP-32/BIP-84/EIP-55 addresses immediately from master seed
    deriveAllAccounts();
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
    // Generate new verifiable 12-word BIP-39 mnemonic via hardware TRNG
    char phrase[240] = {0};
    SeedGenerator::resetEntropy();
    if (SeedGenerator::generateMnemonic12Words(phrase, sizeof(phrase))) {
        setMnemonic(phrase);
    }
    secureZero(phrase, sizeof(phrase));
}

bool CryptoWallet::setMnemonic(const char* phrase) {
    if (!phrase || strlen(phrase) == 0 || strlen(phrase) >= sizeof(_mnemonic)) return false;
    strncpy(_mnemonic, phrase, sizeof(_mnemonic) - 1);
    _mnemonic[sizeof(_mnemonic) - 1] = '\0';
    deriveAllAccounts();
    return true;
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

#include "bip32_engine.h"
#include "crypto_coins.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ripemd160.h"

// ─── Deterministic Key & Address Derivation ──────────────────────────────────
void CryptoWallet::deriveAllAccounts() {
    // 1. Compute standard 64-byte BIP-39 binary seed from mnemonic
    Bip32Engine::mnemonicToSeed(_mnemonic, "", _masterSeed);

    // 2. Real Bitcoin Native SegWit Address (BIP-84: m/84'/0'/0'/0/0 -> bc1q...)
    Bip32Engine::deriveBtcSegwitAddress(_masterSeed, _accounts[COIN_BTC].address, _accounts[COIN_BTC].privKey);
    uECC_compute_public_key(_accounts[COIN_BTC].privKey, _accounts[COIN_BTC].pubKey, uECC_secp256k1());

    // 3. Real Ethereum EVM Address (BIP-44: m/44'/60'/0'/0/0 -> 0x... with EIP-55 Checksum)
    Bip32Engine::deriveEthAddress(_masterSeed, _accounts[COIN_ETH].address, _accounts[COIN_ETH].privKey);
    uECC_compute_public_key(_accounts[COIN_ETH].privKey, _accounts[COIN_ETH].pubKey, uECC_secp256k1());

    // 4. Real Solana Address (SLIP-0010: m/44'/501'/0'/0' -> Base58)
    Bip32Engine::deriveSolAddress(_masterSeed, _accounts[COIN_SOL].address, _accounts[COIN_SOL].privKey);
    Ed25519::derivePublicKey(_accounts[COIN_SOL].pubKey, _accounts[COIN_SOL].privKey);

    // 5. Real Dogecoin Address (P2PKH Legacy Base58)
    deriveDogeAddress(_accounts[COIN_DOGE]);

    // Update global coin registry with real deposit addresses
    CoinAsset* btcCoin = CryptoCoinRegistry::getCoin(COIN_ID_BTC);
    if (btcCoin) strncpy(btcCoin->address, _accounts[COIN_BTC].address, sizeof(btcCoin->address) - 1);

    CoinAsset* ethCoin = CryptoCoinRegistry::getCoin(COIN_ID_ETH);
    if (ethCoin) strncpy(ethCoin->address, _accounts[COIN_ETH].address, sizeof(ethCoin->address) - 1);

    CoinAsset* solCoin = CryptoCoinRegistry::getCoin(COIN_ID_SOL);
    if (solCoin) strncpy(solCoin->address, _accounts[COIN_SOL].address, sizeof(solCoin->address) - 1);

    Serial.println("[WALLET] Real Addresses Derived from BIP-39 Seed:");
    Serial.printf("[WALLET]  BTC (BIP-84): %s\n", _accounts[COIN_BTC].address);
    Serial.printf("[WALLET]  ETH (EIP-55): %s\n", _accounts[COIN_ETH].address);
    Serial.printf("[WALLET]  SOL (Base58): %s\n", _accounts[COIN_SOL].address);
    Serial.printf("[WALLET]  DOGE (B58)  : %s\n", _accounts[COIN_DOGE].address);
}

void CryptoWallet::deriveBtcAddress(WalletAccount& acc) {
    Bip32Engine::deriveBtcSegwitAddress(_masterSeed, acc.address, acc.privKey);
    uECC_compute_public_key(acc.privKey, acc.pubKey, uECC_secp256k1());
}

void CryptoWallet::deriveEthAddress(WalletAccount& acc) {
    Bip32Engine::deriveEthAddress(_masterSeed, acc.address, acc.privKey);
    uECC_compute_public_key(acc.privKey, acc.pubKey, uECC_secp256k1());
}

void CryptoWallet::deriveSolAddress(WalletAccount& acc) {
    Bip32Engine::deriveSolAddress(_masterSeed, acc.address, acc.privKey);
    Ed25519::derivePublicKey(acc.pubKey, acc.privKey);
}

void CryptoWallet::deriveDogeAddress(WalletAccount& acc) {
    Bip32Node master;
    if (!Bip32Engine::initMasterNode(_masterSeed, &master)) return;
    const uint32_t path[5] = { 44 | 0x80000000, 3 | 0x80000000, 0 | 0x80000000, 0, 0 };
    Bip32Node child;
    if (Bip32Engine::derivePath(&master, path, 5, &child)) {
        memcpy(acc.privKey, child.privKey, 32);
        memcpy(acc.pubKey, child.pubKeyCompressed, 33);
        uint8_t shaOut[32];
        mbedtls_sha256(child.pubKeyCompressed, 33, shaOut, 0);
        uint8_t hash160[20];
        mbedtls_ripemd160(shaOut, 32, hash160);
        uint8_t payload[25];
        payload[0] = 0x1E; // Doge version byte
        memcpy(payload + 1, hash160, 20);
        uint8_t sha1[32], sha2[32];
        mbedtls_sha256(payload, 21, sha1, 0);
        mbedtls_sha256(sha1, 32, sha2, 0);
        memcpy(payload + 21, sha2, 4);
        Bip32Engine::base58Encode(payload, 25, acc.address, sizeof(acc.address));
    }
}

#include "evm_decoder.h"

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

bool CryptoWallet::parseAndPrepareEvmTx(const uint8_t* rawTx, size_t len, void* outDecoded) {
    EvmDecodedTx decoded;
    if (!EvmTxDecoder::decodeTx(rawTx, len, decoded)) {
        return false;
    }

    _currentTx.coin = COIN_ETH;
    strncpy(_currentTx.chain, decoded.tokenName, sizeof(_currentTx.chain) - 1);

    if (decoded.action == ACTION_ERC20_TRANSFER || decoded.action == ACTION_ERC20_APPROVE) {
        strncpy(_currentTx.recipient, decoded.recipientOrSpender, sizeof(_currentTx.recipient) - 1);
        strncpy(_currentTx.amount, decoded.tokenAmount, sizeof(_currentTx.amount) - 1);
    } else {
        strncpy(_currentTx.recipient, decoded.toAddress, sizeof(_currentTx.recipient) - 1);
        strncpy(_currentTx.amount, decoded.valueEth, sizeof(_currentTx.amount) - 1);
    }

    strncpy(_currentTx.fee, decoded.dispFee, sizeof(_currentTx.fee) - 1);
    strncpy(_currentTx.memo, decoded.dispAction, sizeof(_currentTx.memo) - 1);
    _currentTx.verified = false;

    if (outDecoded) {
        memcpy(outDecoded, &decoded, sizeof(EvmDecodedTx));
    }
    return true;
}

bool CryptoWallet::parseAndPrepareEvmHexTx(const char* hexStr, void* outDecoded) {
    EvmDecodedTx decoded;
    if (!EvmTxDecoder::decodeHexTx(hexStr, decoded)) {
        return false;
    }

    _currentTx.coin = COIN_ETH;
    strncpy(_currentTx.chain, decoded.tokenName, sizeof(_currentTx.chain) - 1);

    if (decoded.action == ACTION_ERC20_TRANSFER || decoded.action == ACTION_ERC20_APPROVE) {
        strncpy(_currentTx.recipient, decoded.recipientOrSpender, sizeof(_currentTx.recipient) - 1);
        strncpy(_currentTx.amount, decoded.tokenAmount, sizeof(_currentTx.amount) - 1);
    } else {
        strncpy(_currentTx.recipient, decoded.toAddress, sizeof(_currentTx.recipient) - 1);
        strncpy(_currentTx.amount, decoded.valueEth, sizeof(_currentTx.amount) - 1);
    }

    strncpy(_currentTx.fee, decoded.dispFee, sizeof(_currentTx.fee) - 1);
    strncpy(_currentTx.memo, decoded.dispAction, sizeof(_currentTx.memo) - 1);
    _currentTx.verified = false;

    if (outDecoded) {
        memcpy(outDecoded, &decoded, sizeof(EvmDecodedTx));
    }
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
