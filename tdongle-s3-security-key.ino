/**
 * tdongle-s3-security-key.ino
 * ==============================================================================
 * Project: T-Dongle S3 James Bond Hardware Security Key & Crypto Vault
 * Hardware: LilyGo T-Dongle S3 (ESP32-S3, ST7735 0.96" TFT, WS2812 RGB, microSD)
 * 
 * Features:
 *   - FIDO2 / WebAuthn Passkey Authenticator (Visual Origin Verification)
 *   - Crypto Hardware Vault (Clear-Signing WYSIWYS verification)
 *   - Single-Button Morse Cadence PIN Entry (Short / Long / Double / Panic)
 *   - Emergency Duress Self-Destruct (Flash scrub + Decoy crash screen)
 *   - Multi-Color WS2812 Status Beacon (Amber / Cyan / Green / Red)
 *   - MicroSD Air-Gapped PSBT Signer interface
 *   - Native USB CDC Interactive Simulation Console
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Adafruit_NeoPixel.h>

#include "src/config.h"
#include "src/button_cadence.h"
#include "src/rgb_status.h"
#include "src/ui_engine.h"
#include "src/duress_wipe.h"

// ─── Hardware Objects ────────────────────────────────────────────────────────
TFT_eSPI      tft = TFT_eSPI();
RgbStatus     rgb;
ButtonCadence btn;
UiEngine      ui;

// ─── Global State ────────────────────────────────────────────────────────────
DeviceState   deviceState = STATE_LOCKED;
char          pinDigits[PIN_LENGTH + 1] = "0000";
int           pinIndex = 0;
int           currentDigitVal = 0;
uint32_t      lastDashboardUpdate = 0;

// Temporary buffers for active requests
char reqDomain[48]     = "github.com";
char reqNetwork[24]    = "ETH Mainnet";
char reqRecipient[48]  = "0x71C...89E2";
char reqAmount[32]     = "0.250 ETH ($850)";
char reqFilename[32]   = "tx_cold_01.psbt";

// ─── Forward Declarations ───────────────────────────────────────────────────
void handleSerialCommands();
void processLockedState(ButtonEvent ev);
void processDashboardState(ButtonEvent ev);
void processFidoState(ButtonEvent ev);
void processCryptoState(ButtonEvent ev);
void processAirGapState(ButtonEvent ev);
void processBleState(ButtonEvent ev);
void resetPinEntry();

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Initialize Hardware
    btn.begin(PIN_BTN);
    rgb.begin(PIN_LED, LED_COUNT);
    ui.begin(&tft);

    // Initial state: Locked awaiting PIN
    deviceState = STATE_LOCKED;
    resetPinEntry();
    rgb.setMode(LED_MODE_SOLID_AMBER);
    ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal);

    Serial.println("\n========================================================");
    Serial.println(" ⚡ T-DONGLE-S3 SECURITY KEY & CRYPTO VAULT INITIALIZED");
    Serial.println("========================================================");
    Serial.println("Status: LOCKED. Enter PIN via button cadence (Default: 1234)");
    Serial.println("Emergency Duress PIN: 9999 (Flash zeroize + decoy halt)");
    Serial.println("Type 'help' in serial monitor for interactive commands.\n");
}

// ─── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
    // 1. Update Subsystems
    ButtonEvent ev = btn.update();
    rgb.update();
    handleSerialCommands();

    // 2. Global Panic Hold Check (> 6 seconds hold)
    if (ev == BTN_PANIC_HOLD) {
        Serial.println("[EMERGENCY] Physical panic hold detected!");
        DuressWipe::execute(tft, rgb, "PANIC_HOLD");
        return;
    }

    // 3. State Machine Dispatch
    switch (deviceState) {
        case STATE_LOCKED:
            processLockedState(ev);
            break;

        case STATE_IDLE_DASHBOARD:
            processDashboardState(ev);
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
            // Dead state, handled inside DuressWipe
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

// ─── State: Locked / PIN Entry ───────────────────────────────────────────────
void processLockedState(ButtonEvent ev) {
    if (ev == BTN_NONE) return;

    if (ev == BTN_SHORT_PRESS) {
        // Increment current digit (0-9)
        currentDigitVal = (currentDigitVal + 1) % 10;
        pinDigits[pinIndex] = '0' + currentDigitVal;
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal);
        Serial.printf("[PIN] Slot %d = %d\n", pinIndex + 1, currentDigitVal);
    } 
    else if (ev == BTN_LONG_PRESS) {
        // Confirm current digit
        pinDigits[pinIndex] = '0' + currentDigitVal;
        pinIndex++;

        if (pinIndex < PIN_LENGTH) {
            currentDigitVal = 0;
            ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal);
            Serial.printf("[PIN] Confirmed digit %d. Next slot.\n", pinIndex);
        } else {
            // All digits entered — Validate PIN!
            pinDigits[PIN_LENGTH] = '\0';
            Serial.printf("[PIN] Validating entry: %s\n", pinDigits);

            if (strcmp(pinDigits, EMERGENCY_DURESS_PIN) == 0) {
                // Emergency Duress PIN triggered!
                DuressWipe::execute(tft, rgb, "DURESS_PIN");
            } else if (strcmp(pinDigits, DEFAULT_MASTER_PIN) == 0) {
                // Unlock Success!
                Serial.println("[VAULT] Master PIN Accepted! Device Unlocked.");
                rgb.flashSuccess();
                ui.renderSuccessBanner("KEY UNLOCKED", "Vault Active");
                delay(1200);

                deviceState = STATE_IDLE_DASHBOARD;
                rgb.setMode(LED_MODE_BREATHE_CYAN);
                ui.renderDashboard(millis() / 1000, true, true);
                lastDashboardUpdate = millis();
            } else {
                // Invalid PIN
                Serial.println("[AUTH] Invalid PIN entered!");
                ui.renderErrorBanner("Wrong PIN Code");
                rgb.setMode(LED_MODE_STROBE_RED);
                delay(1500);
                resetPinEntry();
                rgb.setMode(LED_MODE_SOLID_AMBER);
                ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal);
            }
        }
    } 
    else if (ev == BTN_DOUBLE_CLICK) {
        // Backspace
        if (pinIndex > 0) {
            pinIndex--;
            currentDigitVal = pinDigits[pinIndex] - '0';
            ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal);
            Serial.println("[PIN] Backspace to previous slot.");
        }
    }
}

// ─── State: Idle Dashboard ───────────────────────────────────────────────────
void processDashboardState(ButtonEvent ev) {
    // Refresh uptime every 3 seconds
    if (millis() - lastDashboardUpdate > 3000) {
        lastDashboardUpdate = millis();
        ui.renderDashboard(millis() / 1000, true, true);
    }

    if (ev == BTN_LONG_PRESS) {
        // Lock device
        Serial.println("[VAULT] Device Locked by user.");
        deviceState = STATE_LOCKED;
        resetPinEntry();
        rgb.setMode(LED_MODE_SOLID_AMBER);
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal);
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Toggle view demo: Air-Gap SD
        Serial.println("[MODE] Switched to Air-Gap SD check.");
        deviceState = STATE_AIRGAP_SD;
        rgb.setMode(LED_MODE_SOLID_BLUE);
        ui.renderAirGapScreen("cold_tx.psbt", "0.150 BTC -> bc1q...");
    }
}

// ─── State: FIDO2 / WebAuthn Request ─────────────────────────────────────────
void processFidoState(ButtonEvent ev) {
    if (ev == BTN_SHORT_PRESS || ev == BTN_LONG_PRESS) {
        // User Presence Confirmed!
        Serial.printf("[FIDO2] ✅ User Presence Confirmed for %s!\n", reqDomain);
        rgb.flashSuccess();
        ui.renderSuccessBanner("ASSERTION SIGNED", reqDomain);
        delay(1200);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true);
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Rejected
        Serial.println("[FIDO2] ❌ Authentication Rejected by user.");
        ui.renderErrorBanner("Auth Cancelled");
        delay(1000);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true);
    }
}

// ─── State: Crypto Clear-Sign Request ────────────────────────────────────────
void processCryptoState(ButtonEvent ev) {
    if (ev == BTN_LONG_PRESS) {
        // Confirmed & Signed
        Serial.printf("[SIGNER] ✅ Transaction Signed: %s to %s\n", reqAmount, reqRecipient);
        rgb.flashSuccess();
        ui.renderSuccessBanner("TX SIGNED & BROADCAST", reqAmount);
        delay(1500);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true);
    } else if (ev == BTN_DOUBLE_CLICK) {
        // Rejected
        Serial.println("[SIGNER] ❌ Transaction Rejected by user.");
        ui.renderErrorBanner("Tx Aborted");
        delay(1000);

        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true);
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
        ui.renderDashboard(millis() / 1000, true, true);
    } else if (ev == BTN_DOUBLE_CLICK) {
        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true);
    }
}

// ─── State: BLE Phone Companion ─────────────────────────────────────────────
void processBleState(ButtonEvent ev) {
    if (ev == BTN_DOUBLE_CLICK) {
        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true);
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
        Serial.println("\n--- Interactive Security Key Commands ---");
        Serial.println("  auth:<domain>            - Trigger WebAuthn Passkey request (e.g. auth:github.com)");
        Serial.println("  sign:<addr>:<amt>        - Trigger Clear-Sign Crypto prompt (e.g. sign:0x123:0.5ETH)");
        Serial.println("  sd                       - Simulate inserting MicroSD with PSBT transaction");
        Serial.println("  ble                      - Switch to BLE phone pairing mode");
        Serial.println("  lock                     - Lock device back to PIN entry");
        Serial.println("  unlock                   - Instant unlock for bench testing");
        Serial.println("  duress                   - Trigger emergency flash wipe + decoy crash");
        Serial.println("  status                   - Print current device state");
        Serial.println("-----------------------------------------\n");
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
        rgb.setMode(LED_MODE_PULSE_GREEN);
        ui.renderCryptoSignRequest("ETH Mainnet", reqRecipient, reqAmount);
        Serial.printf("[SIGNER] Clear-Sign prompt rendered. Recipient: %s, Amount: %s. Hold button to sign!\n", reqRecipient, reqAmount);
    }
    else if (cmd == "sd") {
        deviceState = STATE_AIRGAP_SD;
        rgb.setMode(LED_MODE_SOLID_BLUE);
        ui.renderAirGapScreen("signed_batch.psbt", "0.250 BTC SegWit");
        Serial.println("[AIR-GAP] MicroSD card mode active. Tap button to sign PSBT.");
    }
    else if (cmd == "ble") {
        deviceState = STATE_BLE_COMPANION;
        rgb.setMode(LED_MODE_PULSE_PURPLE);
        ui.renderBleScreen("T-KEY-S3-BOND", false);
        Serial.println("[BLE] Phone Companion mode active.");
    }
    else if (cmd == "lock") {
        deviceState = STATE_LOCKED;
        resetPinEntry();
        rgb.setMode(LED_MODE_SOLID_AMBER);
        ui.renderPinScreen(pinDigits, pinIndex, currentDigitVal);
        Serial.println("[VAULT] Device locked.");
    }
    else if (cmd == "unlock") {
        deviceState = STATE_IDLE_DASHBOARD;
        rgb.setMode(LED_MODE_BREATHE_CYAN);
        ui.renderDashboard(millis() / 1000, true, true);
        Serial.println("[VAULT] Bypassed lock: Dashboard active.");
    }
    else if (cmd == "duress") {
        Serial.println("[SECURITY] Triggering DURESS wipe from console!");
        DuressWipe::execute(tft, rgb, "SERIAL_DURESS");
    }
    else if (cmd == "status") {
        Serial.printf("[STATUS] Device State: %d, Uptime: %lus\n", deviceState, millis() / 1000);
    }
    else {
        Serial.printf("[CMD] Unknown command '%s'. Type 'help' for available commands.\n", cmd.c_str());
    }
}
