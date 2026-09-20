# ⚡ Crypto TKey S3 — Hardware Security Key & Offline Crypto Vault 🛡️

**Crypto TKey S3** (formerly `tdongle-s3-security-key`) is a James Bond-style FIDO2/WebAuthn hardware security key, offline cryptocurrency transaction clear-signer, and air-gapped vault powered by the **LilyGo T-Dongle S3** (`ESP32-S3`).

---

## 🔐 Core Capabilities

- **FIDO2 / WebAuthn Passkeys:** True CTAP2 over USB HID (`0xF1D0`) for Google, GitHub, Bitwarden, and Linux PAM. Displays relying party origin on the 0.96" TFT LCD and requires physical touch (User Presence) to approve.
- **Dynamic Mode-Switched USB:**
  - *Normal Plug-in:* Pure FIDO2 CTAPHID (stealth, zero serial ports exposed).
  - *Boot-Hold Plug-in:* Composite CTAPHID + CDC Serial console on `/dev/ttyACM0` for live diagnostics.
- **Tiered Security Lifecycle:**
  - *Tier 1 (Web Logins):* Ready state (Cyan LED) with 1-tap touch (no PIN fatigue).
  - *Tier 2 (Crypto & UV):* Master PIN gate (Amber LED) unlocking a 3-minute signing window.
- **Crypto Clear-Signer (WYSIWYS):** What You See Is What You Sign on Bitcoin, Ethereum, and Solana.
- **Air-Gapped MicroSD Signer:** Offline PSBT parsing and signing with zero RF radiation.
- **Smart Air-Gap Ticker:** 0 RF on PC (host-streamed prices); Wi-Fi burst mode exclusively when plugged into wall power.
- **Tri-Level Coercion & Anti-Tamper:**
  - *Level 1:* Plausible deniability decoy PIN (`8888`) unlocking secondary decoy wallet.
  - *Level 2:* Panic hold (>5.5s) flash scrub + authentic Guru Meditation crash decoy.
  - *Level 3:* Anti-hammering auto-nuke after 10 failed PIN attempts.

---

## 📟 Hardware Pinout

| Component | Pin | Function |
| :--- | :--- | :--- |
| **TFT CS** | GPIO 4 | SPI Chip Select (`SPI3_HOST`) |
| **TFT DC** | GPIO 2 | Data / Command |
| **TFT RST** | GPIO 1 | Hardware Reset |
| **TFT MOSI** | GPIO 3 | SPI Master Out |
| **TFT SCLK** | GPIO 5 | SPI Clock |
| **TFT BL** | GPIO 38 | Backlight Enable (**Active LOW**) |
| **RGB LED** | GPIO 40 | WS2812 Single Pixel |
| **BOOT BTN** | GPIO 0 | Active LOW User Input |
| **SD CLK** | GPIO 12 | SD_MMC 1-Bit Clock |
| **SD CMD** | GPIO 16 | SD_MMC Command |
| **SD D0** | GPIO 17 | SD_MMC Data 0 |

---

## 🕹️ Single-Button Morse Cadence Controls

| Gesture | Timing | Action |
| :--- | :--- | :--- |
| **Short Tap** | < 650ms | Increment PIN digit (+1) / Next option |
| **Long Press** | 650ms – 2200ms | Confirm digit / Commit transaction / Approve UP |
| **Extended Hold** | 2200ms – 5500ms | Backspace / Cancel / Lock device |
| **Panic Hold** | > 5500ms | Instant Flash Scrub & Guru Meditation Decoy Crash |

---

## 🚀 Build & Flash Commands

```bash
# Compile
make compile

# Flash to T-Dongle S3
make flash

# Open Serial Monitor (when diagnostic boot is active)
make monitor

# Show Hardware Pinout
make pins
```
