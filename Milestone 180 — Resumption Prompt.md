# New Session Resumption Prompt
## Copy and paste this entire block into your new conversation:

---

You are continuing the **Crypto TKey S3** firmware project. Read the session handoff **first before doing anything**:

```
/home/mr-reaper/Documents/Obisdain Valuts/AI-Vault/03 - Workspaces & Projects/Crypto TKey S3 Workspace/Session Handoff — 2026-09-22.md
```

Also read `~/.agents/tasks/TASKS.md` (tail the last 100 lines) and `~/.agents/skills/hardware-wallet-dev/SKILL.md`.

---

## Your Mission: Milestone 180

Implement **three coordinated changes** to the Crypto TKey S3 firmware. Do them in this exact order, then compile and flash.

---

### CHANGE 1 — 1-Second Live Ticker Auto-Refresh

**File:** `/home/mr-reaper/Arduino Projects/crypto-tkey-s3/crypto-tkey-s3.ino`

**Step A** — Add global near line 75 (after other `uint32_t` globals):
```cpp
uint32_t lastTickerRefreshMs = 0;   // 1s live ticker auto-refresh
```

**Step B** — In `loop()`, add AFTER `handleSerialCommands();` (around line 249):
```cpp
    // ── 1-Second Live Ticker Auto-Refresh ────────────────────────────────────
    if (deviceState == STATE_PORTFOLIO_TRACKER &&
        !PowerManager::isDisplaySleeping() &&
        millis() - lastTickerRefreshMs >= 1000) {
        renderCurrentPortfolioCard();
        lastTickerRefreshMs = millis();
    }
```

**Step C** — In `handleSerialCommands()`, right after `renderCurrentPortfolioCard();` on the line that follows `PortfolioManager::processCommand(cmd, res)` succeeding (around line 857), add:
```cpp
        lastTickerRefreshMs = 0; // force immediate repaint on next loop
```

---

### CHANGE 2 — Exchange-Style Price Formatting + LED=UI Brand Color Sync

**File:** `/home/mr-reaper/Arduino Projects/crypto-tkey-s3/src/ui_engine.cpp`

**Step A** — Replace the entire coin color block in `renderPortfolioCard()` (lines 385–432) — the giant `if/else if` chain that starts with `// ── Brand Colors`. Replace with this brand-matched RGB565 table (derived from `getCoinRgb()` brand palette, formula: `((R>>3)<<11)|((G>>2)<<5)|(B>>3)`):

```cpp
    // ── Brand Colors — exact RGB565 derived from getCoinRgb() brand palette ──
    uint16_t coinColor = 0x07FF; // cyan fallback
    if      (strcmp(symbol,"BTC")   ==0) coinColor=0xF483; // {247,147,26}  Bitcoin Orange
    else if (strcmp(symbol,"ETH")   ==0) coinColor=0x33DD; // {98,126,234}  Ethereum Indigo
    else if (strcmp(symbol,"SOL")   ==0) coinColor=0x178A; // {20,241,149}  Solana Green-Teal
    else if (strcmp(symbol,"BNB")   ==0) coinColor=0xF5C5; // {243,186,47}  BNB Gold
    else if (strcmp(symbol,"XRP")   ==0) coinColor=0x045F; // {0,136,255}   XRP Blue
    else if (strcmp(symbol,"ADA")   ==0) coinColor=0x018D; // {0,51,173}    Cardano Cobalt
    else if (strcmp(symbol,"AVAX")  ==0) coinColor=0xE808; // {232,65,66}   Avalanche Red
    else if (strcmp(symbol,"DOT")   ==0) coinColor=0xE00F; // {230,0,122}   Polkadot Pink
    else if (strcmp(symbol,"LINK")  ==0) coinColor=0x15BE; // {43,110,245}  Chainlink Blue
    else if (strcmp(symbol,"LTC")   ==0) coinColor=0xBDD7; // {191,191,191} Litecoin Silver
    else if (strcmp(symbol,"BCH")   ==0) coinColor=0x0549; // {0,168,77}    BCH Green
    else if (strcmp(symbol,"ATOM")  ==0) coinColor=0x62D8; // {99,88,199}   Cosmos Purple
    else if (strcmp(symbol,"POL")   ==0) coinColor=0x823C; // {130,71,229}  Polygon Violet
    else if (strcmp(symbol,"TRX")   ==0) coinColor=0xDB02; // {220,20,20}   TRON Red
    else if (strcmp(symbol,"NEAR")  ==0) coinColor=0x0619; // {0,194,204}   NEAR Teal
    else if (strcmp(symbol,"SUI")   ==0) coinColor=0x2D9C; // {46,176,228}  Sui Sky Blue
    else if (strcmp(symbol,"APT")   ==0) coinColor=0x05F4; // {0,191,165}   Aptos Mint
    else if (strcmp(symbol,"TON")   ==0) coinColor=0x0459; // {0,136,204}   TON Blue
    else if (strcmp(symbol,"XLM")   ==0) coinColor=0x0DBD; // {14,182,236}  Stellar Blue
    else if (strcmp(symbol,"ALGO")  ==0) coinColor=0x65B4; // {100,180,160} Algorand Sage
    else if (strcmp(symbol,"HBAR")  ==0) coinColor=0x0515; // {0,163,174}   Hedera Teal
    else if (strcmp(symbol,"VET")   ==0) coinColor=0x265F; // {32,201,250}  VeChain Aqua
    else if (strcmp(symbol,"FIL")   ==0) coinColor=0x02BF; // {0,84,255}    Filecoin Blue
    else if (strcmp(symbol,"ICP")   ==0) coinColor=0x935C; // {150,110,229} ICP Gradient
    else if (strcmp(symbol,"TAO")   ==0) coinColor=0x8410; // {128,128,128} Bittensor Gray
    else if (strcmp(symbol,"INJ")   ==0) coinColor=0x04BF; // {0,148,255}   Injective Blue
    else if (strcmp(symbol,"ARB")   ==0) coinColor=0x13DA; // {18,120,214}  Arbitrum Blue
    else if (strcmp(symbol,"OP")    ==0) coinColor=0xF824; // {255,4,32}    Optimism Red
    else if (strcmp(symbol,"KAS")   ==0) coinColor=0x76F6; // {112,221,176} Kaspa Teal
    else if (strcmp(symbol,"XMR")   ==0) coinColor=0xFB20; // {255,102,0}   Monero Orange
    else if (strcmp(symbol,"EGLD")  ==0) coinColor=0x251F; // {35,162,255}  MultiversX Blue
    else if (strcmp(symbol,"UNI")   ==0) coinColor=0xF80F; // {255,0,122}   Uniswap Pink
    else if (strcmp(symbol,"DOGE")  ==0) coinColor=0xFDE4; // {255,189,35}  Dogecoin Gold
    else if (strcmp(symbol,"SHIB")  ==0) coinColor=0xFAC0; // {255,90,0}    Shiba Amber
    else if (strcmp(symbol,"PEPE")  ==0) coinColor=0x0646; // {0,200,50}    Pepe Green
    else if (strcmp(symbol,"BONK")  ==0) coinColor=0xFDA0; // {255,180,0}   Bonk Yellow
    else if (strcmp(symbol,"FLOKI") ==0) coinColor=0xFD20; // {255,164,0}   Floki Gold
    else if (strcmp(symbol,"WIF")   ==0) coinColor=0xB327; // {180,100,60}  dogwifhat Peach
    else if (strcmp(symbol,"BRETT") ==0) coinColor=0x443E; // {66,135,245}  Brett Blue
    else if (strcmp(symbol,"MOG")   ==0) coinColor=0xA15B; // {160,40,220}  Mog Violet
    else if (strcmp(symbol,"TURBO") ==0) coinColor=0xF220; // {240,70,0}    Turbo Orange-Red
    else if (strcmp(symbol,"POPCAT")==0) coinColor=0xFCF6; // {255,150,180} Popcat Pink
    else if (strcmp(symbol,"NEIRO") ==0) coinColor=0xFF36; // {255,220,170} Neiro Cream
    else if (strcmp(symbol,"GOAT")  ==0) coinColor=0x664C; // {100,200,100} Goat Sage Green
```

Keep the existing `// ── Category badge` block below it unchanged.

**Step B** — Replace the price formatting block (lines ~465–475) with exchange-style exact format:
```cpp
    // ── Exchange-style price: exact dollar.cent, with comma separator ─────────
    char priceStr[20];
    if (priceUsd >= 10000.0f) {
        uint32_t whole = (uint32_t)priceUsd;
        uint32_t cents = (uint32_t)((priceUsd - (float)whole) * 100.0f + 0.5f);
        snprintf(priceStr, sizeof(priceStr), "$%lu,%03lu.%02lu",
                 whole / 1000, whole % 1000, cents);
    } else if (priceUsd >= 1000.0f) {
        uint32_t whole = (uint32_t)priceUsd;
        uint32_t cents = (uint32_t)((priceUsd - (float)whole) * 100.0f + 0.5f);
        snprintf(priceStr, sizeof(priceStr), "$%lu,%03lu.%02lu",
                 whole / 1000, whole % 1000, cents);
    } else if (priceUsd >= 1.0f) {
        snprintf(priceStr, sizeof(priceStr), "$%.2f", priceUsd);
    } else if (priceUsd >= 0.01f) {
        snprintf(priceStr, sizeof(priceStr), "$%.4f", priceUsd);
    } else if (priceUsd >= 0.000001f) {
        snprintf(priceStr, sizeof(priceStr), "$%.8f", priceUsd);
    } else {
        snprintf(priceStr, sizeof(priceStr), "$%.2e", priceUsd);
    }
    _sprite->setTextColor(coinColor, COLOR_BG);
    uint8_t pFont = (strlen(priceStr) <= 9) ? 2 : 1;
    _sprite->drawString(priceStr, 8, 36, pFont);
```

---

### CHANGE 3 — LED Always Tracks Current Coin Color

**File:** `/home/mr-reaper/Arduino Projects/crypto-tkey-s3/crypto-tkey-s3.ino`

Replace `renderCurrentPortfolioCard()` function body (around lines 374–381):

```cpp
void renderCurrentPortfolioCard() {
    CoinAsset* coin = PortfolioManager::getCurrentCoin();
    if (coin) {
        // ── Always sync LED to coin brand color ───────────────────────
        RgbColor c = RgbStatus::getCoinRgb(coin->symbol);
        rgb.setCoinColor(c.r, c.g, c.b);   // activates LED_MODE_COIN_GLOW
        // ── Render LCD portfolio card ──────────────────────────────────
        ui.renderPortfolioCard(coin->symbol, coin->name, coin->balance,
                               coin->priceUsd, coin->change24h,
                               PortfolioManager::getCurrentIndex(),
                               PortfolioManager::getActiveCount(),
                               PortfolioManager::getTotalValueUsd(),
                               PortfolioManager::isLive());
    }
}
```

---

### After All 3 Changes — Compile & Flash

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 "/home/mr-reaper/Arduino Projects/crypto-tkey-s3/crypto-tkey-s3.ino" 2>&1 | tail -8
```

If compile passes:
```bash
sg dialout -c 'arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3 "/home/mr-reaper/Arduino Projects/crypto-tkey-s3/crypto-tkey-s3.ino"' 2>&1 | tail -6
```

> **Note:** If upload fails with "No serial data received", the device needs manual bootloader entry:
> Hold GPIO 0 button → unplug USB → replug USB → release after 1s → re-run upload.

After flash — restart the daemon:
```bash
systemctl --user restart tkey-tracker.service
sleep 5
journalctl --user -u tkey-tracker.service --no-pager -n 10
```

Then verify: navigate to portfolio tracker on device, prices should update live every ~1s, each coin should show its exact brand color on BOTH the LED and LCD, and the price should show exact dollar-and-cent format (e.g., `$86,533.50`).

---

### Git Commit After Verification
```bash
cd "/home/mr-reaper/Arduino Projects/crypto-tkey-s3"
git add -A
git commit -m "feat: 1s live ticker auto-refresh, exchange-style price format, LED+LCD brand color sync (Milestone 180)"
git push origin main
rsync -a --delete "/home/mr-reaper/Arduino Projects/crypto-tkey-s3/" "/mnt/sdcard/Projects/Arduino Projects/crypto-tkey-s3/"
```

Then update `~/.agents/tasks/TASKS.md` — mark Milestone 180 `[x]` and add Milestone 181 `[ ]` (ETH Keccak-256 fix + SOL BIP-44 path fix).

---

## ⚠️ DO NOT

- Do NOT touch `src/crypto_coins.h/cpp` — 44-coin registry is complete and correct
- Do NOT touch `src/rgb_status.cpp` — `getCoinRgb()` was just rewritten with all 44 brand colors
- Do NOT touch `tools/crypto_tracker_daemon.py` — WebSocket daemon is live and working
- Do NOT re-flash without compiling first
- Do NOT change the serial port from `/dev/ttyACM0`
- Do NOT modify the `SupportedCoinId` enum ordering — NVS preferences keys are index-based
