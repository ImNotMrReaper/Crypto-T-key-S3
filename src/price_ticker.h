/**
 * price_ticker.h — Live Cryptocurrency Price Engine for LilyGo T-Dongle S3
 * ========================================================================
 * Periodically queries public REST market APIs when Wi-Fi is connected.
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

class PriceTicker {
public:
    void begin();
    void update(); // Non-blocking periodic check

    bool fetchPricesNow();

    float getBtcPrice() const { return _btc; }
    float getEthPrice() const { return _eth; }
    float getSolPrice() const { return _sol; }
    float getDogePrice() const { return _doge; }
    bool  isLive() const { return _isLive; }

private:
    float    _btc = 64500.0f;
    float    _eth = 3450.0f;
    float    _sol = 148.50f;
    float    _doge = 0.108f;
    bool     _isLive = false;
    uint32_t _lastFetch = 0;
    uint32_t _fetchInterval = 60000; // Query every 60 seconds
};
