/**
 * crypto_wallet.cpp — Universal Multi-Currency Embedded Crypto Vault Implementation
 */

#include "crypto_p256.h"
#include "crypto_wallet.h"
#include "seed_gen.h"
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>

// Seed at rest: AES-256-GCM under a random per-device key, NVS namespace "wallet_seed"
// ("dev_key", "seed" = nonce || ciphertext || tag, "addr_*" = cached public addresses).
// Confidentiality against a raw flash dump needs ESP32 flash + NVS encryption (phase 2);
// this layer keeps the seed out of plaintext and binds it to this device's key.
static const char* SEED_AAD = "tkey-s3-seed-v1";
static const char* ADDR_KEYS[COIN_COUNT] = {"addr_btc", "addr_eth", "addr_sol", "addr_doge"};

static bool walletDeviceKey(uint8_t key[32]) {
    Preferences prefs;
    prefs.begin("wallet_seed", false);
    bool ok = prefs.getBytesLength("dev_key") == 32 && prefs.getBytes("dev_key", key, 32) == 32;
    if (!ok) {
        CryptoP256::secureRandom(key, 32);
        ok = prefs.putBytes("dev_key", key, 32) == 32;
    }
    prefs.end();
    return ok;
}

// Key derivation at 80 MHz takes ~6 s; run it at full speed and drop back afterwards.
struct CpuBoost {
    uint32_t prev;
    CpuBoost() : prev(getCpuFrequencyMhz()) { setCpuFrequencyMhz(240); }
    ~CpuBoost() { setCpuFrequencyMhz(prev); }
};
#include <uECC.h>
#include <SHA256.h>
#include <SHA3.h>
#include <Ed25519.h>

static int rng_wrapper(uint8_t *dest, unsigned size) {
    CryptoP256::secureRandom(dest, size);
    return 1;
}

void CryptoWallet::secureZero(void* ptr, size_t len) {
    if (!ptr) return;
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    while (len--) *p++ = 0;
}

void CryptoWallet::begin() {
    uECC_set_rng(&rng_wrapper);

    secureZero(_mnemonic, sizeof(_mnemonic));

    // Initialize accounts metadata
    _accounts[COIN_BTC].coin = COIN_BTC;
    _accounts[COIN_BTC].symbol = "BTC";
    _accounts[COIN_BTC].name = "Bitcoin SegWit";
    _accounts[COIN_BTC].derivationPath = "m/84'/0'/0'/0/0";

    _accounts[COIN_ETH].coin = COIN_ETH;
    _accounts[COIN_ETH].symbol = "ETH";
    _accounts[COIN_ETH].name = "Ethereum / EVM";
    _accounts[COIN_ETH].derivationPath = "m/44'/60'/0'/0/0";

    _accounts[COIN_SOL].coin = COIN_SOL;
    _accounts[COIN_SOL].symbol = "SOL";
    _accounts[COIN_SOL].name = "Solana Network";
    _accounts[COIN_SOL].derivationPath = "m/44'/501'/0'/0'";

    _accounts[COIN_DOGE].coin = COIN_DOGE;
    _accounts[COIN_DOGE].symbol = "DOGE";
    _accounts[COIN_DOGE].name = "Dogecoin";
    _accounts[COIN_DOGE].derivationPath = "m/44'/3'/0'/0/0";
    
    // Public addresses come from the cache; keys are only derived after a PIN unlock.
    Preferences prefs;
    prefs.begin("wallet_seed", true);
    _hasSeed = prefs.getBytesLength("seed") > 28;
    for (int i = 0; i < COIN_COUNT; i++) {
        _accounts[i].address[0] = '\0';
        if (_hasSeed) prefs.getString(ADDR_KEYS[i], _accounts[i].address, sizeof(_accounts[i].address));
    }
    for (int f = 0; f < FAM_COUNT; f++) {
        char key[8];
        snprintf(key, sizeof(key), "fa_%d", f);
        _famAddr[f][0] = '\0';
        if (_hasSeed) prefs.getString(key, _famAddr[f], FAMILY_ADDR_LEN);
    }
    prefs.end();
    // Pre-family firmware cached only the four signing accounts
    const WalletFamily LEGACY_FAM[COIN_COUNT] = {FAM_BTC, FAM_EVM, FAM_SOL, FAM_DOGE};
    for (int i = 0; i < COIN_COUNT; i++) {
        if (_hasSeed && !_famAddr[LEGACY_FAM[i]][0] && _accounts[i].address[0]) {
            strncpy(_famAddr[LEGACY_FAM[i]], _accounts[i].address, FAMILY_ADDR_LEN - 1);
            _famAddr[LEGACY_FAM[i]][FAMILY_ADDR_LEN - 1] = '\0';
        }
    }
    if (_hasSeed) publishAddresses();
    Serial.printf("[WALLET] %s\n", _hasSeed ? "Seed present (encrypted). Locked." : "No wallet seed yet: create one from the vault menu.");
}

bool CryptoWallet::unlock() {
    if (!_hasSeed || !loadSeed()) return false;
    {
        CpuBoost boost;
        deriveAllAccounts();
    }
    _isUnlocked = true;
    return true;
}

void CryptoWallet::lock() {
    _isUnlocked = false;
    // Wipe sensitive key material from RAM (public addresses stay cached)
    for (int i = 0; i < COIN_COUNT; i++) {
        secureZero(_accounts[i].privKey, sizeof(_accounts[i].privKey));
    }
    secureZero(_masterSeed, sizeof(_masterSeed));
    secureZero(_mnemonic, sizeof(_mnemonic));
    SeedGenerator::wipe();
}

bool CryptoWallet::storeSeed() {
    uint8_t key[32] = {0};
    uint8_t blob[12 + sizeof(_mnemonic) + 16] = {0};
    if (!walletDeviceKey(key)) {
        mbedtls_platform_zeroize(key, sizeof(key));
        mbedtls_platform_zeroize(blob, sizeof(blob));
        return false;
    }
    size_t n = strlen(_mnemonic);
    CryptoP256::secureRandom(blob, 12);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, n, blob, 12,
                                       (const uint8_t*)SEED_AAD, strlen(SEED_AAD),
                                       (const uint8_t*)_mnemonic, blob + 12, 16, blob + 12 + n);
    }
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(&gcm, sizeof(gcm));
    mbedtls_platform_zeroize(key, sizeof(key));
    if (rc != 0) {
        mbedtls_platform_zeroize(blob, sizeof(blob));
        return false;
    }

    Preferences prefs;
    prefs.begin("wallet_seed", false);
    bool ok = prefs.putBytes("seed", blob, 12 + n + 16) == 12 + n + 16;
    for (int i = 0; i < COIN_COUNT; i++) prefs.putString(ADDR_KEYS[i], _accounts[i].address);
    prefs.end();
    mbedtls_platform_zeroize(blob, sizeof(blob));
    return ok;
}

bool CryptoWallet::loadSeed() {
    uint8_t blob[12 + sizeof(_mnemonic) + 16] = {0};
    Preferences prefs;
    prefs.begin("wallet_seed", true);
    size_t len = prefs.getBytesLength("seed");
    bool ok = len > 28 && len <= sizeof(blob) && prefs.getBytes("seed", blob, len) == len;
    prefs.end();
    uint8_t key[32] = {0};
    if (!ok || !walletDeviceKey(key)) {
        mbedtls_platform_zeroize(key, sizeof(key));
        mbedtls_platform_zeroize(blob, sizeof(blob));
        secureZero(_mnemonic, sizeof(_mnemonic));
        return false;
    }

    size_t n = len - 28;
    if (n >= sizeof(_mnemonic)) {
        mbedtls_platform_zeroize(key, sizeof(key));
        mbedtls_platform_zeroize(blob, sizeof(blob));
        secureZero(_mnemonic, sizeof(_mnemonic));
        return false;
    }
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&gcm, n, blob, 12, (const uint8_t*)SEED_AAD, strlen(SEED_AAD),
                                      blob + 12 + n, 16, blob + 12, (uint8_t*)_mnemonic);
    }
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(&gcm, sizeof(gcm));
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_platform_zeroize(blob, sizeof(blob));
    if (rc != 0) {
        secureZero(_mnemonic, sizeof(_mnemonic));
        Serial.println("[WALLET] ❌ Stored seed failed authentication (corrupted or wrong device key)");
        return false;
    }
    _mnemonic[n] = '\0';
    return true;
}

bool CryptoWallet::isValidMnemonic(const char* phrase) {
    if (!phrase) return false;
    uint16_t idx[24];
    int count = 0;
    char buf[240];
    strncpy(buf, phrase, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    bool wordsValid = true;
    for (char* w = strtok(buf, " "); w; w = strtok(nullptr, " ")) {
        if (count >= 24) {
            wordsValid = false;
            break;
        }
        uint16_t i = SeedGenerator::getWordIndex(w);
        if (i == 0 && strcmp(w, "abandon") != 0) {  // index 0 doubles as "not found"
            wordsValid = false;
            break;
        }
        idx[count++] = i;
    }
    if (!wordsValid || (count != 12 && count != 24)) {
        secureZero(buf, sizeof(buf));
        secureZero(idx, sizeof(idx));
        return false;
    }

    // 11 bits per word = entropy || checksum (entropy bits / 32)
    uint8_t bits[33] = {0};
    for (int w = 0; w < count; w++) {
        for (int b = 0; b < 11; b++) {
            if (idx[w] & (1 << (10 - b))) {
                int pos = w * 11 + b;
                bits[pos / 8] |= 0x80 >> (pos % 8);
            }
        }
    }
    int entBytes = count == 12 ? 16 : 32;
    int csBits = entBytes / 4;
    uint8_t hash[32] = {0};
    int shaRc = mbedtls_sha256(bits, entBytes, hash, 0);
    uint8_t expected = hash[0] >> (8 - csBits);
    uint8_t actual = bits[entBytes] >> (8 - csBits);
    bool valid = shaRc == 0 && expected == actual;
    secureZero(bits, sizeof(bits));
    secureZero(idx, sizeof(idx));
    secureZero(buf, sizeof(buf));
    secureZero(hash, sizeof(hash));
    secureZero(&expected, sizeof(expected));
    secureZero(&actual, sizeof(actual));
    return valid;
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
    if (!phrase || strlen(phrase) >= sizeof(_mnemonic) || !isValidMnemonic(phrase)) return false;
    bool wasUnlocked = _isUnlocked;
    strncpy(_mnemonic, phrase, sizeof(_mnemonic) - 1);
    _mnemonic[sizeof(_mnemonic) - 1] = '\0';
    memset(_famAddr, 0, sizeof(_famAddr));   // a new seed never inherits old addresses
    {
        CpuBoost boost;
        deriveAllAccounts();
    }
    if (!storeSeed()) {
        Serial.println("[WALLET] ❌ Failed to persist seed");
        lock();
        return false;
    }
    _hasSeed = true;
    publishAddresses();   // derive* ran before _hasSeed was set: publish again or a first wallet shows no addresses
    _isUnlocked = true;
    if (!wasUnlocked) lock();  // keys only stay in RAM while the vault is unlocked
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
    // Failed derivations must not leave a previous wallet's private keys active.
    for (int i = 0; i < COIN_COUNT; i++) {
        mbedtls_platform_zeroize(_accounts[i].privKey, sizeof(_accounts[i].privKey));
    }
    mbedtls_platform_zeroize(_masterSeed, sizeof(_masterSeed));

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

    // Receive addresses for every wallet family the selected coins use
    deriveFamilies();
    publishAddresses();
    Serial.println("[WALLET] Addresses derived from the stored seed.");
}

// Derives the receive address of every wallet family that has an enabled coin and is not
// cached yet; drops cached addresses of families no selected coin uses any more.
void CryptoWallet::deriveFamilies() {
    for (int f = 0; f < FAM_COUNT; f++) {
        WalletFamily fam = (WalletFamily)f;
        if (!CryptoCoinRegistry::isFamilyInUse(fam)) {
            _famAddr[f][0] = '\0';
        } else if (!_famAddr[f][0]) {
            if (!WalletFamilies::deriveAddress(fam, _masterSeed, _famAddr[f])) _famAddr[f][0] = '\0';
        }
    }
    saveFamilyCache();
}

void CryptoWallet::saveFamilyCache() {
    Preferences prefs;
    if (!prefs.begin("wallet_seed", false)) return;
    for (int f = 0; f < FAM_COUNT; f++) {
        char key[8];
        snprintf(key, sizeof(key), "fa_%d", f);
        if (_famAddr[f][0]) {
            if (prefs.getString(key, "") != _famAddr[f]) prefs.putString(key, _famAddr[f]);
        } else if (prefs.isKey(key)) {
            prefs.remove(key);
        }
    }
    prefs.end();
}

void CryptoWallet::refreshFamilies() {
    if (!_hasSeed) return;
    if (_isUnlocked) {
        CpuBoost boost;
        deriveFamilies();
    } else {
        for (int f = 0; f < FAM_COUNT; f++) {
            if (!CryptoCoinRegistry::isFamilyInUse((WalletFamily)f)) _famAddr[f][0] = '\0';
        }
        saveFamilyCache();
    }
    publishAddresses();
}

// Pushes the (public) receive addresses into the coin registry: only selected coins get one.
void CryptoWallet::publishAddresses() {
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
        const char* a = (c->enabled && _hasSeed) ? _famAddr[c->meta->family] : "";
        strncpy(c->address, a, sizeof(c->address) - 1);
        c->address[sizeof(c->address) - 1] = '\0';
    }
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
    mbedtls_platform_zeroize(acc.privKey, sizeof(acc.privKey));
    Bip32Node master = {};
    Bip32Node child = {};
    uint8_t shaOut[32] = {0};
    uint8_t hash160[20] = {0};
    uint8_t payload[25] = {0};
    uint8_t sha1[32] = {0};
    uint8_t sha2[32] = {0};
    const uint32_t path[5] = { 44 | 0x80000000, 3 | 0x80000000, 0 | 0x80000000, 0, 0 };
    if (Bip32Engine::initMasterNode(_masterSeed, &master) &&
        Bip32Engine::derivePath(&master, path, 5, &child)) {
        memcpy(acc.privKey, child.privKey, 32);
        memcpy(acc.pubKey, child.pubKeyCompressed, 33);
        mbedtls_sha256(child.pubKeyCompressed, 33, shaOut, 0);
        mbedtls_ripemd160(shaOut, 32, hash160);
        payload[0] = 0x1E; // Doge version byte
        memcpy(payload + 1, hash160, 20);
        mbedtls_sha256(payload, 21, sha1, 0);
        mbedtls_sha256(sha1, 32, sha2, 0);
        memcpy(payload + 21, sha2, 4);
        Bip32Engine::base58Encode(payload, 25, acc.address, sizeof(acc.address));
    }
    mbedtls_platform_zeroize(&master, sizeof(master));
    mbedtls_platform_zeroize(&child, sizeof(child));
    mbedtls_platform_zeroize(shaOut, sizeof(shaOut));
    mbedtls_platform_zeroize(hash160, sizeof(hash160));
    mbedtls_platform_zeroize(payload, sizeof(payload));
    mbedtls_platform_zeroize(sha1, sizeof(sha1));
    mbedtls_platform_zeroize(sha2, sizeof(sha2));
}

#include "psbt_core.h"

// BIP-32 node at `path` (depth 0 = the master node itself)
static bool deriveNode(const uint8_t seed[64], const uint32_t* path, size_t depth, Bip32Node* out) {
    Bip32Node master;
    bool ok = Bip32Engine::initMasterNode(seed, &master);
    if (ok && depth == 0) memcpy(out, &master, sizeof(master));
    else if (ok) ok = Bip32Engine::derivePath(&master, path, depth, out);
    mbedtls_platform_zeroize(&master, sizeof(master));
    return ok;
}

bool CryptoWallet::btcMasterFingerprint(uint8_t fp[4]) const {
    if (!_isUnlocked) return false;
    Bip32Node master;
    if (!deriveNode(_masterSeed, nullptr, 0, &master)) return false;
    uint8_t sha[32], h160[20];
    mbedtls_sha256(master.pubKeyCompressed, 33, sha, 0);
    mbedtls_ripemd160(sha, 32, h160);
    memcpy(fp, h160, 4);   // BIP-32 fingerprint: first 4 bytes of HASH160(master public key)
    mbedtls_platform_zeroize(&master, sizeof(master));
    return true;
}

bool CryptoWallet::btcDerivePubkey(const uint32_t* path, size_t depth, uint8_t pub33[33]) const {
    if (!_isUnlocked || !path || depth == 0 || depth > 8) return false;
    Bip32Node node;
    bool ok = deriveNode(_masterSeed, path, depth, &node);
    if (ok) memcpy(pub33, node.pubKeyCompressed, 33);
    mbedtls_platform_zeroize(&node, sizeof(node));
    return ok;
}

static int psbtBlindRng(void*, unsigned char* out, size_t len) {
    CryptoP256::secureRandom(out, len);
    return 0;
}

bool CryptoWallet::btcSignDigest(const uint32_t* path, size_t depth, const uint8_t digest[32],
                                 uint8_t* der, size_t derCapacity, size_t* derLength) const {
    if (!_isUnlocked || !path || depth == 0 || depth > 8) return false;
    Bip32Node node;
    bool ok = deriveNode(_masterSeed, path, depth, &node) &&
              PsbtCore::signMbedTlsSecp256k1(node.privKey, digest, psbtBlindRng, nullptr, der, derCapacity, derLength);
    mbedtls_platform_zeroize(&node, sizeof(node));
    return ok;
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
    // Fail closed (audit W2): this used to sign SHA-256(recipient || amount) as display strings,
    // which authorizes nothing on-chain and could be mistaken for a transaction signature.
    // Real EIP-155 / EIP-1559 signing over the exact decoded bytes comes in Phase C2.
    if (outSigHex && maxLen) outSigHex[0] = '\0';
    _currentTx.verified = false;
    Serial.println("[WALLET] Transaction signing for this chain isn't implemented yet; nothing was signed.");
    return false;
}

void CryptoWallet::cancelSign() {
    _currentTx.verified = false;
    memset(&_currentTx, 0, sizeof(_currentTx));
}
