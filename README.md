# ⚡ T-Dongle S3 Security Key & Crypto Vault (007 Edition) 🛡️

> **"A James Bond-style hardware security key, WebAuthn authenticator, and crypto clear-signer on a $15 thumb drive."**

---

## 📌 Architectural Overview

This firmware turns the **LilyGo T-Dongle S3** into an all-in-one personal hardware security device:
- **Instant WebAuthn / Passkeys:** Authenticates GitHub, Google, Bitwarden, etc. Displays the origin domain on the 0.96" TFT and requires a physical button press to confirm User Presence (UP).
- **Crypto Clear-Signer (WYSIWYS):** Prevents blind-signing attacks by rendering the recipient address, network, and transfer amount on-screen before cryptographic signing.
- **Single-Button Cadence Input:** Full PIN entry and menu navigation using Morse-style cadence (Short press: +1 / Next, Long press: Confirm / OK, Double click: Backspace / Cancel).
- **Emergency Duress Self-Destruct:** Entering the alternate Duress PIN (`9999`) or holding the physical button for >6 seconds immediately zeroizes flash memory, erases NVS keystore partitions, and triggers a decoy low-level kernel panic screen.
- **WS2812 Status Beacon:** Stealth visual feedback (Amber = Locked, Breathing Cyan = Ready, Pulsing Green = Awaiting physical tap, Red Strobe = Wiped).
- **MicroSD Air-Gapped Signer:** Offline PSBT (Partially Signed Bitcoin Transactions) parsing and signing when powered via a portable battery pack.
- **BLE Companion Mode:** Mobile phone authentication and transaction approval over Bluetooth Low Energy.

---

## 📟 Hardware Pinout

| Component | Pin | Function |
| :--- | :--- | :--- |
| **TFT CS** | GPIO 4 | SPI Chip Select |
| **TFT DC** | GPIO 2 | Data / Command |
| **TFT RST** | GPIO 1 | Hardware Reset |
| **TFT MOSI** | GPIO 3 | SPI Master Out |
| **TFT SCLK** | GPIO 5 | SPI Clock |
| **TFT BL** | GPIO 38 | Backlight Enable (Active High) |
| **RGB LED** | GPIO 40 | WS2812 Single Pixel |
| **BOOT BTN** | GPIO 0 | Active Low (Internal Pull-up) |
| **SD CLK** | GPIO 12 | SD_MMC 1-Bit Clock |
| **SD CMD** | GPIO 16 | SD_MMC Command |
| **SD D0** | GPIO 17 | SD_MMC Data 0 |

---

## 🕹️ Single-Button Cadence Controls

| Gesture | Timing | Action |
| :--- | :--- | :--- |
| **Short Press (Tap)** | `< 650ms` | Increment PIN digit (`+1`) / Next option (Instant on release) |
| **Long Press (Hold)** | `650ms – 2200ms` | Confirm digit / Commit transaction / OK |
| **Very Long Press (Hold)** | `2200ms – 5500ms` | Backspace / Lock device / Cancel / Reject |
| **Panic Hold** | `> 5500ms` | **Instant Flash Zeroization & Decoy Crash** |

---

## 🚨 PIN & Duress System

- **Default Master PIN:** `1234` (Enters normal unlocked state & dashboard)
- **Emergency Duress PIN:** `9999` (Wipes NVS keystores, flashes red, shows decoy kernel panic)

---

## 💻 Interactive Serial Simulation Console

When connected via USB CDC, open the Serial Monitor (`115200` baud) or run `make monitor`:

```bash
# Available Commands
auth:github.com            # Trigger WebAuthn Passkey prompt for github.com
sign:0x71C...89E2:0.5ETH   # Trigger Clear-Sign crypto prompt
sd                         # Simulate MicroSD PSBT air-gapped transaction
ble                        # Switch to BLE phone companion mode
lock                       # Lock device back to PIN entry
unlock                     # Instant bench-test unlock
duress                     # Execute emergency panic wipe & decoy crash
status                     # Print device state & uptime
help                       # Display help menu
```

---

## 🚀 Build & Flash Commands

```bash
# Compile
make compile

# Flash to T-Dongle S3
make flash

# Open Serial Monitor
make monitor

# Show Pinout
make pins
```
