#pragma once
#include <Arduino.h>

/**
 * Wall-power ticker: fetches live prices (CoinGecko) and on-chain balances for the
 * wallet's public receive addresses over verified HTTPS (built-in Mozilla CA bundle),
 * in a background FreeRTOS task so the UI never blocks. Results are queued as the
 * same "setprice" / "setbal" commands the USB companion sends and applied on the main
 * loop through PortfolioManager::processCommand().
 */
namespace WallTicker {
    void start(bool withBalances);  // no-op if a fetch is already running
    bool running();
    void applyResults();            // main loop only
}
