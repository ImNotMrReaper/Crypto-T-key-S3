# Crypto T-Key S3

**A FIDO2 / WebAuthn security key and PIN-gated crypto wallet on a USB stick** — firmware for the
[LilyGo T-Dongle S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3) (ESP32-S3, 0.96" 160×80 TFT, RGB LED, one button, MicroSD).

> **Security status — experimental.** Flash encryption and Secure Boot are **not** enabled yet, so anyone
> holding the dongle can read its flash. Do not use it as the only key for accounts you can't recover, or for
> funds you can't afford to lose. Read [`docs/SECURITY-AUDIT.md`](docs/SECURITY-AUDIT.md) first.

## Features

**Security key**
- CTAP2.0 + U2F over USB HID. Works with Chrome, Firefox (incl. Snap), `ssh-keygen -t ecdsa-sk`, `pam_u2f`, and `fido2-token`.
- Resident / discoverable credentials (passkeys), FIDO client PIN (protocol 1), and packed self-attestation.
- The `hmac-secret` extension, so it can unlock LUKS volumes via `systemd-cryptenroll --fido2-device`.
- Every operation needs a physical button press, with the relying-party name shown on the screen. If the requesting host goes away, the prompt is cancelled.
- True-random keys: the hardware RNG entropy source is enabled around every draw.

**Wallet**
- On-device BIP-39 seed generation with a verification quiz, stored AES-256-GCM encrypted.
- The PIN is kept as a salted PBKDF2 hash. There's a 10-attempt lockout and a duress PIN.
- Receive addresses shown as QR codes: Bitcoin (bc1), Ethereum/EVM, Solana and Dogecoin.
- Offline Bitcoin PSBT signing from MicroSD, and clear-signing prompts for EVM transactions.
- Live portfolio: prices come from the host daemon while on USB, or from duty-cycled Wi-Fi bursts when on a wall charger. The radio is off whenever a computer is attached.

**Setup page**
- Hold the button while plugging in. The key opens a WPA2 Wi-Fi network whose password (and join QR) appears only on its screen.
- There you set the PIN, the optional duress PIN, the setup password, your coins, the home colour and LED effect, and Wi-Fi for wall power.

**Device**
- Single-button interface, 80 MHz idle clock, display sleep, and panic-hold zeroization.

## Controls

| Gesture | Timing | Typical action |
| :--- | :--- | :--- |
| Tap | < 600 ms | Next / +1 / approve a FIDO prompt |
| Double tap | two taps < 320 ms apart | Back / delete |
| Long press | 600 – 2200 ms | Confirm / enter PIN screen |
| Very long press | 2.2 – 6 s | Reset / receive QR on the price screen |
| Panic hold | > 6 s | Emergency zeroization |

The first press after the display sleeps only wakes it.

## Hardware map

| Function | GPIO | Notes |
| :--- | :--- | :--- |
| TFT CS / DC / RST | 4 / 2 / 1 | ST7735, SPI |
| TFT MOSI / SCLK | 3 / 5 | |
| TFT backlight | 38 | active **LOW** |
| RGB LED | 40 (data), 39 (clock) | APA102 |
| Button | 0 | BOOT, active LOW |
| MicroSD | CLK 12, CMD 16, D0–D3 14/17/21/18 | SD_MMC 4-bit |

USB: Espressif VID `0x303A`, composite HID (FIDO) + CDC serial.

## Repository layout

| Path | Contents |
| :--- | :--- |
| `crypto-tkey-s3.ino` | Entry point and device state machine |
| `src/` | CTAP2/CTAPHID, CBOR, P-256, FIDO store, PIN vault, BIP-32/39 wallet, PSBT/EVM, UI, LED, Wi-Fi, SD vault |
| `host/` | udev rules, `install_host.sh`, PAM helper |
| `tools/` | Flash script, hardware test suites, portfolio daemon, coin-catalog generator, eFuse inspector |
| `arduino_forge.py`, `Makefile` | Compile / flash / monitor CLI |

## Build and flash

Requires `arduino-cli` with the ESP32 core and the libraries used in `src/` (TFT_eSPI, Crypto, micro-ecc, ArduinoJson).

```bash
make compile                 # compile only
make flash                   # compile + upload (auto-detects the dongle)
make monitor                 # serial console
tools/flash_app.sh           # upload the app image via the 1200-baud bootloader reset
```

Host setup (udev rules for browsers, including Snap Firefox): `host/install_host.sh`.

### Tests

| Script | What it covers |
| :--- | :--- |
| `tools/test_fido2_hw.py` | CTAP2 makeCredential/getAssertion/PIN on real hardware |
| `tools/test_hmac_secret_hw.py` | hmac-secret outputs |
| `tools/test_webauthn_io.py` | webauthn.io end-to-end |
| `tools/test_wallet_hw.py` | Wallet derivation and signing (test firmware) |
| `tools/test_device_walkthrough.py` | Screen walkthrough, QR decode, soak (test firmware) |
| `tools/preview_portal.py` | Setup page in a desktop browser with a mock device API |

Test builds are compiled with `-DTKEY_TEST_SERIAL_TOUCH`; never leave one on a device you use.

## Security

- Policy and reporting: [`SECURITY.md`](SECURITY.md)
- Audit, known gaps and hardening roadmap: [`docs/SECURITY-AUDIT.md`](docs/SECURITY-AUDIT.md)

Operating rules:
1. Check the relying party, or the recipient, amount and network, on the device screen before pressing.
2. Keep your recovery words offline. Never type them into a computer, issue, chat or serial console.
3. Register a second authenticator on every account, so losing the dongle doesn't lock you out.
4. Don't run eFuse tooling until the hardening roadmap in the audit is complete; eFuses are irreversible.

## License

No license has been chosen yet, so default copyright applies. Please ask before reusing the code.
