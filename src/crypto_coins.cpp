/**
 * crypto_coins.cpp — Implementation of Master Cryptocurrency Asset Registry
 */

#include "crypto_coins.h"
#include <Preferences.h>

CoinAsset CryptoCoinRegistry::_coins[COIN_REGISTRY_COUNT] = {
    { COIN_ID_BTC,  "BTC",   "Bitcoin",       "m/84'/0'/0'/0/0",    CURVE_SECP256K1, true,  0.0850f, 68450.0f, +3.2f, "bc1q7x4p89y3km2segwit" },
    { COIN_ID_ETH,  "ETH",   "Ethereum",      "m/44'/60'/0'/0/0",   CURVE_SECP256K1, true,  1.4500f, 3540.0f,  +1.8f, "0x71C8a9F0...89E2" },
    { COIN_ID_SOL,  "SOL",   "Solana",        "m/44'/501'/0'/0'",   CURVE_ED25519,   true, 14.8000f,  152.4f,  +5.6f, "7x4pM9yK...SOL" },
    { COIN_ID_LTC,  "LTC",   "Litecoin",      "m/84'/2'/0'/0/0",    CURVE_SECP256K1, true,  8.2500f,   78.5f,  -0.4f, "ltc1q89y2m...4k" },
    { COIN_ID_DOGE, "DOGE",  "Dogecoin",      "m/44'/3'/0'/0/0",    CURVE_SECP256K1, true, 2500.0f,    0.145f, +7.1f, "D8vK2eQ9...mY7a" },
    { COIN_ID_ADA,  "ADA",   "Cardano",       "m/1852'/1815'/0'/0", CURVE_ED25519,   false, 0.0f,     0.48f,   +0.9f, "addr1q9y..." },
    { COIN_ID_XRP,  "XRP",   "Ripple",        "m/44'/144'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,     0.58f,   -1.2f, "rEb8TK3g..." },
    { COIN_ID_AVAX, "AVAX",  "Avalanche",     "m/44'/9000'/0'/0/0", CURVE_SECP256K1, false, 0.0f,    28.90f,   +2.1f, "0xAVAX..." },
    { COIN_ID_DOT,  "DOT",   "Polkadot",      "m/44'/354'/0'/0'/0'",CURVE_ED25519,   false, 0.0f,     6.85f,   -0.5f, "1DOT..." },
    { COIN_ID_LINK, "LINK",  "Chainlink",     "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f,    17.20f,   +4.3f, "0xLINK..." },
    { COIN_ID_POL,  "POL",   "Polygon",       "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f,     0.54f,   +1.1f, "0xPOL..." },
    { COIN_ID_ATOM, "ATOM",  "Cosmos",        "m/44'/118'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,     6.40f,   -2.0f, "cosmos1..." },
    { COIN_ID_TRX,  "TRX",   "TRON",          "m/44'/195'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,     0.155f,  +0.3f, "TTRX..." },
    { COIN_ID_NEAR, "NEAR",  "NEAR Protocol", "m/44'/397'/0'/0'",   CURVE_ED25519,   false, 0.0f,     4.80f,   +3.8f, "near1..." },
    { COIN_ID_KAS,  "KAS",   "Kaspa",         "m/44'/111111'/0'/0", CURVE_SECP256K1, false, 0.0f,     0.165f,  +8.4f, "kaspa:q..." },
    { COIN_ID_SUI,  "SUI",   "Sui",           "m/44'/784'/0'/0'/0'",CURVE_ED25519,   false, 0.0f,     1.85f,   +6.2f, "0xSUI..." },
    { COIN_ID_APT,  "APT",   "Aptos",         "m/44'/637'/0'/0'/0'",CURVE_ED25519,   false, 0.0f,     8.20f,   +1.5f, "0xAPT..." },
    { COIN_ID_TON,  "TON",   "Toncoin",       "m/44'/607'/0'/0'",   CURVE_ED25519,   false, 0.0f,     5.60f,   -0.8f, "EQTON..." },
    { COIN_ID_XLM,  "XLM",   "Stellar",       "m/44'/148'/0'/0'",   CURVE_ED25519,   false, 0.0f,     0.115f,  +0.2f, "GSTELLAR..." },
    { COIN_ID_BCH,  "BCH",   "Bitcoin Cash",  "m/44'/145'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,   345.00f,   +1.9f, "bitcoincash:q..." },
    { COIN_ID_BNB,  "BNB",   "BNB Chain",     "m/44'/714'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,   580.00f,   +0.7f, "0xBNB..." },
    { COIN_ID_SHIB, "SHIB",  "Shiba Inu",     "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f, 0.000018f,   +4.1f, "0xSHIB..." },
    { COIN_ID_UNI,  "UNI",   "Uniswap",       "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f,     7.80f,   -1.5f, "0xUNI..." },
    { COIN_ID_XMR,  "XMR",   "Monero",        "m/44'/128'/0'/0/0",  CURVE_ED25519,   false, 0.0f,   158.00f,   +0.4f, "888tXMR..." }
};

void CryptoCoinRegistry::init() {
    loadPreferences();
}

CoinAsset* CryptoCoinRegistry::getCoin(SupportedCoinId id) {
    if (id < COIN_REGISTRY_COUNT) return &_coins[id];
    return nullptr;
}

CoinAsset* CryptoCoinRegistry::getCoinByIndex(int index) {
    if (index >= 0 && index < COIN_REGISTRY_COUNT) return &_coins[index];
    return nullptr;
}

int CryptoCoinRegistry::getActiveCoinCount() {
    int count = 0;
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        if (_coins[i].enabled) count++;
    }
    return count;
}

CoinAsset* CryptoCoinRegistry::getActiveCoinByIndex(int activeIdx) {
    int seen = 0;
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        if (_coins[i].enabled) {
            if (seen == activeIdx) return &_coins[i];
            seen++;
        }
    }
    return &_coins[0];
}

void CryptoCoinRegistry::setCoinEnabled(SupportedCoinId id, bool enabled) {
    if (id < COIN_REGISTRY_COUNT) {
        _coins[id].enabled = enabled;
        savePreferences();
    }
}

void CryptoCoinRegistry::updateBalance(SupportedCoinId id, float balance) {
    if (id < COIN_REGISTRY_COUNT) {
        _coins[id].balance = balance;
        savePreferences();
    }
}

void CryptoCoinRegistry::updatePrice(SupportedCoinId id, float priceUsd, float change24h) {
    if (id < COIN_REGISTRY_COUNT) {
        _coins[id].priceUsd = priceUsd;
        _coins[id].change24h = change24h;
    }
}

float CryptoCoinRegistry::getTotalPortfolioValueUsd() {
    float total = 0.0f;
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        if (_coins[i].enabled && _coins[i].balance > 0.0f) {
            total += (_coins[i].balance * _coins[i].priceUsd);
        }
    }
    return total;
}

void CryptoCoinRegistry::savePreferences() {
    Preferences prefs;
    if (prefs.begin("portfolio_sec", false)) {
        for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
            char keyEn[16], keyBal[16];
            snprintf(keyEn, sizeof(keyEn), "en_%d", i);
            snprintf(keyBal, sizeof(keyBal), "bal_%d", i);
            prefs.putBool(keyEn, _coins[i].enabled);
            prefs.putFloat(keyBal, _coins[i].balance);
        }
        prefs.end();
    }
}

void CryptoCoinRegistry::loadPreferences() {
    Preferences prefs;
    if (prefs.begin("portfolio_sec", true)) {
        for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
            char keyEn[16], keyBal[16];
            snprintf(keyEn, sizeof(keyEn), "en_%d", i);
            snprintf(keyBal, sizeof(keyBal), "bal_%d", i);
            if (prefs.isKey(keyEn)) {
                _coins[i].enabled = prefs.getBool(keyEn, _coins[i].enabled);
            }
            if (prefs.isKey(keyBal)) {
                _coins[i].balance = prefs.getFloat(keyBal, _coins[i].balance);
            }
        }
        prefs.end();
    }
}
