/**
 * crypto_coins.cpp — Portfolio registry over the curated coin catalog
 */

#include "crypto_coins.h"
#include <Preferences.h>

CoinAsset CryptoCoinRegistry::_coins[COIN_REGISTRY_COUNT];
void (*CryptoCoinRegistry::onPriceTick)(const CoinAsset*, float) = nullptr;

// Coins enabled on a fresh device
static const char* DEFAULT_ENABLED[] = {"BTC", "ETH", "SOL", "BNB", "XRP", "DOGE", "PEPE"};

// Index order of the pre-catalog registry (NVS "portfolio_sec" en_<i> / bal_<i>), for migration
static const char* LEGACY_ORDER[] = {
    "BTC", "ETH", "SOL", "BNB", "XRP", "ADA", "AVAX", "DOT", "LINK", "LTC", "BCH", "ATOM", "POL", "TRX",
    "NEAR", "SUI", "APT", "TON", "XLM", "ALGO", "HBAR", "VET", "FIL", "ICP", "TAO", "INJ", "ARB", "OP",
    "KAS", "XMR", "EGLD", "UNI", "DOGE", "SHIB", "PEPE", "BONK", "FLOKI", "WIF", "BRETT", "MOG", "TURBO",
    "POPCAT", "NEIRO", "GOAT"};

void CryptoCoinRegistry::init() {
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        CoinAsset& c = _coins[i];
        memset(&c, 0, sizeof(c));
        c.id = i;
        c.meta = &CATALOG[i];
        c.symbol = CATALOG[i].symbol;
        c.name = CATALOG[i].name;
        for (const char* s : DEFAULT_ENABLED) {
            if (strcmp(s, c.symbol) == 0) c.enabled = true;
        }
    }
    loadPreferences();
}

CoinAsset* CryptoCoinRegistry::getCoin(SupportedCoinId id) {
    return id < COIN_REGISTRY_COUNT ? &_coins[id] : nullptr;
}

CoinAsset* CryptoCoinRegistry::findBySymbol(const char* symbol) {
    if (!symbol) return nullptr;
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        if (strcasecmp(_coins[i].symbol, symbol) == 0) return &_coins[i];
    }
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

int CryptoCoinRegistry::getCountByCategory(CoinCategory cat) {
    int count = 0;
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        if (_coins[i].meta->category == cat) count++;
    }
    return count;
}

bool CryptoCoinRegistry::isFamilyInUse(WalletFamily fam) {
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        if (_coins[i].enabled && _coins[i].meta->family == fam) return true;
    }
    return false;
}

void CryptoCoinRegistry::setCoinEnabled(SupportedCoinId id, bool enabled) {
    if (id < COIN_REGISTRY_COUNT) _coins[id].enabled = enabled;
}

void CryptoCoinRegistry::updateBalance(SupportedCoinId id, float balance) {
    if (id < COIN_REGISTRY_COUNT && _coins[id].balance != balance) {
        _coins[id].balance = balance;
        savePreferences();
    }
}

void CryptoCoinRegistry::updatePrice(SupportedCoinId id, float priceUsd, float change24h) {
    if (id < COIN_REGISTRY_COUNT && priceUsd > 0.0f) {
        float prev = _coins[id].priceUsd;
        _coins[id].priceUsd = priceUsd;
        if (onPriceTick && prev > 0.0f && prev != priceUsd) onPriceTick(&_coins[id], (priceUsd - prev) / prev * 100.0f);
        _coins[id].change24h = change24h;
        _coins[id].priceUpdatedMs = millis() | 1;   // never 0 once live
    }
}

float CryptoCoinRegistry::getTotalPortfolioValueUsd() {
    float total = 0.0f;
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        if (_coins[i].enabled && _coins[i].balance > 0.0f) total += _coins[i].balance * _coins[i].priceUsd;
    }
    return total;
}

// NVS "portfolio_v2": "sel" = comma-separated selected symbols (one entry, not one per coin),
// "b_<SYM>" = balance, only for coins actually held. NVS is only 20 KB; keep it lean.
void CryptoCoinRegistry::savePreferences() {
    String sel;
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        if (!_coins[i].enabled) continue;
        if (sel.length()) sel += ",";
        sel += _coins[i].symbol;
    }
    Preferences prefs;
    if (!prefs.begin("portfolio_v2", false)) return;
    if (prefs.getString("sel", "") != sel || !prefs.isKey("sel")) prefs.putString("sel", sel);
    for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
        char key[16];
        snprintf(key, sizeof(key), "b_%s", _coins[i].symbol);
        if (_coins[i].balance > 0.0f) {
            if (prefs.getFloat(key, -1.0f) != _coins[i].balance) prefs.putFloat(key, _coins[i].balance);
        } else if (prefs.isKey(key)) {
            prefs.remove(key);
        }
    }
    prefs.end();
}

void CryptoCoinRegistry::loadPreferences() {
    Preferences prefs;
    prefs.begin("portfolio_v2", false);
    bool have = prefs.isKey("sel");
    if (have) {
        String sel = "," + prefs.getString("sel", "") + ",";
        for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
            char key[16];
            _coins[i].enabled = sel.indexOf(String(",") + _coins[i].symbol + ",") >= 0;
            snprintf(key, sizeof(key), "b_%s", _coins[i].symbol);
            _coins[i].balance = prefs.getFloat(key, 0.0f);
        }
    } else if (prefs.getBool("init", false)) {
        // Interim per-coin layout (e_<SYM> for every coin): read it, then rewrite compactly
        for (int i = 0; i < COIN_REGISTRY_COUNT; i++) {
            char key[16];
            snprintf(key, sizeof(key), "e_%s", _coins[i].symbol);
            _coins[i].enabled = prefs.getBool(key, false);
            snprintf(key, sizeof(key), "b_%s", _coins[i].symbol);
            _coins[i].balance = prefs.getFloat(key, 0.0f);
        }
        prefs.clear();
        have = true;
    }
    prefs.end();

    if (!have) {
        // First boot on the catalog: carry over the selection from the old index-keyed registry
        if (prefs.begin("portfolio_sec", false)) {
            bool any = false;
            for (size_t i = 0; i < sizeof(LEGACY_ORDER) / sizeof(LEGACY_ORDER[0]); i++) {
                char keyEn[16], keyBal[16];
                snprintf(keyEn, sizeof(keyEn), "en_%d", (int)i);
                snprintf(keyBal, sizeof(keyBal), "bal_%d", (int)i);
                if (!prefs.isKey(keyEn)) continue;
                if (!any) {
                    for (int k = 0; k < COIN_REGISTRY_COUNT; k++) _coins[k].enabled = false;
                    any = true;
                }
                CoinAsset* c = findBySymbol(LEGACY_ORDER[i]);
                if (!c) continue;   // coin no longer in the curated catalog
                c->enabled = prefs.getBool(keyEn, false);
                c->balance = prefs.getFloat(keyBal, 0.0f);
            }
            prefs.end();
        }
    }
    savePreferences();
    // The old namespace is fully migrated: free its ~88 NVS entries
    if (prefs.begin("portfolio_sec", true)) {   // read-only open fails if it no longer exists
        prefs.end();
        prefs.begin("portfolio_sec", false);
        prefs.clear();
        prefs.end();
    }
}
