#include "wall_ticker.h"
#include "crypto_coins.h"
#include "portfolio_mgr.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include <time.h>

// ESP-IDF's Mozilla root store, already linked in via libmbedtls (no extra flash)
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

// CoinGecko ids come from the coin catalog (CatalogCoin::cgId)

#define MAX_RESULTS (CATALOG_COUNT * 2)
static char s_results[MAX_RESULTS][72];
static int s_resultCount = 0;
static SemaphoreHandle_t s_lock = nullptr;
static volatile bool s_running = false;
static bool s_withBalances = false;

static void queueCmd(const char* cmd) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_resultCount < MAX_RESULTS) {
        strncpy(s_results[s_resultCount], cmd, sizeof(s_results[0]) - 1);
        s_results[s_resultCount][sizeof(s_results[0]) - 1] = '\0';
        s_resultCount++;
    }
    xSemaphoreGive(s_lock);
}

// GET/POST over HTTPS with certificate verification (default ESP-IDF CA bundle).
static bool httpsRequest(const String& url, const char* postBody, String& out) {
    NetworkClientSecure client;   // never setInsecure(): full chain + hostname verification
    client.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;
    http.addHeader("User-Agent", "CryptoTKeyS3/1.0");
    int code;
    if (postBody) {
        http.addHeader("Content-Type", "application/json");
        code = http.POST((uint8_t*)postBody, strlen(postBody));
    } else {
        code = http.GET();
    }
    bool ok = code == 200;
    if (ok) {
        out = http.getString();
    } else {
        char err[96] = "";
        client.lastError(err, sizeof(err));
        Serial.printf("[TICKER] HTTP %d (%s) from %.60s\n", code, err, url.c_str());
    }
    http.end();
    return ok;
}

// Hex quantity ("0x1bc16d674ec80000") divided by 10^decimals, without 64-bit overflow.
static double hexToScaled(const char* hex, int decimals) {
    if (!hex) return 0;
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    double v = 0;
    for (; *hex; hex++) {
        char c = *hex;
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) break;
        v = v * 16 + d;
    }
    return v / pow(10, decimals);
}

static void fetchPrices() {
    String ids;
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
        if (!c->enabled) continue;
        if (ids.length()) ids += ",";
        ids += c->meta->cgId;
    }
    if (!ids.length()) return;
    String body;
    if (!httpsRequest("https://api.coingecko.com/api/v3/simple/price?vs_currencies=usd&include_24hr_change=true&ids=" + ids,
                      nullptr, body)) return;
    JsonDocument doc;
    if (deserializeJson(doc, body)) return;
    for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
        CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
        if (!c->enabled) continue;
        JsonVariant v = doc[c->meta->cgId];
        if (v.isNull()) continue;
        char cmd[72];
        snprintf(cmd, sizeof(cmd), "setprice %s %.10g %.2f", c->symbol, v["usd"].as<double>(),
                 v["usd_24h_change"].as<double>());
        queueCmd(cmd);
    }
}

static void queueBalance(const char* sym, double bal) {
    char cmd[72];
    snprintf(cmd, sizeof(cmd), "setbal %s %.8f", sym, bal);
    queueCmd(cmd);
}

static void fetchBalances() {
    CoinAsset* btc = CryptoCoinRegistry::findBySymbol("BTC");
    CoinAsset* eth = CryptoCoinRegistry::findBySymbol("ETH");
    CoinAsset* sol = CryptoCoinRegistry::findBySymbol("SOL");
    CoinAsset* doge = CryptoCoinRegistry::findBySymbol("DOGE");
    String body;

    if (btc && btc->address[0] &&
        httpsRequest(String("https://mempool.space/api/address/") + btc->address, nullptr, body)) {
        JsonDocument d;
        if (!deserializeJson(d, body)) {
            double sats = d["chain_stats"]["funded_txo_sum"].as<double>() - d["chain_stats"]["spent_txo_sum"].as<double>();
            queueBalance("BTC", sats / 1e8);
        }
    }
    if (eth && eth->address[0]) {
        char req[160];
        snprintf(req, sizeof(req), "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBalance\",\"params\":[\"%s\",\"latest\"],\"id\":1}", eth->address);
        if (httpsRequest("https://ethereum-rpc.publicnode.com", req, body)) {
            JsonDocument d;
            if (!deserializeJson(d, body)) queueBalance("ETH", hexToScaled(d["result"] | "0x0", 18));
        }
        // PEPE (ERC-20 balanceOf)
        char call[300];
        snprintf(call, sizeof(call),
                 "{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\",\"params\":[{\"to\":\"0x6982508145454Ce325dDbE47a25d4ec3d2311933\","
                 "\"data\":\"0x70a08231000000000000000000000000%s\"},\"latest\"],\"id\":2}", eth->address + 2);
        if (httpsRequest("https://ethereum-rpc.publicnode.com", call, body)) {
            JsonDocument d;
            if (!deserializeJson(d, body)) queueBalance("PEPE", hexToScaled(d["result"] | "0x0", 18));
        }
    }
    if (sol && sol->address[0]) {
        char req[160];
        snprintf(req, sizeof(req), "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getBalance\",\"params\":[\"%s\"]}", sol->address);
        if (httpsRequest("https://api.mainnet-beta.solana.com", req, body)) {
            JsonDocument d;
            if (!deserializeJson(d, body)) queueBalance("SOL", d["result"]["value"].as<double>() / 1e9);
        }
    }
    if (doge && doge->address[0] &&
        httpsRequest(String("https://api.blockcypher.com/v1/doge/main/addrs/") + doge->address + "/balance", nullptr, body)) {
        JsonDocument d;
        if (!deserializeJson(d, body)) queueBalance("DOGE", d["final_balance"].as<double>() / 1e8);
    }
}

// TLS certificate validity checks need a real clock; the dongle has no RTC battery.
static bool syncClock() {
    if (time(nullptr) > 1700000000) return true;   // already set (after Nov 2023)
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    for (int i = 0; i < 50; i++) {                  // up to 5 s
        if (time(nullptr) > 1700000000) return true;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    Serial.println("[TICKER] NTP time sync failed; skipping HTTPS this burst");
    return false;
}

static void tickerTask(void*) {
    if (!syncClock()) {
        s_running = false;
        vTaskDelete(nullptr);
    }
    fetchPrices();
    if (s_withBalances) fetchBalances();
    s_running = false;
    vTaskDelete(nullptr);
}

void WallTicker::start(bool withBalances) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (s_running) return;
    s_running = true;
    s_withBalances = withBalances;
    // TLS handshakes need a generous stack; core 0 keeps the UI loop (core 1) responsive
    if (xTaskCreatePinnedToCore(tickerTask, "wall_ticker", 16384, nullptr, 1, nullptr, 0) != pdPASS) {
        s_running = false;
    }
}

bool WallTicker::running() {
    return s_running;
}

void WallTicker::applyResults() {
    if (!s_lock) return;
    static char local[MAX_RESULTS][72];   // too big for the loop task stack
    int n;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    n = s_resultCount;
    memcpy(local, s_results, sizeof(local[0]) * n);
    s_resultCount = 0;
    xSemaphoreGive(s_lock);
    String resp;
    for (int i = 0; i < n; i++) PortfolioManager::processCommand(String(local[i]), resp);
    if (n) Serial.printf("[TICKER] Applied %d live update(s) from Wi-Fi\n", n);
}
