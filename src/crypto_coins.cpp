/**
 * crypto_coins.cpp — Implementation of Master Cryptocurrency Asset Registry
 */

#include "crypto_coins.h"
#include <Preferences.h>

CoinAsset CryptoCoinRegistry::_coins[COIN_REGISTRY_COUNT] = {
    { COIN_ID_BTC,  "BTC",   "Bitcoin",       "m/84'/0'/0'/0/0",    CURVE_SECP256K1, true,  0.0f, 85750.0f, +5.6f, "" },
    { COIN_ID_ETH,  "ETH",   "Ethereum",      "m/44'/60'/0'/0/0",   CURVE_SECP256K1, true,  0.0f,  2735.0f, +4.2f, "" },
    { COIN_ID_SOL,  "SOL",   "Solana",        "m/44'/501'/0'/0'",   CURVE_ED25519,   true,  0.0f,   116.8f, +6.4f, "" },
    { COIN_ID_LTC,  "LTC",   "Litecoin",      "m/84'/2'/0'/0/0",    CURVE_SECP256K1, true,  0.0f,    88.5f, +3.1f, "" },
    { COIN_ID_DOGE, "DOGE",  "Dogecoin",      "m/44'/3'/0'/0/0",    CURVE_SECP256K1, true,  0.0f,   0.0974f,+12.0f, "" },
    { COIN_ID_ADA,  "ADA",   "Cardano",       "m/1852'/1815'/0'/0", CURVE_ED25519,   false, 0.0f,     0.36f, +1.9f, "" },
    { COIN_ID_XRP,  "XRP",   "Ripple",        "m/44'/144'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,     0.59f, +2.2f, "" },
    { COIN_ID_AVAX, "AVAX",  "Avalanche",     "m/44'/9000'/0'/0/0", CURVE_SECP256K1, false, 0.0f,    25.80f, +3.1f, "" },
    { COIN_ID_DOT,  "DOT",   "Polkadot",      "m/44'/354'/0'/0'/0'",CURVE_ED25519,   false, 0.0f,     4.85f, +1.5f, "" },
    { COIN_ID_LINK, "LINK",  "Chainlink",     "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f,    12.40f, +2.8f, "" },
    { COIN_ID_POL,  "POL",   "Polygon",       "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f,     0.38f, +1.4f, "" },
    { COIN_ID_ATOM, "ATOM",  "Cosmos",        "m/44'/118'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,     4.60f, +0.8f, "" },
    { COIN_ID_TRX,  "TRX",   "TRON",          "m/44'/195'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,     0.152f,+0.5f, "" },
    { COIN_ID_NEAR, "NEAR",  "NEAR Protocol", "m/44'/397'/0'/0'",   CURVE_ED25519,   false, 0.0f,     4.20f, +3.2f, "" },
    { COIN_ID_KAS,  "KAS",   "Kaspa",         "m/44'/111111'/0'/0", CURVE_SECP256K1, false, 0.0f,     0.135f,+4.1f, "" },
    { COIN_ID_SUI,  "SUI",   "Sui",           "m/44'/784'/0'/0'/0'",CURVE_ED25519,   false, 0.0f,     1.65f, +5.2f, "" },
    { COIN_ID_APT,  "APT",   "Aptos",         "m/44'/637'/0'/0'/0'",CURVE_ED25519,   false, 0.0f,     6.80f, +2.5f, "" },
    { COIN_ID_TON,  "TON",   "Toncoin",       "m/44'/607'/0'/0'",   CURVE_ED25519,   false, 0.0f,     5.40f, +1.2f, "" },
    { COIN_ID_XLM,  "XLM",   "Stellar",       "m/44'/148'/0'/0'",   CURVE_ED25519,   false, 0.0f,     0.098f,+1.1f, "" },
    { COIN_ID_BCH,  "BCH",   "Bitcoin Cash",  "m/44'/145'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,   325.00f, +2.1f, "" },
    { COIN_ID_BNB,  "BNB",   "BNB Chain",     "m/44'/714'/0'/0/0",  CURVE_SECP256K1, false, 0.0f,   575.00f, +1.8f, "" },
    { COIN_ID_SHIB, "SHIB",  "Shiba Inu",     "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f, 0.000015f, +3.1f, "" },
    { COIN_ID_UNI,  "UNI",   "Uniswap",       "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f,     7.20f, +1.4f, "" },
    { COIN_ID_XMR,  "XMR",   "Monero",        "m/44'/128'/0'/0/0",  CURVE_ED25519,   false, 0.0f,   152.00f, +1.0f, "" },
    { COIN_ID_PEPE, "PEPE",  "Pepe",          "m/44'/60'/0'/0/0",   CURVE_SECP256K1, true,  0.0f, 0.0000105f, +24.5f, "" },
    { COIN_ID_BONK, "BONK",  "Bonk",          "m/44'/501'/0'/0'",   CURVE_ED25519,   false, 0.0f, 0.0000185f, +6.2f,  "" },
    { COIN_ID_FLOKI,"FLOKI", "Floki",         "m/44'/60'/0'/0/0",   CURVE_SECP256K1, false, 0.0f, 0.000142f,  +8.1f,  "" },
    { COIN_ID_WIF,  "WIF",   "dogwifhat",     "m/44'/501'/0'/0'",   CURVE_ED25519,   false, 0.0f,     2.15f,  +5.4f,  "" }
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
