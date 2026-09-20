/**
 * crypto_coins.h — Master Cryptocurrency Asset Registry & Portfolio Model
 * ==============================================================================
 * Comprehensive multi-chain metadata, derivation paths, and balance records.
 */

#pragma once

#include <Arduino.h>

enum CoinCurve {
    CURVE_SECP256K1,
    CURVE_ED25519
};

enum SupportedCoinId {
    COIN_ID_BTC = 0,
    COIN_ID_ETH,
    COIN_ID_SOL,
    COIN_ID_LTC,
    COIN_ID_DOGE,
    COIN_ID_ADA,
    COIN_ID_XRP,
    COIN_ID_AVAX,
    COIN_ID_DOT,
    COIN_ID_LINK,
    COIN_ID_POL,
    COIN_ID_ATOM,
    COIN_ID_TRX,
    COIN_ID_NEAR,
    COIN_ID_KAS,
    COIN_ID_SUI,
    COIN_ID_APT,
    COIN_ID_TON,
    COIN_ID_XLM,
    COIN_ID_BCH,
    COIN_ID_BNB,
    COIN_ID_SHIB,
    COIN_ID_UNI,
    COIN_ID_XMR,
    COIN_REGISTRY_COUNT
};

struct CoinAsset {
    SupportedCoinId id;
    const char* symbol;
    const char* name;
    const char* derivationPath;
    CoinCurve   curve;
    bool        enabled;         // Selected by user in active wallet/tracker
    float       balance;         // User's coin holding
    float       priceUsd;        // Live price in USD
    float       change24h;       // 24-hour price change %
    char        address[64];     // Derived public address
};

class CryptoCoinRegistry {
public:
    static void init();
    static int getCoinCount() { return COIN_REGISTRY_COUNT; }
    static CoinAsset* getCoin(SupportedCoinId id);
    static CoinAsset* getCoinByIndex(int index);
    static int getActiveCoinCount();
    static CoinAsset* getActiveCoinByIndex(int activeIdx);
    static void setCoinEnabled(SupportedCoinId id, bool enabled);
    static void updateBalance(SupportedCoinId id, float balance);
    static void updatePrice(SupportedCoinId id, float priceUsd, float change24h);
    static float getTotalPortfolioValueUsd();

    // Persistence
    static void savePreferences();
    static void loadPreferences();

private:
    static CoinAsset _coins[COIN_REGISTRY_COUNT];
};
