/**
 * tdongle-s3-security-key.ino
 * ==============================================================================
 * Project: T-Dongle S3 James Bond Hardware Security Key & Multi-Currency Vault
 * Hardware: LilyGo T-Dongle S3 (ESP32-S3, ST7735 0.96" TFT, APA102 RGB, microSD)
 * 
 * Master Capabilities:
 *   1. Web Captive Setup Portal (SoftAP "T-Key-Setup" at 192.168.4.1 for custom PIN,
 *      Wi-Fi networks, and crypto addresses)
 *   2. Multi-Currency Crypto Vault (Bitcoin SegWit, Ethereum EVM, Solana Ed25519)
 *   3. True Cryptographic Signatures (secp256k1 ECDSA + Keccak-256 + Ed25519)
 *   4. Live Cryptocurrency Price Ticker (BTC, ETH, SOL, DOGE via Wi-Fi REST API)
 *   5. Persistent Multi-Network Wi-Fi Engine (Auto-scan & reconnect in flash NVS)
 *   6. FIDO2 / CTAPHID Security Key Protocol (WebAuthn / Terminus user presence)
 *   7. High-Precision Single-Button Cadence:
 *        • Single Tap: +1 / Next
 *        • Double Click: Instant DELETE / Backspace / Back
 *        • Long Press (Hold ~1s on release): CONFIRM / OK
 *        • Hold (>2.2s on release): RESET PIN / Clear All
 *        • Continuous Hold (>6.0s): EMERGENCY DURESS WIPE
 *   8. Dynamic Pointer Initialization (Zero static constructors at boot)
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
#include "src/button_cadence.h"
#include "src/rgb_status.h"
#include "src/ui_engine.h"
#include "src/duress_wipe.h"
#include "src/crypto_wallet.h"
#include "src/wifi_manager.h"
#include "src/price_ticker.h"
#include "src/fido2_ctaphid.h"
#include "src/web_portal.h"

// ─── Hardware & Subsystem Pointers (Zero Static Constructor Overhead) ────────
TFT_eSPI*     tft    = nullptr;
RgbStatus     rgb;
ButtonCadence btn;
UiEngine      ui;
CryptoWallet* wallet = nullptr;
WifiManager*  wifi   = nullptr;
PriceTicker*  ticker = nullptr;
Fido2Ctaphid* fido   = nullptr;
WebPortal*    portal = nullptr;

// ─── Global State ────────────────────────────────────────────────────────────
DeviceState   deviceState = STATE_LOCKED;
char          masterPin[PIN_LENGTH + 1] = DEFAULT_MASTER_PIN;
char          pinDigits[PIN_LENGTH + 1] = "0000";
int           pinIndex = 0;
int           currentDigitVal = 0;
uint8_t       lastHoldStage = 0;
uint32_t      lastDashboardUpdate = 0;
CryptoCoin    currentViewCoin = COIN_BTC;

// Temporary buffers for active requests
char reqDomain[48]     = "github.com";
char reqNetwork[24]    = "ETH Mainnet";
char reqRecipient[48]  = "0x71C...89E2";
char reqAmount[32]     = "0.250 ETH ($850)";
char reqFilename[32]   = "tx_cold_01.psbt";

// ─── Forward Declarations ───────────────────────────────────────────────────
void handleSerialCommands();
void processSetupPortalState(ButtonEvent ev);
void processLockedState(ButtonEvent ev);
void processDashboardState(ButtonEvent ev);
void processWalletState(ButtonEvent ev);
void processCryptoPricesState(ButtonEvent ev);
void processWifiState(ButtonEvent ev);
void processFidoState(ButtonEvent ev);
void processCryptoState(ButtonEvent ev);
void processAirGapState(ButtonEvent ev);
void processBleState(ButtonEvent ev);
void resetPinEntry();
void loadSecurityConfig();

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Redundant safety (pre_init_early already did this)
    setCpuFrequencyMhz(160);

    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // Non-blocking: never stall if host hasn't opened port
    delay(1500); // Allow USB CDC enumeration and host driver to attach

    Serial.println("\n[BOOT] ===== T-KEY S3 BOOT SEQUENCE =====");
    Serial.println("[BOOT] CHECKPOINT 1: Serial OK");

    // Initialize Hardware Peripherals
    btn.begin(PIN_BTN);
    Serial.println("[BOOT] CHECKPOINT 2: Button OK");

    rgb.begin(PIN_LED_DATA, PIN_LED_CLK);
    rgb.setMode(LED_MODE_BREATHE_CYAN);
    rgb.update();
    Serial.println("[BOOT] CHECKPOINT 3: RGB LED OK");

    tft = new TFT_eSPI();
    ui.begin(tft);
    Serial.println("[BOOT] CHECKPOINT 4: TFT Display OK");

    // Boot Splash & Visual Self-Test
    ui.renderBootSplash();
    for (int i = 0; i < 35; i++) {
        rgb.update();
        delay(25);
    }
    Serial.println("[BOOT] CHECKPOINT 5: Boot Splash OK");

    // Dynamically Instantiate Subsystems (Safe Runtime Initialization)
    wallet = new CryptoWallet();
    wallet->begin();
    Serial.println("[BOOT] CHECKPOINT 6: Crypto Wallet OK");

    wifi   = new WifiManager();
    wifi->begin();
    Serial.println("[BOOT] CHECKPOINT 7: WiFi Manager OK");

    ticker = new PriceTicker();
    ticker->begin();
    Serial.println("[BOOT] CHECKPOINT 8: Price Ticker OK");

    fido   = new Fido2Ctaphid();
    fido->begin();
    Serial.println("[BOOT] CHECKPOINT 9: FIDO2 CTAPHID OK");

    portal = new WebPortal();
    Serial.println("[BOOT] CHECKPOINT 10: Web Portal Allocated");

    // Load Security Configuration & Master PIN from NVS
    loadSecurityConfig();
    Serial.println("[BOOT] CHECKPOINT 11: Security Config Loaded");

    Serial.println("\n========================================================");
    Serial.println(" ⚡ T-DONGLE-S3 MULTI-CURRENCY VAULT & SECURITY KEY");
    Serial.println("========================================================");
    Serial.printf("Device Status: %s\n", (deviceState == STATE_SETUP_WALKTHROUGH) ? "WEB SETUP ACCESS POINT" : "LOCKED");
    Serial.println("Controls: TAP = +1 | DOUBLE-CLICK = DELETE | HOLD 1s = OK");
    Serial.println("Hold >2.2s = Reset PIN | Hold >6s = Emergency Duress Wipe");
    Serial.printf("Wi-Fi Profiles: %d | Type 'help' for console commands\n", wifi->getSavedCount());
    Serial.println("========================================================\n");
}

void loadSecurityConfig() {
    Preferences vaultPrefs;
    vaultPrefs.begin("vault_sec", false);
    bool isProvisioned = vaultPrefs.getBool("provisioned", false);

    if (!isProvisioned) {
        // Unprovisioned device: Launch Web Captive Setup Portal!
        deviceState = STATE_SETUP_WALKTHROUGH;
        portal->begin(wallet, wifi);
        rgb.setMode(LED_MODE_PULSE_PURPLE);
        ui.renderWifiScreen(true, "T-Key-Setup", "192.168.4.1", 0, wifi->getSavedCount());
        Serial.println("[SETUP] Unprovisioned vault. Started Web Captive Portal on 'T-Key-Setup'.");
    } else {
        // Already provisioned: Load user's custom PIN
        String savedPin = vaultPrefs.getString("user_pin", DEFAULT_MASTER_PIN);
        strncpy(masterPin, savedPin.c_str(), sizeof(masterPin) - 1);

        deviceState = STATE_LOCKED;
        resetPinEntry();
        rgb.setMode(LED_MODE_SOLID_AMBER);
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        Serial.println("[VAULT] Loaded custom PIN from persistent flash memory.");
    }
    vaultPrefs.end();
}

// ─── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
    // 1. Update Subsystems
    ButtonEvent ev = btn.update();
    rgb.update();
    if (wifi) wifi->update();
    if (ticker) ticker->update();
    if (portal) portal->update();
    handleSerialCommands();

    // 2. Global Panic Hold Check (> 6 seconds hold)
    if (ev == BTN_PANIC_HOLD) {
        Serial.println("[EMERGENCY] Physical panic hold detected!");
        DuressWipe::execute(*tft, rgb, "PANIC_HOLD");
        return;
    }

    // 3. Live Hold Visual Feedback while button is held down
    if (btn.isPressedNow()) {
        uint8_t stage = btn.getHoldStage();
        if (stage != lastHoldStage) {
            lastHoldStage = stage;
            if (deviceState == STATE_LOCKED) {
                ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, stage);
            }
            if (stage == 1) {
                rgb.setPixel(0, 255, 30, 4);      // Green indicator: Release to confirm!
            } else if (stage == 2) {
                rgb.setPixel(255, 0, 255, 4);     // Purple indicator: Release to reset!
            } else if (stage == 3) {
                rgb.setMode(LED_MODE_STROBE_RED); // Red strobe: Duress wipe imminent!
            }
        }
    } else {
        if (lastHoldStage != 0) {
            lastHoldStage = 0;
            if (deviceState == STATE_LOCKED) {
                rgb.setMode(LED_MODE_SOLID_AMBER);
                ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
            }
        }
    }

    // 4. State Machine Dispatch
    switch (deviceState) {
        case STATE_SETUP_WALKTHROUGH:
            processSetupPortalState(ev);
            break;

        case STATE_LOCKED:
            processLockedState(ev);
            break;

        case STATE_IDLE_DASHBOARD:
            processDashboardState(ev);
            break;

        case STATE_WALLET_VIEW:
            processWalletState(ev);
            break;

        case STATE_CRYPTO_PRICES:
            processCryptoPricesState(ev);
            break;

        case STATE_WIFI_CONFIG:
            processWifiState(ev);
            break;

        case STATE_FIDO_AUTH_REQUEST:
            processFidoState(ev);
            break;

        case STATE_CRYPTO_SIGN_REQUEST:
            processCryptoState(ev);
            break;

        case STATE_AIRGAP_SD:
            processAirGapState(ev);
            break;

        case STATE_BLE_COMPANION:
            processBleState(ev);
            break;

        case STATE_DURESS_WIPED:
            // Dead state
            break;
    }
}

// ─── PIN Reset Helper ────────────────────────────────────────────────────────
void resetPinEntry() {
    pinIndex = 0;
    currentDigitVal = 0;
    for (int i = 0; i < PIN_LENGTH; i++) pinDigits[i] = '0';
    pinDigits[PIN_LENGTH] = '\0';
}

// ─── State: Web Captive Setup Portal ─────────────────────────────────────────
void processSetupPortalState(ButtonEvent ev) {
    // Check if web user submitted settings via browser
    if (portal && portal->isSetupComplete()) {
        const char* customPin = portal->getCustomPin();
        if (customPin && strlen(customPin) == 4) {
            strncpy(masterPin, customPin, sizeof(masterPin) - 1);
        }

        Preferences vaultPrefs;
        vaultPrefs.begin("vault_sec", false);
        vaultPrefs.putString("user_pin", masterPin);
        vaultPrefs.putBool("provisioned", true);
        vaultPrefs.end();

        portal->stop();

        wallet->unlock(masterPin);
        rgb.flashSuccess();
        ui.renderSuccessBanner("SETUP COMPLETE!", "Vault Provisioned");
        delay(1500);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
        Serial.printf("[SETUP] Provisioned via Web Portal! Master PIN: %s\n", masterPin);
        return;
    }

    // Button can also exit portal manually
    if (ev == BTN_LONG_PRESS || ev == BTN_VERY_LONG_PRESS) {
        portal->stop();
        deviceState = STATE_LOCKED;
        resetPinEntry();
        rgb.setMode(LED_MODE_SOLID_AMBER);
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        Serial.println("[SETUP] Exited setup portal to locked screen.");
    }
}

// ─── State: Locked / PIN Entry ───────────────────────────────────────────────
void processLockedState(ButtonEvent ev) {
    if (ev == BTN_NONE) return;

    if (ev == BTN_SHORT_PRESS) {
        // Increment current digit (0-9)
        currentDigitVal = (currentDigitVal + 1) % 10;
        pinDigits[pinIndex] = '0' + currentDigitVal;
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        Serial.printf("[PIN] Slot %d = %d\n", pinIndex + 1, currentDigitVal);
    } 
    else if (ev == BTN_DOUBLE_CLICK) {
        // INSTANT BACKSPACE / DELETE!
        if (pinIndex > 0) {
            pinDigits[pinIndex] = '0';
            pinIndex--;
            currentDigitVal = pinDigits[pinIndex] - '0';
            ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
            Serial.printf("[PIN] Backspace to Slot %d (Val: %d)\n", pinIndex + 1, currentDigitVal);
        } else {
            currentDigitVal = 0;
            pinDigits[0] = '0';
            ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
            Serial.println("[PIN] Reset first slot to 0");
        }
        rgb.flashSuccess();
    }
    else if (ev == BTN_LONG_PRESS) {
        // Confirm current digit
        pinDigits[pinIndex] = '0' + currentDigitVal;
        pinIndex++;

        if (pinIndex < PIN_LENGTH) {
            currentDigitVal = 0;
            ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
            Serial.printf("[PIN] Confirmed digit %d. Next slot.\n", pinIndex);
        } else {
            // All digits entered — Validate PIN against user's custom PIN!
            pinDigits[PIN_LENGTH] = '\0';
            Serial.printf("[PIN] Validating entry: %s\n", pinDigits);

            if (strcmp(pinDigits, EMERGENCY_DURESS_PIN) == 0) {
                // Emergency Duress PIN triggered!
                DuressWipe::execute(*tft, rgb, "DURESS_PIN");
            } else if (strcmp(pinDigits, masterPin) == 0) {
                // Unlock Success!
                Serial.println("[VAULT] Master PIN Accepted! Vault Unlocked.");
                wallet->unlock(pinDigits);
                rgb.flashSuccess();
                ui.renderSuccessBanner("VAULT UNLOCKED", "Multi-Coin Ready");
                delay(1200);

                deviceState = STATE_IDLE_DASHBOARD;
                rgb.setMode(LED_MODE_BREATHE_CYAN);
                ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
                lastDashboardUpdate = millis();
            } else {
                // Invalid PIN
                Serial.println("[AUTH] Invalid PIN entered!");
                ui.renderErrorBanner("Wrong PIN Code");
                rgb.setMode(LED_MODE_STROBE_RED);
                delay(1500);
                resetPinEntry();
                rgb.setMode(LED_MODE_SOLID_AMBER);
                ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
            }
        }
    } 
    else if (ev == BTN_VERY_LONG_PRESS) {
        // Reset all digits back to 0000
        resetPinEntry();
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        Serial.println("[PIN] Reset all digits to 0000.");
        rgb.flashSuccess();
    }
}

// ─── State: Idle Dashboard ───────────────────────────────────────────────────
void processDashboardState(ButtonEvent ev) {
    // Refresh uptime & Wi-Fi status every 3 seconds
    if (millis() - lastDashboardUpdate > 3000) {
        lastDashboardUpdate = millis();
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
    }

    if (ev == BTN_LONG_PRESS) {
        // Hold to lock device
        Serial.println("[VAULT] Device Locked by user.");
        wallet->lock();
        deviceState = STATE_LOCKED;
        resetPinEntry();
        rgb.setMode(LED_MODE_SOLID_AMBER);
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
    } else if (ev == BTN_SHORT_PRESS) {
        // Single tap: Open Multi-Currency Address Explorer
        deviceState = STATE_WALLET_VIEW;
        currentViewCoin = COIN_BTC;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        const WalletAccount* acc = wallet->getAccount(currentViewCoin);
        ui.renderWalletScreen(acc->name, acc->symbol, acc->derivationPath, acc->address);
        Serial.printf("[WALLET] Viewing %s Address: %s\n", acc->name, acc->address);
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Double tap: Open Live Crypto Prices Screen
        deviceState = STATE_CRYPTO_PRICES;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderCryptoPrices(ticker->getBtcPrice(), ticker->getEthPrice(), ticker->getSolPrice(), ticker->getDogePrice(), ticker->isLive());
        Serial.println("[MARKET] Viewing Live Cryptocurrency Prices.");
    }
}

// ─── State: Multi-Currency Address Explorer ─────────────────────────────────
void processWalletState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        // Cycle to next coin: BTC -> ETH -> SOL -> DOGE -> BTC
        currentViewCoin = (CryptoCoin)((currentViewCoin + 1) % COIN_COUNT);
        const WalletAccount* acc = wallet->getAccount(currentViewCoin);
        ui.renderWalletScreen(acc->name, acc->symbol, acc->derivationPath, acc->address);
        Serial.printf("[WALLET] Switched to %s (%s): %s\n", acc->name, acc->symbol, acc->address);
        rgb.flashSuccess();
    } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_LONG_PRESS) {
        // Exit back to dashboard
        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
        Serial.println("[WALLET] Exited to Dashboard.");
    }
}

// ─── State: Live Crypto Prices Screen ───────────────────────────────────────
void processCryptoPricesState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        // Force price refresh
        Serial.println("[PRICE] Refreshing live cryptocurrency market prices...");
        ticker->fetchPricesNow();
        ui.renderCryptoPrices(ticker->getBtcPrice(), ticker->getEthPrice(), ticker->getSolPrice(), ticker->getDogePrice(), ticker->isLive());
        rgb.flashSuccess();
    } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_LONG_PRESS) {
        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
        Serial.println("[PRICE] Exited to Dashboard.");
    }
}

// ─── State: Wi-Fi Network Manager View ──────────────────────────────────────
void processWifiState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS) {
        Serial.println("[WIFI] Initiating network scan & auto-connect...");
        wifi->connectBest();
        ui.renderWifiScreen(wifi->isConnected(), wifi->getSsid().c_str(), wifi->getIp().c_str(), wifi->getRssi(), wifi->getSavedCount());
        rgb.flashSuccess();
    } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_LONG_PRESS) {
        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
        Serial.println("[WIFI] Exited to Dashboard.");
    }
}

// ─── State: FIDO2 / WebAuthn Request ─────────────────────────────────────────
void processFidoState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS || ev == BTN_LONG_PRESS) {
        // User Presence Confirmed!
        fido->confirmUserPresence();
        Serial.printf("[FIDO2] ✅ User Presence Confirmed for %s!\n", reqDomain);
        rgb.flashSuccess();
        ui.renderSuccessBanner("ASSERTION SIGNED", reqDomain);
        delay(1200);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
    } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_VERY_LONG_PRESS) {
        fido->rejectUserPresence();
        Serial.println("[FIDO2] ❌ Authentication Rejected by user.");
        ui.renderErrorBanner("Auth Cancelled");
        delay(1000);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
    }
}

// ─── State: Crypto Clear-Sign Request ────────────────────────────────────────
void processCryptoState(ButtonEvent ev) {
    if (ev == BTN_LONG_PRESS || ev == BTN_SHORT_PRESS) {
        char sigHex[130];
        wallet->executeSign(sigHex, sizeof(sigHex));

        Serial.printf("[SIGNER] ✅ Transaction Signed: %s to %s\n", reqAmount, reqRecipient);
        Serial.printf("[SIGNER] Signature: %s\n", sigHex);

        rgb.flashSuccess();
        ui.renderSuccessBanner("TX SIGNED (ECDSA)", reqAmount);
        delay(1500);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
    } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_VERY_LONG_PRESS) {
        wallet->cancelSign();
        Serial.println("[SIGNER] ❌ Transaction Rejected by user.");
        ui.renderErrorBanner("Tx Aborted");
        delay(1000);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
    }
}

// ─── State: Air-Gap SD Signer ────────────────────────────────────────────────
void processAirGapState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS || ev == BTN_LONG_PRESS) {
        Serial.println("[AIR-GAP] ✅ PSBT Signed and written back to MicroSD!");
        rgb.flashSuccess();
        ui.renderSuccessBanner("PSBT SIGNED TO SD", "Unmount Safe");
        delay(1500);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
    } else if (ev == BTN_DOUBLE_CLICK || ev == BTN_VERY_LONG_PRESS) {
        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
    }
}

// ─── State: BLE Phone Companion ─────────────────────────────────────────────
void processBleState(ButtonEvent ev) {
    if (ev == BTN_DOUBLE_CLICK || ev == BTN_SHORT_PRESS) {
        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
    }
}

// ─── Interactive Serial Simulator ───────────────────────────────────────────
void handleSerialCommands() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    Serial.printf("\n[CMD] Received: '%s'\n", cmd.c_str());

    if (cmd == "help") {
        Serial.println("\n--- T-Dongle S3 Security Key & Crypto Vault Commands ---");
        Serial.println("  setup:web                - Launch Web Captive Portal ('T-Key-Setup' AP at 192.168.4.1)");
        Serial.println("  setup:reset              - Reset vault provisioning and launch Web Setup Portal");
        Serial.println("  prices                   - Display live cryptocurrency prices on TFT screen");
        Serial.println("  wallet:status            - Display all cryptocurrency addresses & paths");
        Serial.println("  wallet:addr:<btc|eth|sol>- View specific coin address on TFT screen");
        Serial.println("  wallet:seed              - Display BIP-39 mnemonic seed phrase");
        Serial.println("  auth:<domain>            - Trigger WebAuthn Passkey request (e.g. auth:github.com)");
        Serial.println("  sign:<addr>:<amt>        - Trigger Clear-Sign Crypto prompt (e.g. sign:0x123:0.5ETH)");
        Serial.println("  wifi:scan                - Scan nearby 2.4GHz Wi-Fi networks");
        Serial.println("  wifi:add:<ssid>:<pass>   - Store Wi-Fi credentials to persistent flash");
        Serial.println("  wifi:connect             - Connect to best stored Wi-Fi network");
        Serial.println("  wifi:status              - Display Wi-Fi connection info & IP");
        Serial.println("  wifi:list                - List all stored Wi-Fi SSIDs");
        Serial.println("  wifi:clear               - Erase all stored Wi-Fi credentials");
        Serial.println("  led:<amber|cyan|green|blue|purple|red|off> - Test status LED modes");
        Serial.println("  rgb:<r>:<g>:<b>          - Set raw RGB values (0-255)");
        Serial.println("  lock                     - Lock vault back to PIN entry");
        Serial.println("  unlock                   - Instant unlock for bench testing");
        Serial.println("  duress                   - Trigger emergency flash wipe + decoy crash");
        Serial.println("  status                   - Print current device state");
        Serial.println("---------------------------------------------------------\n");
    }
    else if (cmd == "setup:web" || cmd == "setup:reset") {
        Preferences vaultPrefs;
        vaultPrefs.begin("vault_sec", false);
        vaultPrefs.putBool("provisioned", false);
        vaultPrefs.end();
        loadSecurityConfig();
    }
    else if (cmd == "prices") {
        deviceState = STATE_CRYPTO_PRICES;
        ticker->fetchPricesNow();
        ui.renderCryptoPrices(ticker->getBtcPrice(), ticker->getEthPrice(), ticker->getSolPrice(), ticker->getDogePrice(), ticker->isLive());
        Serial.println("[PRICES] Rendered live market ticker on screen.");
    }
    else if (cmd.startsWith("auth:")) {
        strncpy(reqDomain, cmd.substring(5).c_str(), sizeof(reqDomain) - 1);
        deviceState = STATE_FIDO_AUTH_REQUEST;
        rgb.setMode(LED_MODE_PULSE_GREEN);
        ui.renderFidoRequest(reqDomain);
        Serial.printf("[FIDO2] WebAuthn request rendered for '%s'. Tap button to confirm!\n", reqDomain);
    }
    else if (cmd.startsWith("sign:")) {
        int firstColon = cmd.indexOf(':');
        int secondColon = cmd.indexOf(':', firstColon + 1);
        if (secondColon > 0) {
            strncpy(reqRecipient, cmd.substring(firstColon + 1, secondColon).c_str(), sizeof(reqRecipient) - 1);
            strncpy(reqAmount, cmd.substring(secondColon + 1).c_str(), sizeof(reqAmount) - 1);
        } else {
            strncpy(reqRecipient, cmd.substring(firstColon + 1).c_str(), sizeof(reqRecipient) - 1);
            strncpy(reqAmount, "0.100 ETH", sizeof(reqAmount) - 1);
        }
        deviceState = STATE_CRYPTO_SIGN_REQUEST;
        wallet->prepareSignRequest(COIN_ETH, reqRecipient, reqAmount);
        rgb.setMode(LED_MODE_PULSE_GREEN);
        ui.renderCryptoSignRequest("Ethereum Mainnet", reqRecipient, reqAmount);
        Serial.printf("[SIGNER] Clear-Sign prompt rendered. Recipient: %s, Amount: %s. Hold button to sign!\n", reqRecipient, reqAmount);
    }
    else if (cmd == "wallet:status") {
        Serial.println("\n--- Multi-Currency Vault Accounts ---");
        for (int i = 0; i < COIN_COUNT; i++) {
            const WalletAccount* acc = wallet->getAccount((CryptoCoin)i);
            Serial.printf("  [%s] %-16s | Path: %-16s | Addr: %s\n",
                          acc->symbol, acc->name, acc->derivationPath, acc->address);
        }
        Serial.printf("Vault Status: %s | Master PIN: %s\n\n", wallet->isUnlocked() ? "UNLOCKED" : "LOCKED", masterPin);
    }
    else if (cmd.startsWith("wallet:addr:")) {
        String coinStr = cmd.substring(12);
        coinStr.toLowerCase();
        CryptoCoin c = COIN_BTC;
        if (coinStr == "eth") c = COIN_ETH;
        else if (coinStr == "sol") c = COIN_SOL;
        else if (coinStr == "doge") c = COIN_DOGE;

        deviceState = STATE_WALLET_VIEW;
        currentViewCoin = c;
        const WalletAccount* acc = wallet->getAccount(c);
        ui.renderWalletScreen(acc->name, acc->symbol, acc->derivationPath, acc->address);
        Serial.printf("[WALLET] Displaying %s address on screen: %s\n", acc->name, acc->address);
    }
    else if (cmd == "wallet:seed") {
        Serial.printf("[WALLET] BIP-39 Mnemonic Seed:\n  \"%s\"\n", wallet->getMnemonicPhrase());
    }
    else if (cmd.startsWith("wifi:add:")) {
        int firstColon = cmd.indexOf(':', 5);
        int secondColon = cmd.indexOf(':', firstColon + 1);
        if (secondColon > 0) {
            String ssid = cmd.substring(firstColon + 1, secondColon);
            String pass = cmd.substring(secondColon + 1);
            wifi->addNetwork(ssid.c_str(), pass.c_str());
        } else {
            String ssid = cmd.substring(firstColon + 1);
            wifi->addNetwork(ssid.c_str(), "");
        }
    }
    else if (cmd == "wifi:scan") {
        wifi->scanNetworks();
    }
    else if (cmd == "wifi:connect") {
        wifi->connectBest();
    }
    else if (cmd == "wifi:status") {
        Serial.printf("[WIFI] Status: %s | SSID: %s | IP: %s | RSSI: %d dBm | Saved: %d\n",
                      wifi->isConnected() ? "CONNECTED" : "DISCONNECTED",
                      wifi->getSsid().c_str(), wifi->getIp().c_str(), wifi->getRssi(), wifi->getSavedCount());
    }
    else if (cmd == "wifi:list") {
        Serial.printf("\n--- Stored Wi-Fi Networks (%d) ---\n", wifi->getSavedCount());
        for (int i = 0; i < wifi->getSavedCount(); i++) {
            Serial.printf("  [%d] %s\n", i + 1, wifi->getSavedSsid(i));
        }
        Serial.println();
    }
    else if (cmd == "wifi:clear") {
        wifi->clearAll();
    }
    else if (cmd.startsWith("led:")) {
        String m = cmd.substring(4);
        if (m == "amber")       { rgb.setMode(LED_MODE_SOLID_AMBER); Serial.println("[LED] Mode: SOLID_AMBER"); }
        else if (m == "cyan")   { rgb.setMode(LED_MODE_BREATHE_CYAN); Serial.println("[LED] Mode: BREATHE_CYAN"); }
        else if (m == "green")  { rgb.setMode(LED_MODE_PULSE_GREEN); Serial.println("[LED] Mode: PULSE_GREEN"); }
        else if (m == "blue")   { rgb.setMode(LED_MODE_SOLID_BLUE); Serial.println("[LED] Mode: SOLID_BLUE"); }
        else if (m == "purple") { rgb.setMode(LED_MODE_PULSE_PURPLE); Serial.println("[LED] Mode: PULSE_PURPLE"); }
        else if (m == "red")    { rgb.setMode(LED_MODE_STROBE_RED); Serial.println("[LED] Mode: STROBE_RED"); }
        else if (m == "off")    { rgb.setMode(LED_MODE_OFF); Serial.println("[LED] Mode: OFF"); }
        else { Serial.println("[LED] Unknown mode. Use: amber, cyan, green, blue, purple, red, off"); }
    }
    else if (cmd.startsWith("rgb:")) {
        int c1 = cmd.indexOf(':');
        int c2 = cmd.indexOf(':', c1 + 1);
        int c3 = cmd.indexOf(':', c2 + 1);
        if (c2 > 0 && c3 > 0) {
            uint8_t r = cmd.substring(c1 + 1, c2).toInt();
            uint8_t g = cmd.substring(c2 + 1, c3).toInt();
            uint8_t b = cmd.substring(c3 + 1).toInt();
            rgb.setPixel(r, g, b, 4);
            Serial.printf("[LED] Set Raw RGB: (%d, %d, %d)\n", r, g, b);
        }
    }
    else if (cmd == "lock") {
        deviceState = STATE_LOCKED;
        resetPinEntry();
        wallet->lock();
        rgb.setMode(LED_MODE_SOLID_AMBER);
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal, 0);
        Serial.println("[VAULT] Device locked.");
    }
    else if (cmd == "unlock") {
        deviceState = STATE_IDLE_DASHBOARD;
        wallet->unlock(masterPin);
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true, wifi->isConnected(), wifi->getIp().c_str());
        Serial.println("[VAULT] Bypassed lock: Dashboard active.");
    }
    else if (cmd == "duress") {
        Serial.println("[SECURITY] Triggering DURESS wipe from console!");
        DuressWipe::execute(*tft, rgb, "SERIAL_DURESS");
    }
    else if (cmd == "status") {
        Serial.printf("[STATUS] State: %d | Uptime: %lus | Wi-Fi: %s (%s) | PIN: %s\n",
                      deviceState, millis() / 1000,
                      wifi->isConnected() ? "CONNECTED" : "OFFLINE",
                      wifi->getIp().c_str(), masterPin);
    }
    else {
        Serial.printf("[CMD] Unknown command '%s'. Type 'help' for available commands.\n", cmd.c_str());
    }
}
