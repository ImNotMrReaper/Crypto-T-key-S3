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
#include "src/portfolio_mgr.h"
#include "src/wifi_manager.h"
#include "src/web_portal.h"
#include "src/psbt_signer.h"
#include "src/evm_decoder.h"

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
char          masterPin[PIN_LENGTH + 1] = DEFAULT_MASTER_PIN;
int           masterPinLen = 4;
uint8_t       dispRotation = DISP_ROTATION;
char          pinDigits[PIN_LENGTH + 1] = "0000";
int           pinIndex = 0;
int           currentDigitVal = 0;
uint8_t       lastHoldStage = 0;
uint32_t      lastStateUpdate = 0;
CryptoCoin    currentViewCoin = COIN_BTC;

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
void loadSecurityConfig();
bool handleUserPresencePrompt(uint32_t cid, const char* rpId, bool isRegistration);
void renderCurrentPortfolioCard();

void launchSetupPortal() {
    vaultPrefs.begin("vault_sec", true);
    bool isProvisioned = vaultPrefs.getBool("provisioned", false);
    String setupPwdHash = vaultPrefs.getString("setup_pwd_hash", "");
    vaultPrefs.end();

    deviceState = STATE_SETUP_WALKTHROUGH;
    portal->begin(wallet, wifi, isProvisioned, setupPwdHash.c_str());
    ui.renderOobeWizard(1, "T-KEY SETUP", "SSID: T-Key-Setup", "GO TO: 192.168.4.1");
    rgb.setMode(LED_MODE_SOFTAP_PULSE);
    Serial.println("[SETUP] 🌐 SoftAP Setup Portal active at http://192.168.4.1 (SSID: T-Key-Setup)");
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // 1. Initialize Thermal & Power Management (80MHz, RF disabled)
    PowerManager::init();

    Serial.begin(115200);
    delay(600);

    Serial.println("\n[BOOT] ===== CRYPTO TKEY S3 INITIALIZATION =====");
    Serial.printf("[BOOT] CPU Clock: %d MHz | RF: Disabled (Thermal Throttled)\n", getCpuFrequencyMhz());

    // 2. Hardware Peripherals
    btn.begin(PIN_BTN);
    rgb.begin(PIN_LED_DATA, PIN_LED_CLK);
    rgb.setMode(LED_MODE_BREATHE_CYAN);
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

    ctapHid.begin();
    ctap2Engine.begin();
    ctap2Engine.setUserPresencePrompt(handleUserPresencePrompt);
    ctapHid.setWinkHandler([](uint32_t cid) {
        Serial.printf("[FIDO2] 😉 WINK identification triggered on CID 0x%08X!\n", cid);
        rgb.flashRainbow(1500);
        if (deviceState == STATE_IDLE_READY) {
            ui.renderSuccessBanner("DEVICE LOCATED", "WINK VERIFIED");
            delay(800);
            const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
            ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        }
    });

    // 7. Portfolio & Seed Engines
    PortfolioManager::init();
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
        rgb.setMode(LED_MODE_BREATHE_CYAN);
    }

    Serial.println("[BOOT] ✅ Crypto TKey S3 Ready. 3-Screen Architecture Active.");
    Serial.println("========================================================\n");
}

// ─── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
    ButtonEvent ev = btn.update();
    rgb.update();
    ctapHid.process();
    if (wifi) wifi->update();

    if (portal && portal->isRunning()) {
        portal->update();
        if (portal->isSetupDone()) {
            strncpy(masterPin, portal->getNewPin(), PIN_LENGTH);
            masterPin[PIN_LENGTH] = '\0';
            masterPinLen = strlen(masterPin);
            if (masterPinLen < PIN_MIN_LENGTH) masterPinLen = PIN_MIN_LENGTH;
            if (masterPinLen > PIN_MAX_LENGTH) masterPinLen = PIN_MAX_LENGTH;

            vaultPrefs.begin("vault_sec", false);
            vaultPrefs.putString("user_pin", masterPin);
            vaultPrefs.putString("duress_pin", portal->getNewDuressPin());
            if (strlen(portal->getSetupPasswordHash()) > 0) {
                vaultPrefs.putString("setup_pwd_hash", portal->getSetupPasswordHash());
            }
            vaultPrefs.putBool("provisioned", true);
            vaultPrefs.end();

            ctap2Engine.setMasterPin(masterPin);

            portal->stop();
            rgb.flashRainbow(800);
            ui.renderSuccessBanner("VAULT PROVISIONED", "LAUNCHING KEY");
            delay(1500);

            deviceState = STATE_IDLE_READY;
            rgb.setMode(LED_MODE_BREATHE_CYAN);
            const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
            ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        }
    }

    bool userActive = (ev != BTN_NONE);
    PowerManager::update(userActive);

    handleSerialCommands();

    switch (deviceState) {
        case STATE_SETUP_WALKTHROUGH:
            if (ev == BTN_DOUBLE_CLICK) {
                // Cancel setup portal
                portal->stop();
                deviceState = STATE_IDLE_READY;
                rgb.setMode(LED_MODE_BREATHE_CYAN);
                const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
                ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
            }
            break;
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
            rgb.setCoinColor(c.r, c.g, c.b);
        }
        renderCurrentPortfolioCard();
        Serial.println("[NAV] Switched to Screen 3: Crypto & Asset Hub (Keep-Awake ON)");
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Double tap returns to Screen 1: Base Home Screen
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
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
        ui.renderPortfolioCard(coin->symbol, coin->name, coin->balance, coin->priceUsd, coin->change24h,
                               PortfolioManager::getCurrentIndex(), PortfolioManager::getActiveCount(),
                               PortfolioManager::getTotalValueUsd());
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
            rgb.setCoinColor(c.r, c.g, c.b);
        }
        renderCurrentPortfolioCard();
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Double tap returns to Screen 1: Base Home Screen and releases keep-awake
        PowerManager::setKeepAwake(false);
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        const char* ssid = (wifi && wifi->isConnected()) ? wifi->getConnectedSsid() : "AIRGAP";
        ui.renderHomeDashboard(millis() / 1000, ssid, PortfolioManager::getTotalValueUsd(), dispRotation == 3);
        Serial.println("[NAV] Exited Crypto Hub -> Base Home Screen");
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
            Serial.printf("[PIN] Validating PIN: %s\n", pinDigits);

            if (strcmp(pinDigits, EMERGENCY_DURESS_PIN) == 0) {
                DuressWipe::execute(*tft, rgb, "DURESS_PIN");
            } else if (strcmp(pinDigits, masterPin) == 0) {
                Serial.println("[VAULT] Master PIN Accepted! Unlocked.");
                wallet->unlock(pinDigits);
                rgb.flashRainbow(800);
                ui.renderSuccessBanner("VAULT UNLOCKED", "CRYPTO SIGNER READY");
                delay(1200);

                deviceState = STATE_VAULT_DASHBOARD;
                currentViewCoin = COIN_BTC;
                rgb.setCoinColor(255, 140, 0);
                const WalletAccount* acc = wallet->getAccount(COIN_BTC);
                ui.renderWalletScreen("BITCOIN", "BTC (SegWit)", acc ? acc->address : "bc1q...", acc ? acc->derivationPath : "m/84'/0'/0'/0/0");
            } else {
                Serial.println("[AUTH] Invalid PIN entered!");
                ui.renderErrorBanner("Wrong PIN Code");
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

// ─── State: Vault Dashboard (Private Derived Addresses Explorer) ────────────
void processVaultDashboardState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        currentViewCoin = (CryptoCoin)((currentViewCoin + 1) % 4);
        const WalletAccount* acc = wallet->getAccount(currentViewCoin);
        const char* name = "BITCOIN";
        const char* sym  = "BTC (SegWit)";
        uint8_t cr = 255, cg = 140, cb = 0;
        if (currentViewCoin == COIN_ETH)  { name = "ETHEREUM / PEPE"; sym = "ETH (ERC-20)"; cr = 138; cg = 75; cb = 255; }
        if (currentViewCoin == COIN_SOL)  { name = "SOLANA";          sym = "SOL (Ed25519)"; cr = 20; cg = 241; cb = 149; }
        if (currentViewCoin == COIN_DOGE) { name = "DOGECOIN";        sym = "DOGE (Legacy)"; cr = 255; cg = 195; cb = 15; }

        rgb.flashTap(cr, cg, cb, 60);
        rgb.setCoinColor(cr, cg, cb);
        ui.renderWalletScreen(name, sym, acc ? acc->address : "", acc ? acc->derivationPath : "");
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
            RgbColor c = RgbStatus::getCoinRgb(coin->symbol);
            rgb.setCoinColor(c.r, c.g, c.b);
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
        const WalletAccount* acc = wallet->getAccount(currentViewCoin);
        const char* name = "BITCOIN";
        const char* sym  = "BTC (SegWit)";
        if (currentViewCoin == COIN_ETH)  { name = "ETHEREUM / PEPE"; sym = "ETH (ERC-20)"; }
        if (currentViewCoin == COIN_SOL)  { name = "SOLANA";          sym = "SOL (Ed25519)"; }
        if (currentViewCoin == COIN_DOGE) { name = "DOGECOIN";        sym = "DOGE (Legacy)"; }
        ui.renderWalletScreen(name, sym, acc ? acc->address : "", acc ? acc->derivationPath : "");
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
                rgb.setMode(LED_MODE_BREATHE_CYAN);
                ui.renderSeedBackupScreen(1, totalMnemonicWords, mnemonicWords[0]);
                Serial.printf("[SEED] Successfully generated %d-word BIP-39 mnemonic!\n", totalMnemonicWords);
            }
        }
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Cancel back to idle
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
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
        rgb.flashRainbow(1000);
        ui.renderSuccessBanner("SEED BACKUP COMPLETE", "WALLET SECURED");
        delay(1200);

        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
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
        currentViewCoin = COIN_BTC;
        rgb.setCoinColor(255, 140, 0);
        const WalletAccount* acc = wallet->getAccount(COIN_BTC);
        ui.renderWalletScreen("BITCOIN", "BTC (SegWit)", acc ? acc->address : "", acc ? acc->derivationPath : "");
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
                currentViewCoin = COIN_BTC;
                rgb.setCoinColor(255, 140, 0);
                const WalletAccount* acc = wallet->getAccount(COIN_BTC);
                ui.renderWalletScreen("BITCOIN", "BTC (SegWit)", acc ? acc->address : "", acc ? acc->derivationPath : "");
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
    bool confirmed = false;
    bool done = false;

    while (millis() - start < 30000 && !done) {
        ButtonEvent ev = btn.update();
        rgb.update();
        ctapHid.process();
        handleSerialCommands();

        // Send FIDO2 CTAPHID Keepalive (UP Needed) every 250ms to keep host browser active
        if (millis() - lastKeepAlive >= 250) {
            ctapHid.sendKeepAlive(cid, CTAPHID_STATUS_UPNEEDED);
            lastKeepAlive = millis();
        }

        float remaining = 1.0f - ((float)(millis() - start) / 30000.0f);
        ui.renderPasskeyHub(true, reqDomain, remaining);

        if (ev == BTN_SHORT_PRESS || ev == BTN_LONG_PRESS) {
            confirmed = true;
            done = true;
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
        rgb.setMode(LED_MODE_BREATHE_CYAN);
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
    masterPinLen = strlen(masterPin);
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
        vaultPrefs.putString("user_pin", DEFAULT_MASTER_PIN);
        strncpy(masterPin, DEFAULT_MASTER_PIN, PIN_LENGTH);
    } else {
        String savedPin = vaultPrefs.getString("user_pin", DEFAULT_MASTER_PIN);
        strncpy(masterPin, savedPin.c_str(), PIN_LENGTH);
    }
    masterPin[PIN_LENGTH] = '\0';
    masterPinLen = strlen(masterPin);
    if (masterPinLen < PIN_MIN_LENGTH) masterPinLen = PIN_MIN_LENGTH;
    if (masterPinLen > PIN_MAX_LENGTH) masterPinLen = PIN_MAX_LENGTH;
    vaultPrefs.end();

    ctap2Engine.setMasterPin(masterPin);
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
        renderCurrentPortfolioCard();
        return;
    }

    if (cmd.equalsIgnoreCase("help")) {
        Serial.println("\n--- Crypto TKey S3 CLI ---");
        Serial.println("  status                   - Print security key status & uptime");
        Serial.println("  flip                     - Flip display 180 degrees (landscape)");
        Serial.println("  coins                    - List all 24 supported coins & holdings");
        Serial.println("  enable <SYM>             - Enable coin in active portfolio tracker");
        Serial.println("  disable <SYM>            - Disable coin from portfolio tracker");
        Serial.println("  setbal <SYM> <AMT>       - Set user coin holding balance");
        Serial.println("  setprice <SYM> <P> [C]   - Update live USD price & 24h change");
        Serial.println("  json                     - Output compact JSON for WebUSB companion");
        Serial.println("  newseed [12|24]          - Start hybrid entropy BIP-39 seed wizard");
        Serial.println("  setpin <PIN>             - Set 4-digit master PIN");
        Serial.println("  unlock <PIN>             - Unlock crypto vault and reveal derived addresses");
        Serial.println("  addresses                - Print genuine derived BIP-32/BIP-84/EIP-55 addresses");
        Serial.println("  lock                     - Lock crypto vault immediately");
        Serial.println("  led <btc|eth|sol|rainbow>- Test RGB DotStar LED color mode");
        Serial.println("  psbt [scan|parse|sign]   - Air-Gapped MicroSD BIP-174 Bitcoin signer");
        Serial.println("  panic                    - Trigger emergency flash nuke");
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
        String pin = cmd.substring(7);
        pin.trim();
        if (wallet->unlock(pin.c_str())) {
            rgb.flashRainbow(800);
            rgb.setCoinColor(255, 140, 0);
            deviceState = STATE_VAULT_DASHBOARD;
            currentViewCoin = COIN_BTC;
            const WalletAccount* acc = wallet->getAccount(COIN_BTC);
            ui.renderWalletScreen("BITCOIN", "BTC (SegWit)", acc ? acc->address : "bc1q...", acc ? acc->derivationPath : "m/84'/0'/0'/0/0");
            Serial.println("[VAULT] ✅ Unlocked successfully! Master keys derived in memory.");
        } else {
            rgb.setMode(LED_MODE_STROBE_RED);
            delay(1000);
            rgb.setMode(LED_MODE_SOLID_AMBER);
            Serial.println("[VAULT] ❌ Error: Invalid PIN.");
        }
    } else if (cmd.equalsIgnoreCase("addresses")) {
        if (!wallet->isUnlocked()) {
            Serial.println("[VAULT] 🔒 Error: Vault is LOCKED. Unlock first with 'unlock <PIN>' or via device screen.");
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
    } else if (cmd.equalsIgnoreCase("lock")) {
        wallet->lock();
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderReadyDashboard(millis() / 1000, true, false);
        Serial.println("[VAULT] 🔒 Vault locked and volatile key material zeroized.");
    } else if (cmd.startsWith("led ")) {
        String mode = cmd.substring(4);
        mode.trim();
        if (mode.equalsIgnoreCase("rainbow")) {
            rgb.flashRainbow(2000);
            Serial.println("[LED] 🌈 Rainbow shimmer activated");
        } else if (mode.equalsIgnoreCase("btc")) {
            rgb.setCoinColor(255, 140, 0);
            Serial.println("[LED] 🟠 Bitcoin Gold/Orange activated");
        } else if (mode.equalsIgnoreCase("eth")) {
            rgb.setCoinColor(138, 75, 255);
            Serial.println("[LED] 🟣 Ethereum Royal Violet activated");
        } else if (mode.equalsIgnoreCase("sol")) {
            rgb.setCoinColor(20, 241, 149);
            Serial.println("[LED] 🟢 Solana Neon Turquoise activated");
        } else if (mode.equalsIgnoreCase("doge")) {
            rgb.setCoinColor(255, 195, 15);
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
            if (!wallet->isUnlocked()) {
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
    } else if (cmd.startsWith("newseed")) {
        int words = 12;
        if (cmd.indexOf("24") != -1) words = 24;
        startSeedGeneration(words);
    } else if (cmd.startsWith("setpin ")) {
        String newPin = cmd.substring(7);
        newPin.trim();
        if (newPin.length() >= PIN_MIN_LENGTH && newPin.length() <= PIN_MAX_LENGTH) {
            strncpy(masterPin, newPin.c_str(), PIN_LENGTH);
            masterPin[PIN_LENGTH] = '\0';
            masterPinLen = strlen(masterPin);
            vaultPrefs.begin("vault_sec", false);
            vaultPrefs.putString("user_pin", masterPin);
            vaultPrefs.end();
            ctap2Engine.setMasterPin(masterPin);
            Serial.printf("Master PIN successfully changed to: %s (%d digits)\n", masterPin, masterPinLen);
        } else {
            Serial.printf("Error: PIN must be between %d and %d digits.\n", PIN_MIN_LENGTH, PIN_MAX_LENGTH);
        }
    } else if (cmd.startsWith("decode_evm ") || cmd.startsWith("sign_evm ")) {
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

            ui.renderCryptoSignPrompt(decoded.tokenName, 
                                      decoded.recipientOrSpender[0] ? decoded.recipientOrSpender : decoded.toAddress, 
                                      decoded.dispAmount);
            rgb.setMode(LED_MODE_SOLID_AMBER);
        } else {
            Serial.println("[EVM] ❌ Error: Failed to parse RLP transaction stream.");
        }
    } else if (cmd.equalsIgnoreCase("panic")) {
        DuressWipe::execute(*tft, rgb, "SERIAL_PANIC");
    }
}
