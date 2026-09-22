/**
 * portfolio_mgr.h — Portfolio Tracker & Multi-Currency Manager
 * ==============================================================================
 * Manages active user-selected coin portfolio, balance display, live USD valuations,
 * and command parsing from WebUSB / Serial host bridge.
 */

#pragma once

#include <Arduino.h>
#include "crypto_coins.h"

class PortfolioManager {
public:
    static void init();
    static int  getActiveCount();
    static CoinAsset* getActiveCoin(int index);
    static void nextCoin();
    static void prevCoin();
    static CoinAsset* getCurrentCoin();
    static int  getCurrentIndex();
    static float getTotalValueUsd();
    static bool  isLive(); // Returns true if telemetry received within last 120s
    static uint32_t getLastUpdateMillis();

    // Command processing for USB Bridge & Serial
    static bool processCommand(const String& cmd, String& response);

private:
    static int _currentActiveIdx;
    static uint32_t _lastPriceUpdateMs;
};
