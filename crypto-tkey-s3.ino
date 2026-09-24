/**
 * crypto-tkey-s3.ino — Master Orchestrator for Crypto TKey S3 Authenticator & Vault
 * ==============================================================================
 * Hardware: LilyGo T-Dongle S3 (ESP32-S3, ST7735 0.96" TFT, APA102 RGB, microSD)
 * Standard: Follows lilygo-tdongle-ui-dev, fido2-security-key-dev, and hardware-wallet-dev skills.
 * 
 * Core Features:
 *   1. Zero-Flicker Double-Buffered UI (160x80 ST7735, 14px Header/52px Card/14px Footer)
 *   2. Pure FIDO2 / CTAPHID Security Key (1-Tap touch for instant WebAuthn logins)
 *   3. 24-Cryptocurrency Dynamic Asset Registry & Live Portfolio Tracker (USD values)
 *   4. Hybrid Entropy BIP-39 Seed Generator (TRNG + Button Jitter Conditioning)
 *   5. Zero-RF Thermal Throttling (80MHz dynamic clock, RF disabled, <35mA total draw)
 *   6. Duress Panic Nuke (>6s panic hold or PIN 9999 triggers full flash scrub)
 *   7. WebUSB & USB Serial Companion Bridge for instant live CoinGecko sync
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <nvs.h>
#include "USB.h"
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_private/brownout.h"

__attribute__((constructor(101))) void pre_init_early() {
    esp_brownout_disable();
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

#include "src/config.h"
#include "src/power_mgr.h"
#include "src/button_cadence.h"
#include "src/rgb_status.h"
#include "src/ui_engine.h"
#include "src/duress_wipe.h"
#include "src/crypto_wallet.h"
#include "src/ctaphid.h"
#include "src/ctap2.h"
#include "src/crypto_coins.h"
#include "src/seed_gen.h"
#include "src/pin_vault.h"
#include "src/portfolio_mgr.h"
#include "src/wifi_manager.h"
#include "src/web_portal.h"
#include "src/psbt_signer.h"
#include "src/evm_decoder.h"
#include "src/sd_vault.h"
#include "src/ui_theme.h"
#include "src/wallet_families.h"
#include "src/bip32_engine.h"
#include <mbedtls/platform_util.h>

// ─── Subsystem Allocations (Dynamic Initialization) ──────────────────────────
TFT_eSPI*     tft    = nullptr;
RgbStatus     rgb;
ButtonCadence btn;
UiEngine      ui;
CryptoWallet* wallet = nullptr;
WifiManager*  wifi   = nullptr;
WebPortal*    portal = nullptr;
Preferences   vaultPrefs;

// ─── Global State ────────────────────────────────────────────────────────────
DeviceState   deviceState = STATE_IDLE_READY;
int           masterPinLen = 4;   // digits of the vault PIN (the hash lives in PinVault)
uint8_t       dispRotation = DISP_ROTATION;
char          pinDigits[PIN_LENGTH + 1] = "0000";
int           pinIndex = 0;
int           currentDigitVal = 0;
uint8_t       lastHoldStage = 0;
uint32_t      lastStateUpdate = 0;
uint32_t      lastTickerRefreshMs = 0;   // 1s live ticker auto-refresh
CryptoCoin    currentViewCoin = COIN_BTC;
bool          s_simulatedTouch = false;
ButtonEvent   s_injectedEvent = BTN_NONE;   // test builds: remote button presses

// Seed Generator State
char          generatedMnemonic[240] = {0};
char          mnemonicWords[24][16];
int           totalMnemonicWords = 12;
int           currentWordIdx = 0;

// On-Device Verified Seed Words Viewer State
char          verifiedSeedWords[24][16];
int           totalSeedWords = 12;
int           currentSeedViewIdx = 0;

// Air-Gap PSBT Signer State
char          psbtFilePath[64] = "";
PsbtTxDetails currentPsbt;
bool          psbtLoaded = false;

// Temporary buffers for active requests
char reqDomain[48]     = "webauthn.io";
char reqChain[24]      = "Bitcoin Mainnet";
char reqRecipient[48]  = "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh";
char reqAmount[32]     = "0.054 BTC ($3,450)";

// ─── Forward Declarations ───────────────────────────────────────────────────
void launchSetupPortal();
void handleSerialCommands();
void processIdleReadyState(ButtonEvent ev);
void processPasskeyHubState(ButtonEvent ev);
void processPinEntryState(ButtonEvent ev);
void processVaultDashboardState(ButtonEvent ev);
void processSeedWordsViewState(ButtonEvent ev);
void processPortfolioTrackerState(ButtonEvent ev);
void processSeedEntropyState(ButtonEvent ev);
void processSeedWordDisplayState(ButtonEvent ev);
void processAirGapSdSignState(ButtonEvent ev);
void scanAndRenderAirGapPsbt();
void parseWalletSeedWords();
void resetPinEntry();
void startSeedGeneration(int wordCount);
void loadSecurityConfig();
bool handleUserPresencePrompt(uint32_t cid, const char* rpId, bool isRegistration);
void renderCurrentPortfolioCard();
void showReceiveScreen(const char* symbol, const char* hint);

void showPortalScreen() {
    vaultPrefs.begin("vault_sec", true);
    bool provisioned = vaultPrefs.getBool("provisioned", false);
    vaultPrefs.end();
    ui.renderPortalScreen(portal->apSsid(), portal->apPass(), portal->wifiQr(),
                          provisioned ? "HOLD 3s: EXIT" : "SCAN TO JOIN");
}

void launchSetupPortal() {
    vaultPrefs.begin("vault_sec", true);
    bool isProvisioned = vaultPrefs.getBool("provisioned", false);
    vaultPrefs.end();

    deviceState = STATE_SETUP_WALKTHROUGH;
    if (wifi) wifi->setPortalActive(true);
    portal->begin(wallet, wifi, isProvisioned);
    showPortalScreen();
    rgb.setMode(LED_MODE_SOFTAP_PULSE);
}

// Leaves the setup portal: radio off, back to the home screen.
void closeSetupPortal(bool saved) {
    portal->stop();
    if (wifi) wifi->setPortalActive(false);
    if (saved) {
        masterPinLen = PinVault::length();
        if (wallet) wallet->refreshFamilies();   // addresses only for the selected coins
        PortfolioManager::resetIndex();
        rgb.flashRainbow(800);
        ui.renderSuccessBanner("SETTINGS SAVED", "WI-FI OFF");
        delay(1500);
    }
    deviceState = STATE_IDLE_READY;
    rgb.setMode(LED_MODE_HOME);
    const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
    ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // 1. Initialize Thermal & Power Management (80MHz, RF disabled)
    PowerManager::init();

    // Enumerate as a FIDO2 key first so browsers see it immediately (the rest of boot
    // takes seconds). Requests queue in CTAPHID until loop() starts servicing them, and
    // authenticatorReset's 10 s power-up window stays usable.
    ctapHid.begin();
    ctap2Engine.begin();
    ctap2Engine.setUserPresencePrompt(handleUserPresencePrompt);

    Serial.begin(115200);
    delay(600);

    Serial.println("\n[BOOT] ===== CRYPTO TKEY S3 INITIALIZATION =====");
    Serial.printf("[BOOT] Reset reason: %d (1=power-on 3=software 4=panic 5=int-wdt 6=task-wdt 7=wdt 9=brownout)\n",
                  (int)esp_reset_reason());
    Serial.printf("[BOOT] CPU Clock: %d MHz | RF: Disabled (Thermal Throttled)\n", getCpuFrequencyMhz());

    // 2. Hardware Peripherals
    btn.begin(PIN_BTN);
    rgb.begin(PIN_LED_DATA, PIN_LED_CLK);
    rgb.setMode(LED_MODE_HOME);
    rgb.update();

    // 3. Security Config (load screen rotation and master PIN)
    loadSecurityConfig();

    // 4. Display Engine (lilygo-tdongle-ui-dev double-buffered)
    tft = new TFT_eSPI();
    tft->init();
    tft->setRotation(dispRotation);
    ui.begin(tft);
    ui.setRotation(dispRotation);

    // 5. Boot Splash
    ui.renderBootSplash();
    for (int i = 0; i < 25; i++) {
        rgb.update();
        delay(20);
    }

    // 6. Security & Vault Engines
    wallet = new CryptoWallet();
    wallet->begin();

    ctapHid.setWinkHandler([](uint32_t cid) {
        Serial.printf("[FIDO2] 😉 WINK identification triggered on CID 0x%08X!\n", cid);
        rgb.flashRainbow(1500);
        if (deviceState == STATE_IDLE_READY) {
            ui.renderSuccessBanner("DEVICE LOCATED", "WINK VERIFIED");
        }
    });

    // 7. Portfolio & Seed Engines
    PortfolioManager::init();
    homeTheme.load();
    // Price ticks light the LED: the coin on screen flashes on its own moves; the home screen
    // flashes when the whole portfolio's value moves. (A lambda: Arduino's generated
    // prototypes would precede the CoinAsset type.)
    CryptoCoinRegistry::onPriceTick = [](const CoinAsset* coin, float pctMove) {
        if (!coin->enabled || PowerManager::isDisplaySleeping()) return;
        if (deviceState == STATE_PORTFOLIO_TRACKER) {
            if (coin == PortfolioManager::getCurrentCoin()) rgb.priceTick(pctMove);
        } else if (deviceState == STATE_IDLE_READY && coin->balance > 0.0f) {
            float total = PortfolioManager::getTotalValueUsd();
            if (total > 0.0f) rgb.priceTick(pctMove * coin->balance * coin->priceUsd / total);   // weighted by holdings
        }
    };
    SeedGenerator::init();
    wifi = new WifiManager();
    wifi->begin();
    portal = new WebPortal();

    vaultPrefs.begin("vault_sec", true);
    bool isProvisioned = vaultPrefs.getBool("provisioned", false);
    vaultPrefs.end();

    if (!isProvisioned || btn.isPressedNow()) {
        launchSetupPortal();
    } else {
        deviceState = STATE_IDLE_READY;
        const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
        ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        rgb.setMode(LED_MODE_HOME);
    }

    ctap2Engine.markReady();
    Serial.println("[BOOT] ✅ Crypto TKey S3 Ready. 3-Screen Architecture Active.");
    Serial.println("========================================================\n");
}

// ─── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
    ButtonEvent ev = btn.update();
    if (ev == BTN_NONE && s_injectedEvent != BTN_NONE) {
        ev = s_injectedEvent;
        s_injectedEvent = BTN_NONE;
    }
    rgb.update();
    ctapHid.process();
    if (wifi) wifi->update();

    if (portal && portal->isRunning()) {
        portal->update();
        if (portal->isSetupDone()) closeSetupPortal(true);
        else if (portal->isExitRequested()) closeSetupPortal(false);
    }

    bool userActive = (ev != BTN_NONE);
    if (userActive && PowerManager::isDisplaySleeping() && ev != BTN_PANIC_HOLD) {
        PowerManager::update(true);   // wake only; don't act on a press the user couldn't see
        ev = BTN_NONE;
    }
    PowerManager::update(userActive);

    handleSerialCommands();

    // ── 1-Second Live Ticker Auto-Refresh ────────────────────────────────────
    if (deviceState == STATE_PORTFOLIO_TRACKER &&
        !PowerManager::isDisplaySleeping() &&
        millis() - lastTickerRefreshMs >= 1000) {
        renderCurrentPortfolioCard();
        lastTickerRefreshMs = millis();
    }

    switch (deviceState) {
        case STATE_SETUP_WALKTHROUGH: {
            // First-time setup can't be skipped; an already set-up key can leave without saving.
            if (ev == BTN_VERY_LONG_PRESS) {
                vaultPrefs.begin("vault_sec", true);
                bool provisioned = vaultPrefs.getBool("provisioned", false);
                vaultPrefs.end();
                if (provisioned) closeSetupPortal(false);
            } else if (ev == BTN_SHORT_PRESS || ev == BTN_LONG_PRESS) {
                showPortalScreen();   // redraw (e.g. after the display woke up)
            }
            break;
        }
        case STATE_IDLE_READY:
            processIdleReadyState(ev);
            break;
        case STATE_PASSKEY_HUB:
            processPasskeyHubState(ev);
            break;
        case STATE_PORTFOLIO_TRACKER:
            processPortfolioTrackerState(ev);
            break;
        case STATE_PIN_ENTRY:
            processPinEntryState(ev);
            break;
        case STATE_VAULT_DASHBOARD:
            processVaultDashboardState(ev);
            break;
        case STATE_SEED_WORDS_VIEW:
            processSeedWordsViewState(ev);
            break;
        case STATE_SEED_ENTROPY_COLLECT:
            processSeedEntropyState(ev);
            break;
        case STATE_SEED_WORD_DISPLAY:
            processSeedWordDisplayState(ev);
            break;
        case STATE_AIRGAP_SD_SIGN:
            processAirGapSdSignState(ev);
            break;
        case STATE_RECEIVE_QR:
            if (ev == BTN_SHORT_PRESS || ev == BTN_DOUBLE_CLICK || ev == BTN_LONG_PRESS ||
                ev == BTN_VERY_LONG_PRESS) {
                deviceState = STATE_PORTFOLIO_TRACKER;
                renderCurrentPortfolioCard();
            } else if (ev == BTN_PANIC_HOLD) {
                DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
            }
            break;
        case STATE_DURESS_WIPED:
            delay(100);
            break;
        default:
            break;
    }

    delay(5);
}

// ─── Screen 1: Base Home Screen (Clock, Wi-Fi, System Status) ───────────────
void processIdleReadyState(ButtonEvent ev) {
    if (millis() - lastStateUpdate > 3000 && !PowerManager::isDisplaySleeping()) {
        const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
        ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        lastStateUpdate = millis();
    }

    if (ev == BTN_SHORT_PRESS) {
        // Single tap switches to Screen 2: Dedicated Passkey Authentication Hub!
        deviceState = STATE_PASSKEY_HUB;
        rgb.setMode(LED_MODE_BREATHE_GREEN);
        ui.renderPasskeyHub(false, nullptr, 1.0f);
        Serial.println("[NAV] Switched to Screen 2: Passkey Authentication Hub");
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Double click toggles 180° Screen Rotation!
        dispRotation = (dispRotation == 1) ? 3 : 1;
        tft->setRotation(dispRotation);
        ui.setRotation(dispRotation);
        vaultPrefs.begin("vault_sec", false);
        vaultPrefs.putUChar("disp_rot", dispRotation);
        vaultPrefs.end();
        const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
        ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        rgb.flashDoubleTap(0, 229, 255);
        Serial.printf("[SCREEN] Flipped orientation to rotation %d\n", dispRotation);
    } else if (ev == BTN_LONG_PRESS) {
        // Long press dims / toggles sleep
        PowerManager::toggleDisplaySleep();
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── Screen 2: Passkey Authentication Hub (Dedicated WebAuthn / FIDO2) ──────
void processPasskeyHubState(ButtonEvent ev) {
    if (millis() - lastStateUpdate > 4000 && !PowerManager::isDisplaySleeping()) {
        ui.renderPasskeyHub(false, nullptr, 1.0f);
        lastStateUpdate = millis();
    }

    if (ev == BTN_SHORT_PRESS) {
        // Single tap switches to Screen 3: Crypto & Asset Hub!
        deviceState = STATE_PORTFOLIO_TRACKER;
        PowerManager::setKeepAwake(true); // Keep display awake continuously on live price screen!
        CoinAsset* coin = PortfolioManager::getCurrentCoin();
        if (coin) {
            RgbColor c = RgbStatus::getCoinRgb(coin->symbol);
            rgb.flashTap(c.r, c.g, c.b, 60);
            rgb.setCoin(coin->symbol);
        }
        renderCurrentPortfolioCard();
        Serial.println("[NAV] Switched to Screen 3: Crypto & Asset Hub (Keep-Awake ON)");
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Double tap returns to Screen 1: Base Home Screen
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_HOME);
        const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
        ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        Serial.println("[NAV] Returned to Screen 1: Base Home Screen");
    } else if (ev == BTN_LONG_PRESS) {
        // Arm / Tactile indicator touch test
        rgb.flashRainbow(800);
        ui.renderSuccessBanner("FIDO2 ARMED", "READY FOR LOGIN");
        delay(800);
        ui.renderPasskeyHub(false, nullptr, 1.0f);
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── Screen 3: Crypto & Asset Hub (Public Prices & Balances) ────────────────
void renderCurrentPortfolioCard() {
    CoinAsset* coin = PortfolioManager::getCurrentCoin();
    if (coin) {
        // ── Always sync LED to active coin brand color ─────────────────
        rgb.setCoin(coin->symbol);

        ui.renderPortfolioCard(coin->symbol, coin->name, coin->balance, coin->priceUsd, coin->change24h,
                               PortfolioManager::getCurrentIndex(), PortfolioManager::getActiveCount(),
                               PortfolioManager::getTotalValueUsd(), PortfolioManager::isLive());
    }
}

void processPortfolioTrackerState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        // Next Coin in Carousel with dynamic LED color shift
        PortfolioManager::nextCoin();
        CoinAsset* coin = PortfolioManager::getCurrentCoin();
        if (coin) {
            RgbColor c = RgbStatus::getCoinRgb(coin->symbol);
            rgb.flashTap(c.r, c.g, c.b, 60);
            rgb.setCoin(coin->symbol);
        }
        renderCurrentPortfolioCard();
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Double tap returns to Screen 1: Base Home Screen and releases keep-awake
        PowerManager::setKeepAwake(false);
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_HOME);
        const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
        ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        Serial.println("[NAV] Exited Crypto Hub -> Base Home Screen");
    } else if (ev == BTN_VERY_LONG_PRESS) {
        // Hold 2-5 s: receive QR for this coin (public address, no PIN needed)
        CoinAsset* coin = PortfolioManager::getCurrentCoin();
        if (coin) {
            deviceState = STATE_RECEIVE_QR;
            showReceiveScreen(coin->symbol, "PRESS: BACK");
        }
    } else if (ev == BTN_LONG_PRESS) {
        // Long press opens Master PIN Gate to unlock Private Vault!
        PowerManager::setKeepAwake(false);
        deviceState = STATE_PIN_ENTRY;
        resetPinEntry();
        rgb.setMode(LED_MODE_SOLID_AMBER);
        ui.renderPinScreen(pinDigits, masterPinLen, pinIndex, currentDigitVal, 0);
        Serial.println("[VAULT] Entering Master PIN gate (4-8 digits)...");
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── State: Master PIN Gate (Dynamic 4-to-8 Digits) ─────────────────────────
void processPinEntryState(ButtonEvent ev) {
    if (btn.isPressedNow()) {
        uint8_t stage = btn.getHoldStage() * 33;
        if (stage != lastHoldStage) {
            lastHoldStage = stage;
            ui.renderPinScreen(pinDigits, masterPinLen, pinIndex, currentDigitVal, stage);
            rgb.setHoldProgress((float)stage / 100.0f);
        }
    } else if (lastHoldStage > 0) {
        lastHoldStage = 0;
        ui.renderPinScreen(pinDigits, masterPinLen, pinIndex, currentDigitVal, 0);
        rgb.setMode(LED_MODE_SOLID_AMBER);
    }

    if (ev == BTN_SHORT_PRESS) {
        currentDigitVal = (currentDigitVal + 1) % 10;
        pinDigits[pinIndex] = '0' + currentDigitVal;
        ui.renderPinScreen(pinDigits, masterPinLen, pinIndex, currentDigitVal, 0);
        rgb.flashTap(0, 229, 255, 60);
        Serial.printf("[PIN] Slot %d = %d\n", pinIndex + 1, currentDigitVal);
    } else if (ev == BTN_DOUBLE_CLICK) {
        rgb.flashDoubleTap(255, 140, 0);
        if (pinIndex > 0) {
            pinDigits[pinIndex] = '0';
            pinIndex--;
            currentDigitVal = pinDigits[pinIndex] - '0';
            ui.renderPinScreen(pinDigits, masterPinLen, pinIndex, currentDigitVal, 0);
        } else {
            // Cancel back to Crypto Hub
            deviceState = STATE_PORTFOLIO_TRACKER;
            PowerManager::setKeepAwake(true);
            renderCurrentPortfolioCard();
        }
    } else if (ev == BTN_LONG_PRESS) {
        pinDigits[pinIndex] = '0' + currentDigitVal;
        pinIndex++;

        if (pinIndex < masterPinLen) {
            currentDigitVal = 0;
            pinDigits[pinIndex] = '0';
            ui.renderPinScreen(pinDigits, masterPinLen, pinIndex, currentDigitVal, 0);
            rgb.flashTap(0, 255, 100, 100);
        } else {
            pinDigits[masterPinLen] = '\0';
            PinVault::Result pr = PinVault::check(pinDigits);
            CryptoWallet::secureZero(pinDigits, sizeof(pinDigits));

            if (pr == PinVault::DURESS) {
                DuressWipe::execute(*tft, rgb, "DURESS_PIN");
            } else if (pr == PinVault::LOCKED_OUT) {
                Serial.println("[AUTH] Too many wrong PINs: anti-hammering wipe");
                DuressWipe::execute(*tft, rgb, "PIN_ATTEMPTS");
            } else if (pr == PinVault::OK && !wallet->hasSeed()) {
                Serial.println("[VAULT] PIN accepted. No wallet yet: starting seed creation.");
                ui.renderSuccessBanner("CREATE YOUR WALLET", "TAP 12x FOR ENTROPY");
                delay(1500);
                startSeedGeneration(12);
            } else if (pr == PinVault::OK) {
                ui.renderSuccessBanner("PIN ACCEPTED", "UNLOCKING VAULT...");
                if (!wallet->unlock()) {
                    ui.renderErrorBanner("Seed Unreadable");
                    rgb.setMode(LED_MODE_STROBE_RED);
                    delay(1500);
                    deviceState = STATE_IDLE_READY;
                    rgb.setMode(LED_MODE_HOME);
                    ui.renderReadyDashboard(millis() / 1000, true, false);
                    return;
                }
                Serial.println("[VAULT] PIN accepted. Vault unlocked.");
                rgb.flashRainbow(800);
                ui.renderSuccessBanner("VAULT UNLOCKED", "CRYPTO SIGNER READY");
                delay(1200);

                deviceState = STATE_VAULT_DASHBOARD;
                showVaultCoinScreen(COIN_BTC);
            } else {
                Serial.printf("[AUTH] Wrong PIN (%u attempts left before wipe)\n", PinVault::attemptsLeft());
                char left[24];
                snprintf(left, sizeof(left), "%u TRIES LEFT", PinVault::attemptsLeft());
                ui.renderErrorBanner(PinVault::attemptsLeft() <= 3 ? left : "Wrong PIN Code");
                rgb.setMode(LED_MODE_STROBE_RED);
                delay(1200);
                resetPinEntry();
                rgb.setMode(LED_MODE_SOLID_AMBER);
                ui.renderPinScreen(pinDigits, masterPinLen, pinIndex, currentDigitVal, 0);
            }
        }
    } else if (ev == BTN_VERY_LONG_PRESS) {
        resetPinEntry();
        rgb.flashDoubleTap(255, 0, 0);
        ui.renderPinScreen(pinDigits, masterPinLen, pinIndex, currentDigitVal, 0);
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── Receive Addresses (QR) ──────────────────────────────────────────────────
// Explicit list: a coin gets a receive QR only if this device derives an address that
// is valid on its network. (The registry also tags TAO/INJ/BNB with EVM paths, but
// their address formats differ, so they are deliberately absent.)
// Receive QR for any selected coin: the address of its wallet family (EVM tokens share the
// ETH address, SPL tokens the SOL address, ...), labelled with the coin's network.
void showReceiveScreen(const char* symbol, const char* hint) {
    const CoinAsset* coin = CryptoCoinRegistry::findBySymbol(symbol);
    const char* addr = (coin && coin->enabled) ? coin->address : "";
    // BIP-173: an all-uppercase bech32 address is valid and encodes in the compact
    // alphanumeric QR mode (bigger modules, easier scan). Other chains are case-sensitive.
    char qrText[FAMILY_ADDR_LEN];
    strncpy(qrText, addr, sizeof(qrText) - 1);
    qrText[sizeof(qrText) - 1] = '\0';
    if (coin && (coin->meta->family == FAM_BTC || coin->meta->family == FAM_LTC)) {
        for (char* c = qrText; *c; c++) *c = toupper(*c);
    }
    if (coin && coin->enabled && !addr[0]) hint = wallet && wallet->hasSeed() ? "UNLOCK TO CREATE" : "NO WALLET YET";
    rgb.setCoin(symbol);
    ui.renderReceiveScreen(symbol, coin ? coin->meta->network : "Unsupported", addr, qrText, hint);
}

// ─── Helper: Unified Vault Coin Display with Dynamic LED & UI Theme Sync ─────
void showVaultCoinScreen(uint8_t coin) {
    currentViewCoin = (CryptoCoin)coin;
    const WalletAccount* acc = wallet->getAccount(currentViewCoin);
    const char* name = "BITCOIN";
    const char* sym  = "BTC (SegWit)";
    const char* lookupSym = "BTC";
    if (coin == COIN_ETH)  { name = "ETHEREUM / PEPE"; sym = "ETH (ERC-20)"; lookupSym = "ETH"; }
    if (coin == COIN_SOL)  { name = "SOLANA";          sym = "SOL (Ed25519)"; lookupSym = "SOL"; }
    if (coin == COIN_DOGE) { name = "DOGECOIN";        sym = "DOGE (Legacy)"; lookupSym = "DOGE"; }

    (void)name; (void)sym; (void)acc;
    showReceiveScreen(lookupSym, "tap: next");
}

// ─── State: Vault Dashboard (Private Derived Addresses Explorer) ────────────
void processVaultDashboardState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        currentViewCoin = (CryptoCoin)((currentViewCoin + 1) % 4);
        const char* lookupSym = "BTC";
        if (currentViewCoin == COIN_ETH)  lookupSym = "ETH";
        if (currentViewCoin == COIN_SOL)  lookupSym = "SOL";
        if (currentViewCoin == COIN_DOGE) lookupSym = "DOGE";
        RgbColor c = RgbStatus::getCoinRgb(lookupSym);
        rgb.flashTap(c.r, c.g, c.b, 60);
        showVaultCoinScreen(currentViewCoin);
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Double click launches the On-Device Verified Seed Words Viewer!
        deviceState = STATE_SEED_WORDS_VIEW;
        currentSeedViewIdx = 0;
        parseWalletSeedWords();
        ui.renderSeedWordsView(currentSeedViewIdx + 1, totalSeedWords, verifiedSeedWords[currentSeedViewIdx]);
        rgb.setMode(LED_MODE_SOLID_AMBER);
        Serial.println("[VAULT] Opened On-Device Recovery Seed Viewer (PIN Protected)");
    } else if (ev == BTN_LONG_PRESS) {
        // Long press locks vault and returns to Screen 3: Crypto Hub
        wallet->lock();
        deviceState = STATE_PORTFOLIO_TRACKER;
        PowerManager::setKeepAwake(true);
        CoinAsset* coin = PortfolioManager::getCurrentCoin();
        if (coin) {
            rgb.setCoin(coin->symbol);
        }
        renderCurrentPortfolioCard();
        Serial.println("[VAULT] Vault Locked. Returned to Crypto Hub.");
    } else if (ev == BTN_VERY_LONG_PRESS) {
        // Very long press launches the Air-Gap MicroSD PSBT Signer
        deviceState = STATE_AIRGAP_SD_SIGN;
        scanAndRenderAirGapPsbt();
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── State: Seed Words Viewer (Word-by-Word BIP-39 Viewer) ──────────────────
void parseWalletSeedWords() {
    const char* phrase = wallet ? wallet->getMnemonicPhrase() : nullptr;
    if (!phrase || strlen(phrase) == 0) {
        totalSeedWords = 12;
        for (int i = 0; i < 12; i++) {
            snprintf(verifiedSeedWords[i], sizeof(verifiedSeedWords[i]), "word%d", i + 1);
        }
        return;
    }
    char temp[240];
    strncpy(temp, phrase, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char* token = strtok(temp, " ");
    int count = 0;
    while (token && count < 24) {
        strncpy(verifiedSeedWords[count], token, sizeof(verifiedSeedWords[count]) - 1);
        verifiedSeedWords[count][sizeof(verifiedSeedWords[count]) - 1] = '\0';
        token = strtok(nullptr, " ");
        count++;
    }
    totalSeedWords = (count > 0) ? count : 12;
}

void processSeedWordsViewState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        // Next Word
        currentSeedViewIdx = (currentSeedViewIdx + 1) % totalSeedWords;
        rgb.flashTap(0, 229, 255, 50);
        ui.renderSeedWordsView(currentSeedViewIdx + 1, totalSeedWords, verifiedSeedWords[currentSeedViewIdx]);
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Previous Word
        currentSeedViewIdx = (currentSeedViewIdx - 1 + totalSeedWords) % totalSeedWords;
        rgb.flashDoubleTap(255, 140, 0);
        ui.renderSeedWordsView(currentSeedViewIdx + 1, totalSeedWords, verifiedSeedWords[currentSeedViewIdx]);
    } else if (ev == BTN_LONG_PRESS || ev == BTN_VERY_LONG_PRESS) {
        // Exit back to Vault Dashboard
        deviceState = STATE_VAULT_DASHBOARD;
        showVaultCoinScreen(currentViewCoin);
        Serial.println("[VAULT] Exited Seed Words View -> Vault Dashboard");
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── State: Seed Generation (Entropy Gathering & Word Verification) ──────────
void startSeedGeneration(int wordCount = 12) {
    totalMnemonicWords = (wordCount == 24) ? 24 : 12;
    SeedGenerator::resetEntropy();
    deviceState = STATE_SEED_ENTROPY_COLLECT;
    rgb.setMode(LED_MODE_SOLID_AMBER);
    ui.renderEntropyGatherScreen(0, 12);
    Serial.println("[SEED] Tap physical button 12 times to inject human entropy...");
}

void processSeedEntropyState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS || ev == BTN_LONG_PRESS) {
        uint32_t jitter = (uint32_t)(btn.currentHoldDuration() * 1000 + micros());
        SeedGenerator::recordButtonPressJitter(jitter, 50000);
        int samples = SeedGenerator::getEntropySampleCount();
        ui.renderEntropyGatherScreen(samples, 12);
        rgb.setEntropyJitter(jitter);

        if (samples >= 12) {
            // Generate verified mnemonic
            rgb.flashRainbow(1200);
            bool ok = (totalMnemonicWords == 24) 
                ? SeedGenerator::generateMnemonic24Words(generatedMnemonic, sizeof(generatedMnemonic))
                : SeedGenerator::generateMnemonic12Words(generatedMnemonic, sizeof(generatedMnemonic));

            if (ok) {
                // Parse words
                char temp[240];
                strncpy(temp, generatedMnemonic, sizeof(temp));
                char* token = strtok(temp, " ");
                int i = 0;
                while (token && i < totalMnemonicWords) {
                    strncpy(mnemonicWords[i], token, sizeof(mnemonicWords[i]) - 1);
                    token = strtok(nullptr, " ");
                    i++;
                }

                currentWordIdx = 0;
                deviceState = STATE_SEED_WORD_DISPLAY;
                rgb.setMode(LED_MODE_HOME);
                ui.renderSeedBackupScreen(1, totalMnemonicWords, mnemonicWords[0]);
                Serial.printf("[SEED] Successfully generated %d-word BIP-39 mnemonic!\n", totalMnemonicWords);
            }
        }
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Cancel back to idle
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_HOME);
        ui.renderReadyDashboard(millis() / 1000, true, wallet->isUnlocked());
    }
}

void processSeedWordDisplayState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        // Next Word
        currentWordIdx = (currentWordIdx + 1) % totalMnemonicWords;
        rgb.flashTap(0, 229, 255, 50);
        ui.renderSeedBackupScreen(currentWordIdx + 1, totalMnemonicWords, mnemonicWords[currentWordIdx]);
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Previous Word
        currentWordIdx = (currentWordIdx - 1 + totalMnemonicWords) % totalMnemonicWords;
        rgb.flashDoubleTap(255, 140, 0);
        ui.renderSeedBackupScreen(currentWordIdx + 1, totalMnemonicWords, mnemonicWords[currentWordIdx]);
    } else if (ev == BTN_LONG_PRESS) {
        // Complete seed verification
        if (wallet) {
            wallet->setMnemonic(generatedMnemonic);
            Serial.println("[WALLET] ✅ New verified BIP-39 mnemonic installed into active crypto wallet.");
        }
        rgb.flashRainbow(1000);
        ui.renderSuccessBanner("SEED BACKUP COMPLETE", "WALLET SECURED");
        delay(1200);

        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_HOME);
        ui.renderReadyDashboard(millis() / 1000, true, true);
    }
}

// ─── State: Air-Gap MicroSD PSBT Signer (BIP-174 Cold Wallet) ────────────────
void scanAndRenderAirGapPsbt() {
    rgb.setMode(LED_MODE_SOLID_BLUE);
    if (!PsbtSigner::initSD()) {
        ui.renderAirGapScreen("MICROSD ERROR", "NO TF CARD DETECTED", false);
        psbtLoaded = false;
        return;
    }

    if (PsbtSigner::findPendingPsbt(psbtFilePath, sizeof(psbtFilePath))) {
        psbtLoaded = PsbtSigner::parsePsbtFile(psbtFilePath, currentPsbt, *wallet);
        if (psbtLoaded && currentPsbt.isValid) {
            char amtStr[32], feeStr[32];
            PsbtSigner::formatSatoshis(currentPsbt.sendSatoshis, amtStr, sizeof(amtStr));
            snprintf(feeStr, sizeof(feeStr), "FEE: %llu sat", currentPsbt.feeSatoshis);
            ui.renderAirGapPsbt(currentPsbt.fileName, currentPsbt.recipientAddr, amtStr, feeStr, true);
            rgb.setMode(LED_MODE_PULSE_GREEN); // Pulse green: User presence required to sign!
            Serial.printf("[PSBT] Loaded %s: %s -> %s\n", currentPsbt.fileName, amtStr, currentPsbt.recipientAddr);
        } else {
            ui.renderAirGapScreen(psbtFilePath, "INVALID PSBT FORMAT", false);
            rgb.flashDoubleTap(255, 140, 0);
        }
    } else {
        ui.renderAirGapScreen("NO PENDING .PSBT", "SD CARD READY", false);
        psbtLoaded = false;
    }
}

void processAirGapSdSignState(ButtonEvent ev) {
    if (btn.isPressedNow() && psbtLoaded && currentPsbt.isValid) {
        uint8_t stage = btn.getHoldStage() * 33;
        if (stage != lastHoldStage) {
            lastHoldStage = stage;
            rgb.setHoldProgress((float)stage / 100.0f);
        }
    } else if (lastHoldStage > 0) {
        lastHoldStage = 0;
        if (psbtLoaded && currentPsbt.isValid) rgb.setMode(LED_MODE_PULSE_GREEN);
    }

    if (ev == BTN_SHORT_PRESS) {
        // Rescan SD card
        rgb.flashTap(0, 150, 255, 60);
        scanAndRenderAirGapPsbt();
    } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_VERY_LONG_PRESS) {
        // Exit back to Vault Dashboard
        deviceState = STATE_VAULT_DASHBOARD;
        showVaultCoinScreen(COIN_BTC);
    } else if (ev == BTN_LONG_PRESS) {
        if (psbtLoaded && currentPsbt.isValid) {
            char signedPath[64] = "";
            bool ok = PsbtSigner::signPsbtFile(psbtFilePath, *wallet, signedPath, sizeof(signedPath));
            if (ok) {
                rgb.flashRainbow(1200);
                ui.renderSuccessBanner("PSBT SIGNED OK", signedPath);
                Serial.printf("[PSBT] ✅ Signed and saved to: %s\n", signedPath);
                delay(1500);
                deviceState = STATE_VAULT_DASHBOARD;
                showVaultCoinScreen(COIN_BTC);
            } else {
                rgb.flashDoubleTap(255, 0, 0);
                ui.renderErrorBanner("SIGNING FAILED");
                delay(1200);
                scanAndRenderAirGapPsbt();
            }
        }
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── FIDO2 / WebAuthn User Presence Prompt Callback ─────────────────────────
bool handleUserPresencePrompt(uint32_t cid, const char* rpId, bool isRegistration) {
    PowerManager::wakeDisplay();
    if (rpId && strlen(rpId) > 0) {
        strncpy(reqDomain, rpId, sizeof(reqDomain) - 1);
    }

    DeviceState prev = deviceState;
    deviceState = STATE_PASSKEY_HUB;
    rgb.setMode(LED_MODE_PULSE_GREEN);
    ui.renderPasskeyHub(true, reqDomain, 1.0f);
    Serial.printf("[FIDO2] Prompting User Presence for '%s' (Registration: %s, CID: 0x%08X)\n",
                  reqDomain, isRegistration ? "YES" : "NO", cid);

    uint32_t start = millis();
    uint32_t lastKeepAlive = 0;
    uint8_t keepAliveFailures = 0;
    bool confirmed = false;
    bool done = false;

    while (millis() - start < 30000 && !done) {
        ButtonEvent ev = btn.update();
        rgb.update();
        ctapHid.process();
        handleSerialCommands();

        // Send FIDO2 CTAPHID Keepalive (UP Needed) every 250ms to keep host browser active.
        // If the host stops reading (the requesting program was killed, e.g. sudo timed
        // out), keepalives can't be delivered: abandon the prompt instead of showing it
        // for the full 30 s and colliding with the next request.
        if (millis() - lastKeepAlive >= 250) {
            if (ctapHid.sendKeepAlive(cid, CTAPHID_STATUS_UPNEEDED)) {
                keepAliveFailures = 0;
            } else if (++keepAliveFailures >= 3) {
                Serial.println("[FIDO2] Host stopped listening: prompt abandoned");
                ui.renderErrorBanner("Request Ended");
                delay(400);
                done = true;
            }
            lastKeepAlive = millis();
        }

        float remaining = 1.0f - ((float)(millis() - start) / 30000.0f);
        ui.renderPasskeyHub(true, reqDomain, remaining);

        if (ctapHid.isCancelRequested()) {  // browser cancelled / timed out the request
            confirmed = false;
            done = true;
            ui.renderErrorBanner("Request Cancelled");
            delay(600);
        } else if (ev == BTN_SHORT_PRESS || ev == BTN_LONG_PRESS || s_simulatedTouch) {
            confirmed = true;
            done = true;
            s_simulatedTouch = false;
            rgb.flashRainbow(900);
            ui.renderSuccessBanner(isRegistration ? "PASSKEY REGISTERED" : "ASSERTION SIGNED", reqDomain);
            delay(1000);
        } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_VERY_LONG_PRESS) {
            confirmed = false;
            done = true;
            rgb.flashDoubleTap(255, 0, 0);
            ui.renderErrorBanner("Auth Cancelled");
            delay(1000);
        } else if (ev == BTN_PANIC_HOLD) {
            DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
            return false;
        }

        delay(15);
    }

    deviceState = prev;
    if (deviceState == STATE_IDLE_READY) {
        rgb.setMode(LED_MODE_HOME);
        const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
        ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
    } else if (deviceState == STATE_PASSKEY_HUB) {
        rgb.setMode(LED_MODE_BREATHE_GREEN);
        ui.renderPasskeyHub(false, nullptr, 1.0f);
    } else if (deviceState == STATE_PORTFOLIO_TRACKER) {
        renderCurrentPortfolioCard();
    }

    return confirmed;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
void resetPinEntry() {
    pinIndex = 0;
    currentDigitVal = 0;
    masterPinLen = PinVault::length();
    if (masterPinLen < PIN_MIN_LENGTH) masterPinLen = PIN_MIN_LENGTH;
    if (masterPinLen > PIN_MAX_LENGTH) masterPinLen = PIN_MAX_LENGTH;
    for (int i = 0; i < masterPinLen; i++) pinDigits[i] = '0';
    pinDigits[masterPinLen] = '\0';
}

void loadSecurityConfig() {
    vaultPrefs.begin("vault_sec", false);
    dispRotation = vaultPrefs.getUChar("disp_rot", DISP_ROTATION);
    if (!vaultPrefs.isKey("provisioned")) {
        vaultPrefs.putBool("provisioned", false);
    }
    vaultPrefs.end();

    PinVault::begin();  // migrates any legacy plaintext PIN to a salted hash
    masterPinLen = PinVault::length();
    if (masterPinLen < PIN_MIN_LENGTH) masterPinLen = PIN_MIN_LENGTH;
    if (masterPinLen > PIN_MAX_LENGTH) masterPinLen = PIN_MAX_LENGTH;
}

void handleSerialCommands() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    // Check PortfolioManager command dispatcher first
    String res;
    if (PortfolioManager::processCommand(cmd, res)) {
        Serial.println(res);
        if (cmd.startsWith("enable ") || cmd.startsWith("disable ")) {
            CryptoCoinRegistry::savePreferences();
            if (wallet) wallet->refreshFamilies();
        }
        lastTickerRefreshMs = millis();
        renderCurrentPortfolioCard();
        return;
    }

    if (cmd.equalsIgnoreCase("help")) {
        Serial.println("\n--- Crypto TKey S3 CLI ---");
        Serial.println("  status                   - Print security key status & uptime");
        Serial.println("  flip                     - Flip display 180 degrees (landscape)");
        Serial.println("  coins                    - List the coin catalog & holdings");
        Serial.println("  enable <SYM>             - Enable coin in active portfolio tracker");
        Serial.println("  disable <SYM>            - Disable coin from portfolio tracker");
        Serial.println("  setbal <SYM> <AMT>       - Set user coin holding balance");
        Serial.println("  setprice <SYM> <P> [C]   - Update live USD price & 24h change");
        Serial.println("  json                     - Output compact JSON for WebUSB companion");
        Serial.println("  newseed [12|24] [quick]  - Generate genuine BIP-39 mnemonic (screen or TRNG)");
        Serial.println("  seed [--allow-serial]    - View recovery mnemonic (LCD screen or test export)");
        Serial.println("  setpin <PIN>             - Set 4-digit master PIN");
        Serial.println("  unlock <PIN>             - Unlock crypto vault and reveal derived addresses");
        Serial.println("  addresses                - Print genuine derived BIP-32/BIP-84/EIP-55 addresses");
        Serial.println("  lock                     - Lock crypto vault immediately");
        Serial.println("  led <btc|eth|sol|rainbow>- Test RGB DotStar LED color mode");
        Serial.println("  psbt [scan|parse|sign]   - Air-Gapped MicroSD BIP-174 Bitcoin signer");
        Serial.println("  vault [status|backup|restore|wipe] - Hardware-Bound Encrypted MicroSD Vault (AES-256-GCM)");
        Serial.println("  panic                    - Trigger emergency flash nuke");
        Serial.println("  decode_evm <hex>         - Clear-sign & inspect EVM transaction (PEPE/EIP-1559)");
        Serial.println("  sign_evm <hex>           - Clear-sign & display prompt on device screen");
    } else if (cmd.equalsIgnoreCase("touch") || cmd.equalsIgnoreCase("press")) {
#ifdef TKEY_TEST_SERIAL_TOUCH
        // Test builds only: lets the automated suite approve prompts over CDC.
        s_simulatedTouch = true;
        Serial.println("[BTN] 👆 Simulated User Presence button touch received.");
#else
        // Production: user presence must be the physical button. Any local process can
        // open the CDC port, so a serial "touch" would let malware approve logins.
        Serial.println("[BTN] Serial touch disabled in production firmware (press the button).");
#endif
    } else if (cmd.equalsIgnoreCase("status")) {
        Serial.printf("Uptime: %lus | CPU: %dMHz | Vault: %s | Master PIN Len: %d | Active Coins: %d | Rotation: %d\n",
            millis() / 1000, getCpuFrequencyMhz(), wallet->isUnlocked() ? "UNLOCKED" : "LOCKED",
            masterPinLen, PortfolioManager::getActiveCount(), dispRotation);
    } else if (cmd.equalsIgnoreCase("flip")) {
        dispRotation = (dispRotation == 1) ? 3 : 1;
        tft->setRotation(dispRotation);
        ui.setRotation(dispRotation);
        vaultPrefs.begin("vault_sec", false);
        vaultPrefs.putUChar("disp_rot", dispRotation);
        vaultPrefs.end();
        const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
        ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        Serial.printf("[SCREEN] Flipped to rotation %d\n", dispRotation);
    } else if (cmd.startsWith("unlock ")) {
#ifdef TKEY_TEST_SERIAL_TOUCH
        String pin = cmd.substring(7);
        pin.trim();
        if (PinVault::check(pin.c_str()) == PinVault::OK && wallet->unlock()) {
            deviceState = STATE_VAULT_DASHBOARD;
            showVaultCoinScreen(COIN_BTC);
            Serial.println("[VAULT] ✅ Unlocked (test build).");
        } else {
            Serial.println("[VAULT] ❌ Error: Invalid PIN or no wallet seed.");
        }
#else
        // Any local process can open the CDC port: the PIN is only entered on the device.
        Serial.println("[VAULT] Serial unlock is disabled; enter the PIN on the device.");
#endif
    } else if (cmd.equalsIgnoreCase("diag")) {
        nvs_stats_t nvs = {};
        nvs_get_stats(NULL, &nvs);
        Serial.printf("DIAG state=%d uptime=%lus heap=%u minheap=%u reset=%d wifimode=%d usbhost=%d "
                      "loopstack=%u led=%u,%u,%u@%u mode=%d sleeping=%d nvsused=%u nvsfree=%u\n",
                      (int)deviceState, millis() / 1000, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                      (int)esp_reset_reason(), (int)WiFi.getMode(), (int)(bool)USB,
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr), rgb.lastR, rgb.lastG, rgb.lastB,
                      rgb.lastBrightness, (int)rgb.currentMode(), (int)PowerManager::isDisplaySleeping(),
                      (unsigned)nvs.used_entries, (unsigned)nvs.free_entries);
#ifdef TKEY_TEST_SERIAL_TOUCH
    } else if (cmd.startsWith("btn ")) {
        String b = cmd.substring(4);
        b.trim();
        s_injectedEvent = b == "short" ? BTN_SHORT_PRESS : b == "double" ? BTN_DOUBLE_CLICK :
                          b == "long" ? BTN_LONG_PRESS : b == "vlong" ? BTN_VERY_LONG_PRESS : BTN_NONE;
        Serial.printf("[TEST] injected button: %s\n", b.c_str());
    } else if (cmd.equalsIgnoreCase("shot")) {
        // Raw RGB565 framebuffer of the UI sprite, base64, for a laptop-side screenshot
        TFT_eSprite* sp = ui.sprite();
        if (!sp) {
            Serial.println("SHOT NONE");
        } else {
            const uint8_t* px = (const uint8_t*)sp->getPointer();
            const size_t n = DISP_W * DISP_H * 2;
            static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            Serial.printf("SHOT %d %d\n", DISP_W, DISP_H);
            char out[81];
            size_t o = 0;
            for (size_t i = 0; i < n; i += 3) {
                uint32_t v = (uint32_t)px[i] << 16 | (i + 1 < n ? (uint32_t)px[i + 1] << 8 : 0) | (i + 2 < n ? px[i + 2] : 0);
                out[o++] = B64[(v >> 18) & 63];
                out[o++] = B64[(v >> 12) & 63];
                out[o++] = i + 1 < n ? B64[(v >> 6) & 63] : '=';
                out[o++] = i + 2 < n ? B64[v & 63] : '=';
                if (o >= 76) { out[o] = '\0'; Serial.println(out); o = 0; }
            }
            if (o) { out[o] = '\0'; Serial.println(out); }
            Serial.println("SHOT END");
        }
    } else if (cmd.equalsIgnoreCase("portal")) {
        launchSetupPortal();   // test builds: same as holding the button while plugging in
        Serial.println("[TEST] setup portal launched");
    } else if (cmd.equalsIgnoreCase("nvsdump")) {
        // Entries per namespace (sizes only, never values)
        nvs_iterator_t it = nullptr;
        char names[24][16];
        int counts[24] = {0}, kinds = 0;
        esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);
        while (e == ESP_OK) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            int k = 0;
            while (k < kinds && strcmp(names[k], info.namespace_name) != 0) k++;
            if (k == kinds && kinds < 24) { strncpy(names[kinds], info.namespace_name, 15); names[kinds][15] = 0; kinds++; }
            if (k < 24) counts[k]++;
            e = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        for (int k = 0; k < kinds; k++) Serial.printf("NVS %s keys=%d\n", names[k], counts[k]);
        Serial.println("NVS END");
    } else if (cmd.startsWith("famtest ")) {
        // Derive every wallet family for a given (test) mnemonic; compared with bip_utils vectors
        String m = cmd.substring(8);
        m.trim();
        uint8_t seed[64];
        if (!Bip32Engine::mnemonicToSeed(m.c_str(), "", seed)) {
            Serial.println("FAM ERROR seed");
        } else {
            char addr[FAMILY_ADDR_LEN];
            for (int f = 0; f < FAM_COUNT; f++) {
                uint32_t t0 = millis();
                bool ok = WalletFamilies::deriveAddress((WalletFamily)f, seed, addr);
                Serial.printf("FAM %d %s %lums\n", f, ok ? addr : "FAIL", millis() - t0);
            }
            mbedtls_platform_zeroize(seed, sizeof(seed));
        }
        Serial.println("FAM END");
#endif
    } else if (cmd.equalsIgnoreCase("addrs")) {
        // Machine-readable public receive addresses for the host companion (balances).
        // Public data only, cached at seed creation, so this works while locked.
        // One line per selected coin: ADDR <SYM> <address> <family> <network>|<contract>
        for (int i = 0; i < CryptoCoinRegistry::getCoinCount(); i++) {
            const CoinAsset* c = CryptoCoinRegistry::getCoinByIndex(i);
            if (c->enabled && c->address[0]) {
                Serial.printf("ADDR %s %s %s %s|%s\n", c->symbol, c->address, WalletFamilies::name(c->meta->family),
                              c->meta->network, c->meta->contract ? c->meta->contract : "");
            }
        }
        Serial.println("ADDR END");
    } else if (cmd.equalsIgnoreCase("addresses")) {
        if (!wallet->hasSeed()) {
            Serial.println("[VAULT] No wallet seed yet.");
        } else {
            Serial.println("\n--- Genuine Derived Addresses (BIP-32 / BIP-84 / EIP-55) ---");
            for (int i = 0; i < COIN_COUNT; i++) {
                const WalletAccount* acc = wallet->getAccount((CryptoCoin)i);
                if (acc) {
                    Serial.printf("  %-10s [%s] (%s):\n    %s\n", acc->name, acc->symbol, acc->derivationPath, acc->address);
                }
            }
            Serial.println("------------------------------------------------------------\n");
        }
    } else if (cmd.equalsIgnoreCase("seed") || cmd.startsWith("seed ") || cmd.startsWith("showseed")) {
        if (!wallet || !wallet->isUnlocked()) {
            Serial.println("[VAULT] 🔒 Error: Vault locked. Unlock with 'unlock <PIN>' first.");
#ifdef TKEY_TEST_SERIAL_TOUCH
        } else if (cmd.indexOf("--allow-serial") != -1 || cmd.indexOf("--insecure-serial-dump") != -1) {
            Serial.printf("[VAULT] ⚠️ INSECURE SERIAL EXPORT (test build): %s\n", wallet->getMnemonicPhrase());
#endif
        } else {
            Serial.println("[VAULT] 🛡️ Zero-Seed-Leakage Air-Gap Policy Active.");
            Serial.println("  Seed words are displayed EXCLUSIVELY on physical 160x80 LCD screen.");
            Serial.println("  To view on screen: Enter Vault (PIN) -> Double Click button.");
        }
    } else if (cmd.equalsIgnoreCase("lock")) {
        wallet->lock();
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_HOME);
        ui.renderReadyDashboard(millis() / 1000, true, false);
        Serial.println("[VAULT] 🔒 Vault locked and volatile key material zeroized.");
    } else if (cmd.startsWith("led ")) {
        String mode = cmd.substring(4);
        mode.trim();
        if (mode.equalsIgnoreCase("rainbow")) {
            rgb.flashRainbow(2000);
            Serial.println("[LED] 🌈 Rainbow shimmer activated");
        } else if (mode.equalsIgnoreCase("btc")) {
            rgb.setCoin("BTC");
            Serial.println("[LED] 🟠 Bitcoin Gold/Orange activated");
        } else if (mode.equalsIgnoreCase("eth")) {
            rgb.setCoin("ETH");
            Serial.println("[LED] 🟣 Ethereum Royal Violet activated");
        } else if (mode.equalsIgnoreCase("sol")) {
            rgb.setCoin("SOL");
            Serial.println("[LED] 🟢 Solana Neon Turquoise activated");
        } else if (mode.equalsIgnoreCase("doge")) {
            rgb.setCoin("DOGE");
            Serial.println("[LED] 🟡 Dogecoin Sunny Gold activated");
        } else if (mode.equalsIgnoreCase("softap")) {
            rgb.setMode(LED_MODE_SOFTAP_PULSE);
            Serial.println("[LED] 🟪 SoftAP Portal Neon Pulse activated");
        }
    } else if (cmd.startsWith("psbt")) {
        String sub = cmd.length() > 5 ? cmd.substring(5) : "scan";
        sub.trim();
        if (sub.length() == 0 || sub.equalsIgnoreCase("scan")) {
            char p[64];
            if (PsbtSigner::findPendingPsbt(p, sizeof(p))) {
                Serial.printf("[PSBT] 📂 Found pending unsigned file: %s\n", p);
            } else {
                Serial.println("[PSBT] ℹ️ No pending unsigned .psbt files found on MicroSD.");
            }
        } else if (sub.equalsIgnoreCase("parse")) {
            char p[64];
            if (PsbtSigner::findPendingPsbt(p, sizeof(p))) {
                PsbtTxDetails details;
                if (PsbtSigner::parsePsbtFile(p, details, *wallet)) {
                    char amtStr[32], feeStr[32];
                    PsbtSigner::formatSatoshis(details.sendSatoshis, amtStr, sizeof(amtStr));
                    PsbtSigner::formatSatoshis(details.feeSatoshis, feeStr, sizeof(feeStr));
                    Serial.println("\n--- PSBT Transaction Details (BIP-174 WYSIWYS) ---");
                    Serial.printf("  File:      %s\n", details.fileName);
                    Serial.printf("  To:        %s\n", details.recipientAddr);
                    Serial.printf("  Amount:    %s (%llu sats)\n", amtStr, details.sendSatoshis);
                    Serial.printf("  Miner Fee: %s (%llu sats)\n", feeStr, details.feeSatoshis);
                    Serial.printf("  Inputs:    %u | Outputs: %u\n", details.numInputs, details.numOutputs);
                    Serial.printf("  Status:    %s\n", details.isSigned ? "ALREADY SIGNED" : "READY TO SIGN");
                    Serial.println("--------------------------------------------------\n");
                } else {
                    Serial.println("[PSBT] ❌ Error: Could not parse PSBT structure.");
                }
            } else {
                Serial.println("[PSBT] ℹ️ Error: No pending .psbt file found.");
            }
        } else if (sub.equalsIgnoreCase("sign")) {
#ifndef TKEY_TEST_SERIAL_TOUCH
            // Signing needs the on-device review screen (vault -> very long press).
            Serial.println("[PSBT] Serial signing is disabled; sign on the device (vault -> very long press).");
            if (true) {
            } else if (!wallet->isUnlocked()) {
#else
            if (!wallet->isUnlocked()) {
#endif
                Serial.println("[PSBT] 🔒 Error: Vault must be UNLOCKED first. Run 'unlock <PIN>'.");
            } else {
                char p[64];
                if (PsbtSigner::findPendingPsbt(p, sizeof(p))) {
                    char signedPath[64];
                    if (PsbtSigner::signPsbtFile(p, *wallet, signedPath, sizeof(signedPath))) {
                        rgb.flashRainbow(1200);
                        Serial.printf("[PSBT] ✅ Successfully signed! Exported to: %s\n", signedPath);
                    } else {
                        Serial.println("[PSBT] ❌ Signing failed.");
                    }
                } else {
                    Serial.println("[PSBT] ℹ️ Error: No pending .psbt file found to sign.");
                }
            }
        }
    } else if (cmd.startsWith("vault")) {
        String sub = cmd.substring(5);
        sub.trim();
        if (sub.length() == 0 || sub.equalsIgnoreCase("status")) {
            if (sdVault.begin()) {
                Serial.printf("[SD VAULT] ✅ Card Mounted: %llu MB | Used: %llu KB\n",
                    sdVault.getCardSizeMB(), sdVault.getUsedBytes() / 1024);
                Serial.printf("  Seed Backup: %s\n", sdVault.hasSeedBackup() ? "PRESENT (/vault/tkey_backup.vault)" : "NONE");
                Serial.println("  Encryption:  AES-256-GCM Hardware-Bound (ESP32-S3 MAC + AAGUID + PIN)");
            } else {
                Serial.println("[SD VAULT] ❌ No MicroSD card detected or mount failed. Check slot (Pins: CLK=12, CMD=16, D0=17).");
            }
        } else if (sub.startsWith("backup")) {
            String pin = sub.substring(6);
            pin.trim();
            const char* mnemonic = wallet->getMnemonicPhrase();
            if (!wallet->isUnlocked()) {
                Serial.println("[SD VAULT] 🔒 Unlock the vault on the device first.");
            } else if (pin.length() < PIN_MIN_LENGTH) {
                Serial.println("[SD VAULT] Usage: vault backup <backup PIN> (4+ digits)");
            } else if (!mnemonic || strlen(mnemonic) == 0) {
                Serial.println("[SD VAULT] ❌ Error: No seed mnemonic active in wallet.");
            } else if (sdVault.backupSeed(mnemonic, pin.c_str())) {
                rgb.flashRainbow(800);
                Serial.println("[SD VAULT] 🔒✅ Backup SUCCESS: Active BIP-39 seed encrypted to /vault/tkey_backup.vault (AES-256-GCM).");
            } else {
                Serial.println("[SD VAULT] ❌ Error: Failed to write encrypted backup to SD card.");
            }
        } else if (sub.startsWith("restore")) {
            String pin = sub.substring(7);
            pin.trim();
            char restoredMnemonic[256] = {0};
            if (wallet->hasSeed() && !wallet->isUnlocked()) {
                Serial.println("[SD VAULT] 🔒 A wallet exists: unlock it on the device before replacing it.");
            } else if (sdVault.restoreSeed(restoredMnemonic, sizeof(restoredMnemonic), pin.c_str())) {
                bool ok = wallet->setMnemonic(restoredMnemonic);
                CryptoWallet::secureZero(restoredMnemonic, sizeof(restoredMnemonic));
                if (ok) {
                    rgb.flashRainbow(1200);
                    Serial.println("[SD VAULT] 🔓✅ Restore SUCCESS: seed verified (GCM + BIP-39 checksum) and stored.");
                } else {
                    Serial.println("[SD VAULT] ❌ Restored data is not a valid BIP-39 seed; wallet unchanged.");
                }
            } else {
                Serial.println("[SD VAULT] ❌ Error: Decryption or GCM integrity check failed! Wrong PIN, wrong hardware, or file tampered.");
            }
        } else if (sub.equalsIgnoreCase("wipe CONFIRM")) {
            if (sdVault.wipeVault()) {
                Serial.println("[SD VAULT] ⚠️ Vault containers securely overwritten and wiped from MicroSD.");
            } else {
                Serial.println("[SD VAULT] No vault files found to wipe.");
            }
        } else if (sub.equalsIgnoreCase("wipe")) {
            Serial.println("[SD VAULT] ⚠️ DANGER: To wipe all encrypted vaults from SD, type: 'vault wipe CONFIRM'");
        }
    } else if (cmd.startsWith("newseed")) {
        int words = 12;
        if (cmd.indexOf("24") != -1) words = 24;
#ifdef TKEY_TEST_SERIAL_TOUCH
        if (cmd.indexOf("quick") != -1 || cmd.indexOf("auto") != -1 || cmd.indexOf("trng") != -1) {
            char phrase[240] = {0};
            SeedGenerator::resetEntropy();
            bool ok = (words == 24)
                ? SeedGenerator::generateMnemonic24Words(phrase, sizeof(phrase))
                : SeedGenerator::generateMnemonic12Words(phrase, sizeof(phrase));
            if (ok && wallet && wallet->setMnemonic(phrase)) {
                Serial.printf("[SEED] ✅ (test build) %d-word mnemonic: %s\n", words, phrase);
            } else {
                Serial.println("[SEED] ❌ Failed to generate mnemonic.");
            }
            CryptoWallet::secureZero(phrase, sizeof(phrase));
            return;
        }
#endif
        if (wallet->hasSeed() && !wallet->isUnlocked()) {
            Serial.println("[SEED] 🔒 A wallet exists: unlock it on the device before replacing it.");
        } else {
            startSeedGeneration(words);  // words are shown only on the device screen
        }
#ifdef TKEY_TEST_SERIAL_TOUCH
    } else if (cmd.startsWith("wallet check ")) {
        Serial.printf("[TEST] mnemonic valid: %s\n", CryptoWallet::isValidMnemonic(cmd.substring(13).c_str()) ? "YES" : "NO");
    } else if (cmd.equalsIgnoreCase("wallet selftest")) {
        char cached[COIN_COUNT][64];
        for (int i = 0; i < COIN_COUNT; i++) strncpy(cached[i], wallet->getAddress((CryptoCoin)i), 63);
        uint32_t t0 = millis();
        bool ok = wallet->unlock();
        uint32_t dt = millis() - t0;
        bool match = ok;
        for (int i = 0; ok && i < COIN_COUNT; i++) match &= strcmp(cached[i], wallet->getAddress((CryptoCoin)i)) == 0;
        Serial.printf("[TEST] unlock=%d (%lu ms) cached==derived:%s ETH=%s\n", ok, dt, match ? "YES" : "NO", wallet->getAddress(COIN_ETH));
        wallet->lock();
        Serial.printf("[TEST] after lock: mnemonic cleared=%s\n", strlen(wallet->getMnemonicPhrase()) == 0 ? "YES" : "NO");
    } else if (cmd.equalsIgnoreCase("wallet wipe")) {
        wallet->lock();
        Preferences p;
        p.begin("wallet_seed", false);
        p.clear();
        p.end();
        Serial.println("[TEST] wallet_seed namespace erased; reboot for a clean state.");
#endif
#ifdef TKEY_TEST_SERIAL_TOUCH
    } else if (cmd.equalsIgnoreCase("wifitest")) {
        wifi->forceWallMode(true);
        Serial.println("[TEST] Wall-mode Wi-Fi burst forced while on USB");
#endif
    } else if (cmd.startsWith("setpin ")) {
        // PIN changes go through the password-protected setup portal, never an open serial port.
        Serial.println("Error: serial PIN changes are disabled; use the setup portal.");
    } else if (cmd.startsWith("decode_evm ") || cmd.startsWith("sign_evm ")) {
        bool isSignCmd = cmd.startsWith("sign_evm ");
        String hexTx = cmd.substring(cmd.indexOf(' ') + 1);
        hexTx.trim();
        EvmDecodedTx decoded;
        if (wallet && wallet->parseAndPrepareEvmHexTx(hexTx.c_str(), &decoded)) {
            Serial.println("[EVM] ✅ Clear-Sign Transaction Decoded Successfully:");
            Serial.printf("  Envelope: %s\n", (decoded.txType == EVM_TX_EIP1559) ? "EIP-1559 (Type 2 Dynamic Fee)" : "Legacy EIP-155 (Type 0)");
            Serial.printf("  Action:   %s\n", decoded.dispAction);
            Serial.printf("  Asset:    %s (%s)\n", decoded.tokenName, decoded.tokenSymbol);
            Serial.printf("  Amount:   %s\n", decoded.dispAmount);
            Serial.printf("  Target:   %s\n", decoded.recipientOrSpender[0] ? decoded.recipientOrSpender : decoded.toAddress);
            Serial.printf("  Fee Est:  %s\n", decoded.dispFee);

            if (decoded.isUnlimitedApproval) {
                Serial.println("  ⚠️ [ALERT] HIGH RISK: UNLIMITED TOKEN ALLOWANCE DRAINER DETECTED!");
            }

            ui.renderCryptoSignPrompt(decoded.tokenName, 
                                      decoded.recipientOrSpender[0] ? decoded.recipientOrSpender : decoded.toAddress, 
                                      decoded.dispAmount);
            rgb.setMode(LED_MODE_SOLID_AMBER);

            if (isSignCmd) {
#ifndef TKEY_TEST_SERIAL_TOUCH
                // Signing must be confirmed on the device, never triggered from an open serial port.
                Serial.println("[EVM] Serial signing is disabled; review and sign on the device.");
                if (false) {
#else
                if (wallet->isUnlocked()) {
#endif
                    char sigHex[130] = {0};
                    if (wallet->executeSign(sigHex, sizeof(sigHex))) {
                        Serial.println("[EVM] ✍️ Transaction Signed via secp256k1 (m/44'/60'/0'/0/0):");
                        Serial.printf("  Signature (r||s): %s\n", sigHex);
                    } else {
                        Serial.println("[EVM] ❌ Error executing signature.");
                    }
                } else {
                    Serial.println("[EVM] ⚠️ Vault is LOCKED. Unlock it on the device first.");
                }
            }
        } else {
            Serial.println("[EVM] ❌ Error: Failed to parse RLP transaction stream.");
        }
    } else if (cmd.equalsIgnoreCase("panic CONFIRM") || cmd.equalsIgnoreCase("panic NUKE")) {
#ifdef TKEY_TEST_SERIAL_TOUCH
        Serial.println("[PANIC] 🚨 CONFIRMATION RECEIVED. Executing cryptographic flash scrub...");
        DuressWipe::execute(*tft, rgb, "SERIAL_PANIC");
#else
        // A wipe destroys every FIDO key and the wallet: only the physical panic hold may do it.
        Serial.println("[PANIC] Serial wipe is disabled; hold the device button >6 s.");
#endif
    } else if (cmd.equalsIgnoreCase("panic")) {
        Serial.println("[PANIC] ⚠️ SAFEGUARD GUARD: Accidental execution blocked.");
        Serial.println("  Or hold the physical device button for >6 seconds.");
    }
}
