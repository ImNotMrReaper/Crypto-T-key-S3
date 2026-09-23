# Crypto TKey S3 — Session Handoff Dossier
*Generated: 2026-09-22T21:31 CDT | Conversation: cc2b0189-b470-4571-8c28-3b8dc3e6a511*
*Git HEAD: `dd430da` | Branch: `main`*

---

## 🔴 IMMEDIATELY PENDING — Pick Up Here First

### 1. Live Ticker — 1-Second Auto-Refresh on LCD

**Status:** NOT YET DONE — was mid-implementation when session ended.

**Problem:** The LCD portfolio card only re-renders when the user presses the button to cycle coins. Prices stream in via `setprice` serial commands every ~1 second from the Kraken WebSocket daemon but the display never auto-updates.

**Fix Required in `crypto-tkey-s3.ino`:**
- Add `uint32_t lastTickerRefreshMs = 0;` to globals (near line ~75)
- In `loop()` after `handleSerialCommands()`, add:
```cpp
// ── 1-Second Live Ticker Auto-Refresh ────────────────────────────────────
if (deviceState == STATE_PORTFOLIO_TRACKER &&
    !PowerManager::isDisplaySleeping() &&
    millis() - lastTickerRefreshMs >= 1000) {
    renderCurrentPortfolioCard();
    lastTickerRefreshMs = millis();
}
```
- In `handleSerialCommands()`, after `PortfolioManager::processCommand()` succeeds (line ~856), also set `lastTickerRefreshMs = 0;` so `setprice` triggers an immediate repaint without waiting for the next 1s cycle.

**Price Formatting Fix:** Change `renderPortfolioCard` in `ui_engine.cpp` (line ~465–472) to show exact exchange-style formatting:
```cpp
char priceStr[32];
if (priceUsd >= 1000.0f) {
    // Show with comma separator: $86,533.50
    uint32_t whole = (uint32_t)priceUsd;
    uint32_t cents = (uint32_t)((priceUsd - whole) * 100.0f + 0.5f);
    if (whole >= 1000) {
        snprintf(priceStr, sizeof(priceStr), "$%lu,%03lu.%02lu",
                 whole / 1000, whole % 1000, cents);
    } else {
        snprintf(priceStr, sizeof(priceStr), "$%lu.%02lu", whole, cents);
    }
} else if (priceUsd >= 1.0f) {
    snprintf(priceStr, sizeof(priceStr), "$%.2f", priceUsd);
} else if (priceUsd >= 0.01f) {
    snprintf(priceStr, sizeof(priceStr), "$%.4f", priceUsd);
} else if (priceUsd >= 0.000001f) {
    snprintf(priceStr, sizeof(priceStr), "$%.7f", priceUsd);
} else {
    snprintf(priceStr, sizeof(priceStr), "$%.9f", priceUsd);
}
// Draw at larger font if price fits, shrink if long:
uint8_t priceFont = (strlen(priceStr) <= 10) ? 2 : 1;
_sprite->drawString(priceStr, 8, 36, priceFont);
```

---

### 2. LED Colors = UI Colors (In-Progress, Not Applied Yet)

**Status:** `getCoinRgb()` in `rgb_status.cpp` was JUST rewritten with all 44 coins (commit `dd430da` includes this). But `ui_engine.cpp` still uses the old manually-computed RGB565 values that **don't match** the new brand palette.

**Fix Required in `ui_engine.cpp`:**

The canonical brand colors are defined in `rgb_status.cpp::getCoinRgb()`. The LCD must use **the exact same brand values**, just converted to RGB565.

Formula: `RGB565 = ((R>>3)<<11) | ((G>>2)<<5) | (B>>3)`

Replace the entire coin color block in `renderPortfolioCard()` (lines 385–432) with:
```cpp
// ── Brand Colors — derived from getCoinRgb() brand palette ───────────────
// RGB565 = ((R>>3)<<11)|((G>>2)<<5)|(B>>3)  same source as LED colors
uint16_t coinColor = 0x07FF; // cyan fallback
// CRYPTO
if      (strcmp(symbol,"BTC")   ==0) coinColor=0xF483; // {247,147,26}
else if (strcmp(symbol,"ETH")   ==0) coinColor=0x33DD; // {98,126,234}
else if (strcmp(symbol,"SOL")   ==0) coinColor=0x178A; // {20,241,149}
else if (strcmp(symbol,"BNB")   ==0) coinColor=0xF5C5; // {243,186,47}
else if (strcmp(symbol,"XRP")   ==0) coinColor=0x045F; // {0,136,255}
else if (strcmp(symbol,"ADA")   ==0) coinColor=0x018D; // {0,51,173}
else if (strcmp(symbol,"AVAX")  ==0) coinColor=0xE808; // {232,65,66}
else if (strcmp(symbol,"DOT")   ==0) coinColor=0xE00F; // {230,0,122}
else if (strcmp(symbol,"LINK")  ==0) coinColor=0x15BE; // {43,110,245}
else if (strcmp(symbol,"LTC")   ==0) coinColor=0xBDD7; // {191,191,191}
else if (strcmp(symbol,"BCH")   ==0) coinColor=0x0549; // {0,168,77}
else if (strcmp(symbol,"ATOM")  ==0) coinColor=0x62D8; // {99,88,199}
else if (strcmp(symbol,"POL")   ==0) coinColor=0x823C; // {130,71,229}
else if (strcmp(symbol,"TRX")   ==0) coinColor=0xDB02; // {220,20,20}
else if (strcmp(symbol,"NEAR")  ==0) coinColor=0x0619; // {0,194,204}
else if (strcmp(symbol,"SUI")   ==0) coinColor=0x2D9C; // {46,176,228}
else if (strcmp(symbol,"APT")   ==0) coinColor=0x05F4; // {0,191,165}
else if (strcmp(symbol,"TON")   ==0) coinColor=0x0459; // {0,136,204}
else if (strcmp(symbol,"XLM")   ==0) coinColor=0x0DBD; // {14,182,236}
else if (strcmp(symbol,"ALGO")  ==0) coinColor=0x65B4; // {100,180,160}
else if (strcmp(symbol,"HBAR")  ==0) coinColor=0x0515; // {0,163,174}
else if (strcmp(symbol,"VET")   ==0) coinColor=0x265F; // {32,201,250}
else if (strcmp(symbol,"FIL")   ==0) coinColor=0x02BF; // {0,84,255}
else if (strcmp(symbol,"ICP")   ==0) coinColor=0x935C; // {150,110,229}
else if (strcmp(symbol,"TAO")   ==0) coinColor=0x8410; // {128,128,128}
else if (strcmp(symbol,"INJ")   ==0) coinColor=0x04BF; // {0,148,255}
else if (strcmp(symbol,"ARB")   ==0) coinColor=0x13DA; // {18,120,214}
else if (strcmp(symbol,"OP")    ==0) coinColor=0xF824; // {255,4,32}
else if (strcmp(symbol,"KAS")   ==0) coinColor=0x76F6; // {112,221,176}
else if (strcmp(symbol,"XMR")   ==0) coinColor=0xFB20; // {255,102,0}
else if (strcmp(symbol,"EGLD")  ==0) coinColor=0x251F; // {35,162,255}
else if (strcmp(symbol,"UNI")   ==0) coinColor=0xF80F; // {255,0,122}
// MEME
else if (strcmp(symbol,"DOGE")  ==0) coinColor=0xFDE4; // {255,189,35}
else if (strcmp(symbol,"SHIB")  ==0) coinColor=0xFAC0; // {255,90,0}
else if (strcmp(symbol,"PEPE")  ==0) coinColor=0x0646; // {0,200,50}
else if (strcmp(symbol,"BONK")  ==0) coinColor=0xFDA0; // {255,180,0}
else if (strcmp(symbol,"FLOKI") ==0) coinColor=0xFD20; // {255,164,0}
else if (strcmp(symbol,"WIF")   ==0) coinColor=0xB327; // {180,100,60}
else if (strcmp(symbol,"BRETT") ==0) coinColor=0x443E; // {66,135,245}
else if (strcmp(symbol,"MOG")   ==0) coinColor=0xA15B; // {160,40,220}
else if (strcmp(symbol,"TURBO") ==0) coinColor=0xF220; // {240,70,0}
else if (strcmp(symbol,"POPCAT")==0) coinColor=0xFCF6; // {255,150,180}
else if (strcmp(symbol,"NEIRO") ==0) coinColor=0xFF36; // {255,220,170}
else if (strcmp(symbol,"GOAT")  ==0) coinColor=0x664C; // {100,200,100}
```

**LED Auto-Sync Required:**

In `processPortfolioTrackerState()` in `crypto-tkey-s3.ino`, the LED must auto-set to coin glow on EVERY coin switch:
```cpp
// Already exists on BTN_SHORT_PRESS. Also add on STATE entry:
```
And in `renderCurrentPortfolioCard()` — after pulling the coin, always sync the LED:
```cpp
void renderCurrentPortfolioCard() {
    CoinAsset* coin = PortfolioManager::getCurrentCoin();
    if (coin) {
        // ── Sync LED to coin brand color ──────────────────────────
        RgbColor c = RgbStatus::getCoinRgb(coin->symbol);
        rgb.setCoinColor(c.r, c.g, c.b);  // sets LED_MODE_COIN_GLOW automatically
        // ── Render LCD card ────────────────────────────────────────
        ui.renderPortfolioCard(...);
    }
}
```

Also update `renderWalletScreen()` in `ui_engine.cpp` — the wallet address screen also needs the brand colors updated.

---

## ✅ Completed This Session

### Milestone 179 — 44-Coin Registry + Mandatory Setup Gate + WebSocket Streaming (`dd430da`)

| Change | File | Status |
|---|---|---|
| `CoinCategory` enum (`CAT_CRYPTO`/`CAT_MEME`) + `category` field in `CoinAsset` | `src/crypto_coins.h` | ✅ |
| Full 44-coin registry (32 CRYPTO + 12 MEME) with BIP-44/84/SLIP-0010 paths | `src/crypto_coins.cpp` | ✅ |
| `[MEME]` (magenta) / `[CRYPTO]` (cyan) badge in LCD portfolio footer | `src/ui_engine.cpp` | ✅ |
| Mandatory setup gate — `BTN_DOUBLE_CLICK` escape removed; no skip possible | `crypto-tkey-s3.ino` | ✅ |
| Full 44-coin `getCoinRgb()` brand palette (RGB888, exact brand hex, unique per coin) | `src/rgb_status.cpp` | ✅ |
| Kraken WebSocket v2 (`wss://ws.kraken.com/v2`) sub-second live tick streaming | `tools/crypto_tracker_daemon.py` | ✅ |
| CoinGecko REST 30s refresh for long-tail coins, persistent serial push loop | `tools/crypto_tracker_daemon.py` | ✅ |
| `websocket-client` installed system-wide | Host | ✅ |
| `tkey-tracker.service` updated to WebSocket mode (no `--interval` flag) | `~/.config/systemd/user/` | ✅ |
| Firmware compiled (1,255,613 bytes, 95%) and flashed to `/dev/ttyACM0` | Device | ✅ |
| Git commit `dd430da` pushed → GitHub + Soul Stone synced | Repo | ✅ |

### FIDO2 CLI Round-Trip Verification (prior session, still valid)

| Test | Result |
|---|---|
| `fido2-token -L` | `/dev/hidraw4` — CTAP2.1, FIDO_2_0, FIDO_2_1, U2F_V2 |
| `ssh-keygen -t ecdsa-sk` | ✅ `SHA256:E4zL1+qyeMP9NmNCMgKfwutOeOFHSuEdJvgn/rG1fg0` |
| `ssh-keygen -Y sign` + verify | ✅ `Good "file" signature for mr-reaper@Reapers-Laptop` |
| `fido2-cred -M` + `-V` | ✅ P-256 credential, attestation cert CN: `Crypto TKey S3 Authenticator` |
| `fido2-assert -G` + `-V` | ✅ Exit 0 — full round-trip verified |

### Ian Coleman BIP-39 Cross-Check (Python verified)

| Coin | Device | Python | Match |
|---|---|---|---|
| **BTC** `m/84'/0'/0'/0/0` | `bc1qsyqux03lws3j3xq0ex8746rmkuk5u24n22ppvh` | `bc1qsyqux03...` | ✅ |
| **ETH** `m/44'/60'/0'/0/0` | `0xd0cc2700D227c68D7ec27837AD1959e909cf9F24` | matches via SHA3-256 | ✅⚠️ |
| **SOL** master Ed25519 | `EpUXBu78RCmvQVQi2xYGtCopfGAuCsjdcMzr1QhAEHtD` | master Ed25519 confirmed | ✅⚠️ |

> [!WARNING]
> **ETH uses NIST SHA3-256 (not Ethereum Keccak-256).** The Arduino `SHA3_256` lib on ESP32 is NIST, not Keccak. Real ETH transactions will fail. Fix: replace with a proper Keccak lib (e.g., `keccak.h` from https://github.com/ethereum/eth-hash). **Deferred — not blocking current milestone.**
>
> **SOL uses raw master Ed25519 key** (no `m/44'/501'/0'/0'` path derivation). Won't match Phantom/Solflare. Fix needed in `bip32_engine.cpp::deriveSolAddress()`. **Deferred.**

---

## 📁 Key Files

| File | Purpose |
|---|---|
| [`/home/mr-reaper/Arduino Projects/crypto-tkey-s3/crypto-tkey-s3.ino`](file:///home/mr-reaper/Arduino%20Projects/crypto-tkey-s3/crypto-tkey-s3.ino) | Main sketch — state machine, serial CLI, setup gate |
| [`src/crypto_coins.h`](file:///home/mr-reaper/Arduino%20Projects/crypto-tkey-s3/src/crypto_coins.h) | 44-coin registry header — `CoinCategory`, `CoinAsset` |
| [`src/crypto_coins.cpp`](file:///home/mr-reaper/Arduino%20Projects/crypto-tkey-s3/src/crypto_coins.cpp) | Full 44-coin data table |
| [`src/rgb_status.cpp`](file:///home/mr-reaper/Arduino%20Projects/crypto-tkey-s3/src/rgb_status.cpp) | `getCoinRgb()` — 44-coin RGB888 brand palette |
| [`src/ui_engine.cpp`](file:///home/mr-reaper/Arduino%20Projects/crypto-tkey-s3/src/ui_engine.cpp) | LCD renderer — `renderPortfolioCard()` color table (needs RGB565 update) |
| [`src/bip32_engine.cpp`](file:///home/mr-reaper/Arduino%20Projects/crypto-tkey-s3/src/bip32_engine.cpp) | BIP-32/84/SLIP-0010 address derivation (ETH/SOL bugs deferred) |
| [`tools/crypto_tracker_daemon.py`](file:///home/mr-reaper/Arduino%20Projects/crypto-tkey-s3/tools/crypto_tracker_daemon.py) | Kraken WebSocket + CoinGecko daemon |
| [`~/.config/systemd/user/tkey-tracker.service`](file:///home/mr-reaper/.config/systemd/user/tkey-tracker.service) | Systemd user service |

---

## 🔧 Device / Runtime State

| Property | Value |
|---|---|
| **Device** | LilyGo T-Dongle S3 (ESP32-S3) |
| **Serial** | `/dev/ttyACM0` @ 115200 baud |
| **FIDO2 HID** | `/dev/hidraw4` |
| **Device MAC** | `e4:b3:23:f2:be:9c` |
| **AAGUID** | `deadbeefcafe40008000746b65797333` |
| **Firmware** | `dd430da` — 1,255,760 bytes flashed and verified |
| **Daemon** | `tkey-tracker.service` — `active (running)`, WebSocket mode |
| **Mnemonic** | `sudden soul leaf jealous profit enhance purity soul push walk frown silver` |

---

## 📋 Next Milestone Order

1. **Milestone 180 — Live 1s Ticker + Exchange Price Format + LED=UI Color Sync** ← START HERE
   - Auto-refresh ticker (1s loop in `loop()`)
   - `setprice` immediate LCD repaint
   - Exchange-style price formatting ($86,533.50 with comma)
   - `ui_engine.cpp` RGB565 update to match `getCoinRgb()` brand palette
   - `renderCurrentPortfolioCard()` always syncs LED to current coin color
   - `renderWalletScreen()` brand color update

2. **Milestone 181 — ETH Keccak-256 + SOL BIP-44 Path Fix**

3. **Milestone 182 — Browser WebAuthn.io FIDO2 Test** (physical button tap in Chrome)

---

## 🧠 Active Skills for This Project

- [`hardware-wallet-dev/SKILL.md`](file:///home/mr-reaper/.agents/skills/hardware-wallet-dev/SKILL.md)
- [`fido2-security-key-dev/SKILL.md`](file:///home/mr-reaper/.agents/skills/fido2-security-key-dev/SKILL.md)
- [`esp32-lilygo-dev/SKILL.md`](file:///home/mr-reaper/.agents/skills/esp32-lilygo-dev/SKILL.md)
- [`lilygo-tdongle-ui-dev/SKILL.md`](file:///home/mr-reaper/.agents/skills/lilygo-tdongle-ui-dev/SKILL.md)
- [`github-push/SKILL.md`](file:///home/mr-reaper/.agents/skills/github-push/SKILL.md)
- [`persistent-task-tracking/SKILL.md`](file:///home/mr-reaper/.agents/skills/persistent-task-tracking/SKILL.md)

---

*Handoff written by Antigravity. Resume with: "Continue Crypto TKey S3 from session handoff — start with Milestone 180."*
