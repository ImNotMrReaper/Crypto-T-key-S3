# Crypto T-Key S3

**Crypto T-Key S3** is an ESP32-S3 hardware security key and offline cryptocurrency vault for the LilyGo T-Dongle S3.

> **Security status:** This is security-sensitive embedded software. Treat it as experimental until you have independently reviewed the source, built the firmware reproducibly, and tested it on hardware. Never use it to protect funds or credentials you cannot afford to lose.

## What it does

- FIDO2/WebAuthn and CTAP2 authentication over USB HID.
- Physical user-presence confirmation for passkey operations.
- On-device PIN-gated cryptocurrency wallet workflows.
- BIP-39 seed generation and on-device verification.
- Bitcoin PSBT parsing and signing from MicroSD.
- EVM transaction decoding and clear-signing prompts.
- Encrypted MicroSD vault containers using AES-256-GCM.
- Duress and panic zeroization workflows.
- Low-power, USB-aware Wi-Fi bursts for optional portfolio data.

## What was upgraded in this repository

- Unified project branding under **Crypto T-Key S3**.
- Added a security policy and responsible-disclosure guidance.
- Added a documented security audit with prioritized findings, limitations, and release gates.
- Documented the trust boundaries, threat model, secure-build expectations, and operational warnings.
- Clarified that the firmware is not independently certified and that the eFuse tool is irreversible.
- Improved build and flashing documentation so production and development workflows are easier to distinguish.

## Repository layout

- `crypto-tkey-s3.ino` — firmware entry point and device state machine.
- `src/` — FIDO2, wallet, PIN, vault, display, Wi-Fi, and signing subsystems.
- `host/` — host integration files and udev rules.
- `tools/` — hardware tests, companion utilities, and production tooling.
- `arduino_forge.py` — compile, flash, monitor, device detection, and pin-map CLI.
- `docs/SECURITY-AUDIT.md` — security review and remediation plan.
- `SECURITY.md` — supported versions and vulnerability-reporting policy.

## Build and flash

Install Arduino CLI, the ESP32 Arduino core, and the libraries required by the source tree. Then run:

```bash
# Compile only; preferred first step for every change
make compile

# Detect a connected T-Dongle S3
make detect

# Show the board pin map
make pins

# Flash a development device
make flash

# Open the diagnostic monitor
make monitor
```

Do not burn production eFuses during development. The eFuse tool is intentionally separate and must be reviewed against the exact bootloader, partition table, signing keys, and recovery process for the device being secured.

## Security-critical operating rules

1. Provision a strong, unique master PIN and setup password; do not use example values.
2. Verify transaction recipient, network, amount, and fee on the physical display before signing.
3. Keep recovery words offline and never paste them into an issue, chat, serial log, browser, or host script.
4. Treat a connected computer and MicroSD card as untrusted inputs.
5. Use a dedicated test device until independent review and hardware testing are complete.
6. Make and test recovery backups before enabling any irreversible hardware hardening.
7. Review `docs/SECURITY-AUDIT.md` before describing a build as production-ready.

## Project identity

- Display name: **Crypto T-Key S3**
- Suggested GitHub slug: `Crypto-T-key-S3`
- Hardware: LilyGo T-Dongle S3 / ESP32-S3
- Primary language: C++

## License

No license is currently declared. Until a license is added, normal copyright restrictions apply; do not assume that the code may be redistributed or used commercially.
