/**
 * price_ticker.cpp — Implementation of Live Cryptocurrency Price Engine
 */

#include "price_ticker.h"
#include <WiFiClientSecure.h>

void PriceTicker::begin() {
    _lastFetch = 0;
    _isLive = false;
}

bool PriceTicker::fetchPricesNow() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate verification for public market feed

    HTTPClient https;
    // CoinGecko public simple price API
    const char* url = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum,solana,dogecoin&vs_currencies=usd";

    if (!https.begin(client, url)) {
        return false;
    }

    https.setTimeout(4000);
    https.addHeader("User-Agent", "TDongle-S3-CryptoVault/1.0");
    int httpCode = https.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = https.getString();
        StaticJsonDocument<512> doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (!err) {
            if (doc.containsKey("bitcoin"))   _btc = doc["bitcoin"]["usd"].as<float>();
            if (doc.containsKey("ethereum"))  _eth = doc["ethereum"]["usd"].as<float>();
            if (doc.containsKey("solana"))    _sol = doc["solana"]["usd"].as<float>();
            if (doc.containsKey("dogecoin"))  _doge = doc["dogecoin"]["usd"].as<float>();
            _isLive = true;
            _lastFetch = millis();
            https.end();
            Serial.printf("[PRICE] Live Prices Updated -> BTC: $%0.0f | ETH: $%0.0f | SOL: $%0.2f | DOGE: $%0.3f\n",
                          _btc, _eth, _sol, _doge);
            return true;
        }
    } else {
        Serial.printf("[PRICE] HTTP fetch failed: %d\n", httpCode);
    }

    https.end();
    return false;
}

void PriceTicker::update() {
    if (WiFi.status() == WL_CONNECTED) {
        uint32_t now = millis();
        if (now - _lastFetch > _fetchInterval || _lastFetch == 0) {
            _lastFetch = now;
            fetchPricesNow();
        }
    }
}
