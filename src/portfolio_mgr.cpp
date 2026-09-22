/**
 * portfolio_mgr.cpp — Implementation of Portfolio Manager
 */

#include "portfolio_mgr.h"

int PortfolioManager::_currentActiveIdx = 0;
uint32_t PortfolioManager::_lastPriceUpdateMs = 0;

void PortfolioManager::init() {
    CryptoCoinRegistry::init();
    _currentActiveIdx = 0;
    _lastPriceUpdateMs = 0;
}

int PortfolioManager::getActiveCount() {
    return CryptoCoinRegistry::getActiveCoinCount();
}

CoinAsset* PortfolioManager::getActiveCoin(int index) {
    return CryptoCoinRegistry::getActiveCoinByIndex(index);
}

void PortfolioManager::nextCoin() {
    int total = getActiveCount();
    if (total > 0) {
        _currentActiveIdx = (_currentActiveIdx + 1) % total;
    }
}

void PortfolioManager::prevCoin() {
    int total = getActiveCount();
    if (total > 0) {
        _currentActiveIdx = (_currentActiveIdx - 1 + total) % total;
    }
}

CoinAsset* PortfolioManager::getCurrentCoin() {
    int total = getActiveCount();
    if (total == 0) return CryptoCoinRegistry::getCoinByIndex(0);
    return CryptoCoinRegistry::getActiveCoinByIndex(_currentActiveIdx);
}

int PortfolioManager::getCurrentIndex() {
    return _currentActiveIdx;
}

float PortfolioManager::getTotalValueUsd() {
    return CryptoCoinRegistry::getTotalPortfolioValueUsd();
}

bool PortfolioManager::isLive() {
    if (_lastPriceUpdateMs == 0) return false;
    return (millis() - _lastPriceUpdateMs < 120000);
}

uint32_t PortfolioManager::getLastUpdateMillis() {
    return _lastPriceUpdateMs;
}

bool PortfolioManager::processCommand(const String& cmd, String& response) {
    String c = cmd;
    c.trim();

    if (c.equalsIgnoreCase("coins")) {
        response = "\n--- Supported Coins & Portfolio Holdings ---\n";
        for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
            CoinAsset* coin = CryptoCoinRegistry::getCoinByIndex(i);
            float fiatVal = coin->balance * coin->priceUsd;
            char line[128];
            snprintf(line, sizeof(line), " [%s] %-5s %-14s Bal: %-10.4f Price: $%-.2f Val: $%-.2f\n",
                     coin->enabled ? "✔" : " ", coin->symbol, coin->name, coin->balance, coin->priceUsd, fiatVal);
            response += line;
        }
        char tot[64];
        snprintf(tot, sizeof(tot), "\nTotal Portfolio Valuation: $%.2f USD\n", getTotalValueUsd());
        response += tot;
        return true;
    }

    if (c.startsWith("enable ")) {
        String sym = c.substring(7);
        sym.trim();
        sym.toUpperCase();
        for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
            CoinAsset* coin = CryptoCoinRegistry::getCoinByIndex(i);
            if (sym.equalsIgnoreCase(coin->symbol)) {
                CryptoCoinRegistry::setCoinEnabled((SupportedCoinId)i, true);
                response = "Enabled " + sym + " in portfolio tracker.";
                return true;
            }
        }
        response = "Coin symbol not found.";
        return true;
    }

    if (c.startsWith("disable ")) {
        String sym = c.substring(8);
        sym.trim();
        sym.toUpperCase();
        for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
            CoinAsset* coin = CryptoCoinRegistry::getCoinByIndex(i);
            if (sym.equalsIgnoreCase(coin->symbol)) {
                CryptoCoinRegistry::setCoinEnabled((SupportedCoinId)i, false);
                response = "Disabled " + sym + " from portfolio tracker.";
                return true;
            }
        }
        response = "Coin symbol not found.";
        return true;
    }

    if (c.startsWith("setbal ")) {
        int firstSpace = c.indexOf(' ', 7);
        if (firstSpace == -1) {
            response = "Usage: setbal <SYMBOL> <AMOUNT>";
            return true;
        }
        String sym = c.substring(7, firstSpace);
        sym.trim();
        sym.toUpperCase();
        float bal = c.substring(firstSpace + 1).toFloat();

        for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
            CoinAsset* coin = CryptoCoinRegistry::getCoinByIndex(i);
            if (sym.equalsIgnoreCase(coin->symbol)) {
                CryptoCoinRegistry::updateBalance((SupportedCoinId)i, bal);
                char res[64];
                snprintf(res, sizeof(res), "Updated %s balance to: %.6f", coin->symbol, bal);
                response = res;
                return true;
            }
        }
        response = "Coin symbol not found.";
        return true;
    }

    if (c.startsWith("setprice ")) {
        // Syntax: setprice <SYMBOL> <PRICE_USD> [CHANGE_24H]
        int s1 = c.indexOf(' ', 9);
        if (s1 == -1) {
            response = "Usage: setprice <SYMBOL> <PRICE_USD> [CHANGE_24H]";
            return true;
        }
        String sym = c.substring(9, s1);
        sym.trim();
        sym.toUpperCase();

        int s2 = c.indexOf(' ', s1 + 1);
        float price = (s2 == -1) ? c.substring(s1 + 1).toFloat() : c.substring(s1 + 1, s2).toFloat();
        float chg = (s2 == -1) ? 0.0f : c.substring(s2 + 1).toFloat();

        for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
            CoinAsset* coin = CryptoCoinRegistry::getCoinByIndex(i);
            if (sym.equalsIgnoreCase(coin->symbol)) {
                CryptoCoinRegistry::updatePrice((SupportedCoinId)i, price, chg);
                _lastPriceUpdateMs = millis();
                char res[64];
                snprintf(res, sizeof(res), "Updated %s: Price=$%.2f Chg=%.2f%%", coin->symbol, price, chg);
                response = res;
                return true;
            }
        }
        response = "Coin symbol not found.";
        return true;
    }

    if (c.equalsIgnoreCase("json")) {
        // Output compact JSON for WebUSB companion app
        response = "{\"total\":";
        response += String(getTotalValueUsd(), 2);
        response += ",\"coins\":[";
        bool first = true;
        for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
            CoinAsset* coin = CryptoCoinRegistry::getCoinByIndex(i);
            if (!first) response += ",";
            first = false;
            response += "{\"id\":";
            response += String(coin->id);
            response += ",\"sym\":\"";
            response += coin->symbol;
            response += "\",\"en\":";
            response += coin->enabled ? "true" : "false";
            response += ",\"bal\":";
            response += String(coin->balance, 6);
            response += ",\"price\":";
            response += String(coin->priceUsd, 2);
            response += ",\"chg\":";
            response += String(coin->change24h, 2);
            response += "}";
        }
        response += "]}";
        return true;
    }

    return false;
}
