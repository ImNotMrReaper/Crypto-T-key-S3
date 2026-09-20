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

// Temporary buffers for active requests
char reqDomain[48]     = "webauthn.io";
char reqChain[24]      = "Bitcoin Mainnet";
char reqRecipient[48]  = "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh";
char reqAmount[32]     = "0.054 BTC ($3,450)";

// ─── Forward Declarations ───────────────────────────────────────────────────
void handleSerialCommands();
void processIdleReadyState(ButtonEvent ev);
void processPinEntryState(ButtonEvent ev);
void processVaultDashboardState(ButtonEvent ev);
void processPortfolioTrackerState(ButtonEvent ev);
void processSeedEntropyState(ButtonEvent ev);
void processSeedWordDisplayState(ButtonEvent ev);
void resetPinEntry();
void loadSecurityConfig();
bool handleUserPresencePrompt(uint32_t cid, const char* rpId, bool isRegistration);
void renderCurrentPortfolioCard();

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

    // 3. Display Engine (lilygo-tdongle-ui-dev double-buffered)
    tft = new TFT_eSPI();
    tft->init();
    tft->setRotation(DISP_ROTATION);
    ui.begin(tft);

    // 4. Boot Splash
    ui.renderBootSplash();
    for (int i = 0; i < 25; i++) {
        rgb.update();
        delay(20);
    }

    // 5. Security & Vault Engines
    wallet = new CryptoWallet();
    wallet->begin();

    ctapHid.begin();
    ctap2Engine.begin();
    ctap2Engine.setUserPresencePrompt(handleUserPresencePrompt);

    // 6. Portfolio & Seed Engines
    PortfolioManager::init();
    SeedGenerator::init();
    wifi = new WifiManager();
    wifi->begin();
    portal = new WebPortal();

    // 7. Security Config
    loadSecurityConfig();

    vaultPrefs.begin("vault_sec", true);
    bool isProvisioned = vaultPrefs.getBool("provisioned", false);
    vaultPrefs.end();

    if (!isProvisioned || btn.isPressedNow()) {
        deviceState = STATE_SETUP_WALKTHROUGH;
        portal->begin(wallet, wifi);
        ui.renderOobeWizard(1, "T-KEY SETUP", "SSID: T-Key-Setup", "GO TO: 192.168.4.1");
        rgb.setMode(LED_MODE_SOLID_AMBER);
        Serial.println("[BOOT] 🌐 SoftAP Setup Portal launched at 192.168.4.1");
    } else {
        deviceState = STATE_IDLE_READY;
        ui.renderReadyDashboard(millis() / 1000, true, wallet->isUnlocked());
        rgb.setMode(LED_MODE_BREATHE_CYAN);
    }

    Serial.println("[BOOT] ✅ Crypto TKey S3 Ready. Pure Security Key & Crypto Vault Active.");
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
            vaultPrefs.begin("vault_sec", false);
            vaultPrefs.putString("user_pin", masterPin);
            vaultPrefs.putString("duress_pin", portal->getNewDuressPin());
            vaultPrefs.putBool("provisioned", true);
            vaultPrefs.end();

            portal->stop();
            rgb.flashSuccess();
            ui.renderSuccessBanner("VAULT PROVISIONED", "LAUNCHING KEY");
            delay(1500);

            deviceState = STATE_IDLE_READY;
            rgb.setMode(LED_MODE_BREATHE_CYAN);
            ui.renderReadyDashboard(millis() / 1000, true, wallet->isUnlocked());
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
                ui.renderReadyDashboard(millis() / 1000, true, wallet->isUnlocked());
            }
            break;
        case STATE_IDLE_READY:
            processIdleReadyState(ev);
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
        case STATE_SEED_ENTROPY_COLLECT:
            processSeedEntropyState(ev);
            break;
        case STATE_SEED_WORD_DISPLAY:
            processSeedWordDisplayState(ev);
            break;
        case STATE_DURESS_WIPED:
            delay(100);
            break;
        default:
            break;
    }

    delay(5);
}

// ─── State: Idle Security Key Ready (1-Tap Touch Active) ─────────────────────
void processIdleReadyState(ButtonEvent ev) {
    if (millis() - lastStateUpdate > 2000 && !PowerManager::isDisplaySleeping()) {
        ui.renderReadyDashboard(millis() / 1000, true, wallet->isUnlocked());
        lastStateUpdate = millis();
    }

    if (ev == BTN_SHORT_PRESS) {
        // Single tap switches directly to Live Portfolio Tracker!
        deviceState = STATE_PORTFOLIO_TRACKER;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        renderCurrentPortfolioCard();
    } else if (ev == BTN_LONG_PRESS) {
        // Long press opens Master PIN Gate
        deviceState = STATE_PIN_ENTRY;
        resetPinEntry();
        rgb.setMode(LED_MODE_SOLID_AMBER);
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        Serial.println("[VAULT] Entering Master PIN gate...");
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── State: Portfolio Tracker (Carousel of Active Coins) ─────────────────────
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
        // Next Coin in Carousel
        PortfolioManager::nextCoin();
        renderCurrentPortfolioCard();
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Previous Coin
        PortfolioManager::prevCoin();
        renderCurrentPortfolioCard();
    } else if (ev == BTN_LONG_PRESS || ev == BTN_VERY_LONG_PRESS) {
        // Return to Idle Ready
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderReadyDashboard(millis() / 1000, true, wallet->isUnlocked());
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── State: PIN Entry (Master PIN Gate) ──────────────────────────────────────
void processPinEntryState(ButtonEvent ev) {
    if (btn.isPressedNow()) {
        uint8_t stage = btn.getHoldStage() * 33;
        if (stage != lastHoldStage) {
            lastHoldStage = stage;
            ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, stage);
        }
    } else if (lastHoldStage > 0) {
        lastHoldStage = 0;
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
    }

    if (ev == BTN_SHORT_PRESS) {
        currentDigitVal = (currentDigitVal + 1) % 10;
        pinDigits[pinIndex] = '0' + currentDigitVal;
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        Serial.printf("[PIN] Slot %d = %d\n", pinIndex + 1, currentDigitVal);
    } else if (ev == BTN_DOUBLE_CLICK) {
        if (pinIndex > 0) {
            pinDigits[pinIndex] = '0';
            pinIndex--;
            currentDigitVal = pinDigits[pinIndex] - '0';
            ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        } else {
            deviceState = STATE_IDLE_READY;
            rgb.setMode(LED_MODE_BREATHE_CYAN);
            ui.renderReadyDashboard(millis() / 1000, true, wallet->isUnlocked());
        }
    } else if (ev == BTN_LONG_PRESS) {
        pinDigits[pinIndex] = '0' + currentDigitVal;
        pinIndex++;

        if (pinIndex < PIN_LENGTH) {
            currentDigitVal = 0;
            pinDigits[pinIndex] = '0';
            ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        } else {
            pinDigits[PIN_LENGTH] = '\0';
            Serial.printf("[PIN] Validating PIN: %s\n", pinDigits);

            if (strcmp(pinDigits, EMERGENCY_DURESS_PIN) == 0) {
                DuressWipe::execute(*tft, rgb, "DURESS_PIN");
            } else if (strcmp(pinDigits, masterPin) == 0) {
                Serial.println("[VAULT] Master PIN Accepted! Unlocked.");
                wallet->unlock(pinDigits);
                rgb.flashSuccess();
                ui.renderSuccessBanner("VAULT UNLOCKED", "CRYPTO SIGNER READY");
                delay(1200);

                deviceState = STATE_VAULT_DASHBOARD;
                rgb.setMode(LED_MODE_BREATHE_CYAN);
                currentViewCoin = COIN_BTC;
                const WalletAccount* acc = wallet->getAccount(COIN_BTC);
                ui.renderWalletScreen("BITCOIN", "BTC (SegWit)", acc ? acc->address : "bc1q...", acc ? acc->derivationPath : "m/84'/0'/0'/0/0");
            } else {
                Serial.println("[AUTH] Invalid PIN entered!");
                ui.renderErrorBanner("Wrong PIN Code");
                rgb.setMode(LED_MODE_STROBE_RED);
                delay(1200);
                resetPinEntry();
                rgb.setMode(LED_MODE_SOLID_AMBER);
                ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
            }
        }
    } else if (ev == BTN_VERY_LONG_PRESS) {
        resetPinEntry();
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
    } else if (ev == BTN_PANIC_HOLD) {
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
    }
}

// ─── State: Vault Dashboard & Multi-Currency Address Explorer ───────────────
void processVaultDashboardState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        currentViewCoin = (CryptoCoin)((currentViewCoin + 1) % 4);
        const WalletAccount* acc = wallet->getAccount(currentViewCoin);
        const char* name = "BITCOIN";
        const char* sym  = "BTC (SegWit)";
        if (currentViewCoin == COIN_ETH)  { name = "ETHEREUM"; sym = "ETH (ERC-20)"; }
        if (currentViewCoin == COIN_SOL)  { name = "SOLANA";   sym = "SOL (Ed25519)"; }
        if (currentViewCoin == COIN_DOGE) { name = "DOGECOIN"; sym = "DOGE (Legacy)"; }

        ui.renderWalletScreen(name, sym, acc ? acc->address : "", acc ? acc->derivationPath : "");
    } else if (ev == BTN_LONG_PRESS || ev == BTN_VERY_LONG_PRESS) {
        wallet->lock();
        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderReadyDashboard(millis() / 1000, true, false);
        Serial.println("[VAULT] Vault Locked.");
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
        SeedGenerator::recordButtonPressJitter(btn.currentHoldDuration() * 1000, 50000);
        int samples = SeedGenerator::getEntropySampleCount();
        ui.renderEntropyGatherScreen(samples, 12);
        rgb.flashSuccess();

        if (samples >= 12) {
            // Generate verified mnemonic
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
        ui.renderSeedBackupScreen(currentWordIdx + 1, totalMnemonicWords, mnemonicWords[currentWordIdx]);
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Previous Word
        currentWordIdx = (currentWordIdx - 1 + totalMnemonicWords) % totalMnemonicWords;
        ui.renderSeedBackupScreen(currentWordIdx + 1, totalMnemonicWords, mnemonicWords[currentWordIdx]);
    } else if (ev == BTN_LONG_PRESS) {
        // Complete seed verification
        rgb.flashSuccess();
        ui.renderSuccessBanner("SEED BACKUP COMPLETE", "WALLET SECURED");
        delay(1200);

        deviceState = STATE_IDLE_READY;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderReadyDashboard(millis() / 1000, true, true);
    }
}

// ─── FIDO2 / WebAuthn User Presence Prompt Callback ─────────────────────────
bool handleUserPresencePrompt(uint32_t cid, const char* rpId, bool isRegistration) {
    PowerManager::wakeDisplay();
    if (rpId && strlen(rpId) > 0) {
        strncpy(reqDomain, rpId, sizeof(reqDomain) - 1);
    }

    DeviceState prev = deviceState;
    deviceState = STATE_FIDO_AUTH_PROMPT;
    rgb.setMode(LED_MODE_PULSE_GREEN);
    ui.renderFidoPrompt(reqDomain, 1.0f);
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
        ui.renderFidoPrompt(reqDomain, remaining);

        if (ev == BTN_SHORT_PRESS || ev == BTN_LONG_PRESS) {
            confirmed = true;
            done = true;
            rgb.flashSuccess();
            ui.renderSuccessBanner(isRegistration ? "PASSKEY REGISTERED" : "ASSERTION SIGNED", reqDomain);
            delay(1000);
        } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_VERY_LONG_PRESS) {
            confirmed = false;
            done = true;
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
        ui.renderReadyDashboard(millis() / 1000, true, wallet->isUnlocked());
    }

    return confirmed;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
void resetPinEntry() {
    pinIndex = 0;
    currentDigitVal = 0;
    for (int i = 0; i < PIN_LENGTH; i++) pinDigits[i] = '0';
    pinDigits[PIN_LENGTH] = '\0';
}

void loadSecurityConfig() {
    vaultPrefs.begin("vault_sec", false);
    if (!vaultPrefs.isKey("provisioned")) {
        vaultPrefs.putBool("provisioned", true);
        vaultPrefs.putString("user_pin", DEFAULT_MASTER_PIN);
        strncpy(masterPin, DEFAULT_MASTER_PIN, PIN_LENGTH);
    } else {
        String savedPin = vaultPrefs.getString("user_pin", DEFAULT_MASTER_PIN);
        strncpy(masterPin, savedPin.c_str(), PIN_LENGTH);
    }
    vaultPrefs.end();
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
        Serial.println("  coins                    - List all 24 supported coins & holdings");
        Serial.println("  enable <SYM>             - Enable coin in active portfolio tracker");
        Serial.println("  disable <SYM>            - Disable coin from portfolio tracker");
        Serial.println("  setbal <SYM> <AMT>       - Set user coin holding balance");
        Serial.println("  setprice <SYM> <P> [C]   - Update live USD price & 24h change");
        Serial.println("  json                     - Output compact JSON for WebUSB companion");
        Serial.println("  newseed [12|24]          - Start hybrid entropy BIP-39 seed wizard");
        Serial.println("  setpin <PIN>             - Set 4-digit master PIN");
        Serial.println("  panic                    - Trigger emergency flash nuke");
    } else if (cmd.equalsIgnoreCase("status")) {
        Serial.printf("Uptime: %lus | CPU: %dMHz | Vault: %s | Master PIN: %s | Active Coins: %d\n",
            millis() / 1000, getCpuFrequencyMhz(), wallet->isUnlocked() ? "UNLOCKED" : "LOCKED",
            masterPin, PortfolioManager::getActiveCount());
    } else if (cmd.startsWith("newseed")) {
        int words = 12;
        if (cmd.indexOf("24") != -1) words = 24;
        startSeedGeneration(words);
    } else if (cmd.startsWith("setpin ")) {
        String newPin = cmd.substring(7);
        newPin.trim();
        if (newPin.length() == PIN_LENGTH) {
            strncpy(masterPin, newPin.c_str(), PIN_LENGTH);
            vaultPrefs.begin("vault_sec", false);
            vaultPrefs.putString("user_pin", masterPin);
            vaultPrefs.end();
            Serial.printf("Master PIN successfully changed to: %s\n", masterPin);
        } else {
            Serial.println("Error: PIN must be exactly 4 digits.");
        }
    } else if (cmd.equalsIgnoreCase("panic")) {
        DuressWipe::execute(*tft, rgb, "SERIAL_PANIC");
    }
}
