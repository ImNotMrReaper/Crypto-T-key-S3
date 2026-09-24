/**
 * crypto_wallet.h — Universal Multi-Currency Embedded Crypto Vault & Signer
 * =========================================================================
 * Hardware-grade key derivation & signing engine for LilyGo T-Dongle S3.
 * Supports: Bitcoin (BTC), Ethereum (ETH / EVM), Solana (SOL), and UTXO/EVM tokens.
 */

#pragma once

#include <Arduino.h>

enum CryptoCoin {
    COIN_BTC,
    COIN_ETH,
    COIN_SOL,
    COIN_DOGE,
    COIN_COUNT
};

struct WalletAccount {
    CryptoCoin  coin;
    const char* symbol;
    const char* name;
    const char* derivationPath;
    char        address[64];
    uint8_t     pubKey[65];
    uint8_t     privKey[32]; // Ephemeral in-memory key (zeroized on lock)
};

struct ClearSignTx {
    CryptoCoin coin;
    char chain[24];
    char recipient[64];
    char amount[24];
    char fee[24];
    char memo[32];
    bool verified;
};

class CryptoWallet {
public:
    void begin();
    // Decrypts the stored seed and derives the signing keys. The caller verifies the PIN
    // first (PinVault::check). Returns false when no wallet seed has been created yet.
    bool unlock();
    void lock();
    bool isUnlocked() const { return _isUnlocked; }
    bool hasSeed() const { return _hasSeed; }

    // Validates a 12/24-word BIP-39 phrase (word list + checksum).
    static bool isValidMnemonic(const char* phrase);

    // Seed & Account APIs
    const char* getMnemonicPhrase() const;
    void generateNewMnemonic();
    // Installs and persists a new seed (AES-256-GCM in NVS) and caches its public addresses.
    bool setMnemonic(const char* phrase);
    const WalletAccount* getAccount(CryptoCoin coin) const;
    const char* getAddress(CryptoCoin coin) const;
    const char* getCoinSymbol(CryptoCoin coin) const;
    const char* getCoinName(CryptoCoin coin) const;

    // Clear-Signing (WYSIWYS) Engine
    bool prepareSignRequest(CryptoCoin coin, const char* to, const char* amt, const char* fee = "Standard");
    bool parseAndPrepareEvmTx(const uint8_t* rawTx, size_t len, void* outDecoded = nullptr);
    bool parseAndPrepareEvmHexTx(const char* hexStr, void* outDecoded = nullptr);
    const ClearSignTx& getCurrentRequest() const { return _currentTx; }
    bool executeSign(char* outSigHex, size_t maxLen);
    void cancelSign();

    // Memory Security
    static void secureZero(void* ptr, size_t len);

private:
    void deriveAllAccounts();
    void publishAddresses();
    bool storeSeed();
    bool loadSeed();
    void deriveBtcAddress(WalletAccount& acc);
    void deriveEthAddress(WalletAccount& acc);
    void deriveSolAddress(WalletAccount& acc);
    void deriveDogeAddress(WalletAccount& acc);

    bool          _isUnlocked = false;
    bool          _hasSeed = false;
    char          _mnemonic[160];
    uint8_t       _masterSeed[64];
    WalletAccount _accounts[COIN_COUNT];
    ClearSignTx   _currentTx;
};
