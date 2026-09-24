# Crypto T-Key S3 — Security Audit

| | |
| :--- | :--- |
| **Scope** | Firmware (`crypto-tkey-s3.ino`, `src/`), host integration (`host/`), tooling (`tools/`) |
| **Method** | Source review, plus hardware testing with the suites in `tools/` (CTAP2, hmac-secret, wallet, UI soak) |
| **Revision** | 2026-09-24, revised and verified against the code (supersedes the first draft of the same date) |
| **Verdict** | Fine for experimenting and as a *second* authenticator. **Not** yet a safe sole key or cold wallet. |

This is a source-level review, not a penetration test, fault-injection assessment or certification.
Re-check the findings after any firmware change.

## Threat model

| Adversary | In scope | Notes |
| :--- | :--- | :--- |
| Malicious website / relying party | Yes | CTAP2 origin binding and the touch requirement |
| Malware on the host computer | Yes | It can ask for signatures but can't approve them; the screen shows what is being approved |
| Someone nearby over Wi-Fi | Yes | Setup portal SoftAP |
| Someone holding the dongle | Yes | Main open risk today (C1) |
| Lab attacker (decapping, glitching) | No | The ESP32-S3 isn't a secure element |

## Findings

| ID | Severity | Finding | Status |
| :--- | :--- | :--- | :--- |
| C1 | **Critical** | Secrets readable from flash | Open: needs the hardening roadmap below |
| C2 | **Critical** | eFuse tool would brick the device | **Fixed** (burn path disabled) |
| H1 | High | Setup portal is an open AP with weak authentication | Planned: portal rebuild |
| H2 | High | PIN hashes are only brute-force resistant on-device | Open: tied to C1 |
| M1 | Medium | Attestation private key is published in the source | Planned |
| M2 | Medium | Vault container header isn't fully authenticated | Open |
| M3 | Medium | MicroSD "wipe" is only a logical delete | Documented |
| M4 | Medium | Builds aren't reproducible or pinned | Open |
| L1 | Low | No license and no tagged releases | Open |

### C1 — Critical: secrets are readable from flash

Flash encryption and Secure Boot are not enabled. The ROM download mode can be reached with the BOOT button or the 1200-baud reset that `tools/flash_app.sh` uses, and in that mode `esptool read_flash` dumps the whole flash. That dump contains:

- `fido_vault/master_sec`: the root secret for every non-resident credential and for hmac-secret, in plaintext.
- `fido_rk/*`: resident credential private keys.
- `wallet_seed/dev_key` **and** `wallet_seed/seed`: the AES-256-GCM key sits next to the ciphertext it protects, so encrypting the seed at rest doesn't protect it against a dump.
- `wifi_cfg/*`: Wi-Fi SSIDs and passwords in plaintext.

**Impact:** anyone who holds the dongle for about a minute can clone every passkey and recover the wallet seed.

**Remediation** (in order; the first step needs no irreversible eFuse beyond one key block):
1. **Tie wallet encryption to the chip and the PIN.**
   1. Burn a random 256-bit key into an eFuse key block with purpose `HMAC_UP`. That block can't be read back.
   2. Derive the seed key as `HKDF(HMAC_efuse(PBKDF2(PIN, salt)), dev_key)`.

   A flash dump is then useless without the physical chip, and PIN guessing has to happen on-device, where the lockout applies. Apply the same wrapping to `master_sec`, and to resident keys, without the PIN.
2. **Flash encryption (release mode) and Secure Boot v2**, using an ESP-IDF–built bootloader. The bootloader generates the key on-chip and encrypts the firmware in place. Arduino's prebuilt bootloader can't do this; see C2.
3. **Disable ROM download mode** (`DIS_DOWNLOAD_MODE`, or `ENABLE_SECURITY_DOWNLOAD`) only after OTA updates over USB work, or the device can never be updated again.

### C2 — Critical (fixed): the eFuse tool would brick the device

`tools/burn_production_efuses.py --burn-now` burned `SPI_BOOT_CRYPT_CNT=1` directly, without:
- generating or writing a flash-encryption key, or
- encrypting the firmware.

On the next boot the ROM would "decrypt" plaintext flash and the device would never boot again. It also ignored `espefuse` return codes. Its simulation claimed Secure Boot and download-mode lockout, which the burn path never did.

**Fix:** the burn path is removed. The tool now only reads eFuses (`--summary`) and prints the correct provisioning sequence.

### H1 — High: setup portal

- The SoftAP `T-Key-Setup` has **no WPA2 password**, so anyone in range can join.
- The setup password is stored as an **unsalted SHA-256**, with no attempt limit or lockout, and the session never expires.
- The new PIN is written back into the page (`value='…'`) after a form error.

**Remediation (portal rebuild, next firmware):**
- A random WPA2 passphrase shown only on the device screen.
- A salted PBKDF2 setup password, migrated from the old hash.
- A per-session random cookie with an expiry, and a lockout after 5 failed attempts.
- No PIN or password echoed back into the page.
- A button press on the device required to apply changes.

### H2 — High: PIN hashes

The PIN (`pin_vault`) and the SD vault key both use PBKDF2-SHA256 with 10,000 iterations. On a 4–8 digit PIN, the entire 8-digit keyspace falls to a GPU in minutes, so the only real defence is keeping the hash off an attacker's machine. The 10-attempt lockout protects against on-device guessing only. Fixed by C1 step 1, after which the PBKDF2 cost matters much less.

### M1 — Medium: attestation key is published

`FIDO_ATTESTATION_PRIVKEY` in `src/ctap2.cpp` is in this public repository, so anyone can produce attestations that claim this model's AAGUID. User credentials aren't affected: each credential has its own key. Relying parties shouldn't treat this device's attestation as proof of genuine hardware. **Remediation:** switch to self attestation (packed with no `x5c`, signed by the credential key), or generate a per-device attestation key on first boot.

### M2 — Medium: vault container header

`sd_vault` uses only the magic value as GCM AAD; the version, flags and length aren't authenticated. **Remediation:** authenticate the whole serialized header; check version, flags and length against the file size before allocating memory; reject truncated files and trailing data.

### M3 — Medium: MicroSD deletion

Overwrite-then-delete can't guarantee erasure on flash media, because of wear levelling. Treat the vault wipe as a logical delete. Destroying the key (for example, the device being wiped) is what actually makes old containers unreadable.

### M4 — Medium: reproducibility

Neither the ESP32 core nor the libraries are pinned, and there's no CI build or published checksum. **Remediation:** pin versions (`sketch.yaml` profile), add a CI compile, and publish the SHA-256 of each release binary.

### L1 — Low: license and releases

There's no license, tagged release or changelog.

## Controls verified in place

- **User presence:** every CTAP2/U2F operation needs a physical press, with the relying party shown. The prompt is cancelled when the host stops polling.
- **Randomness:** the ESP32-S3 RNG is a PRNG unless RF is on, so every draw enables the bootloader entropy source (`bootloader_random_enable`) first.
- **Serial console:** production builds compile out serial touch, seed export, unlock and signing (`TKEY_TEST_SERIAL_TOUCH` is off).
- **PIN handling:**
  - salted PBKDF2 with a constant-time compare;
  - legacy plaintext PINs are migrated and erased;
  - duress PIN and a 10-attempt lockout;
  - FIDO client-PIN retries are tracked separately.
- **Wallet:**
  - the seed is decrypted into RAM only while unlocked and wiped on lock;
  - key material is zeroized with `mbedtls_platform_zeroize`;
  - receive addresses are cross-checked against `bip_utils` reference vectors.
- **Radio:** Wi-Fi is off whenever a USB host is attached and duty-cycled on wall power. HTTPS uses NTP time and the Mozilla CA bundle.
- **Host:** udev rules give HID access to the logged-in user (`uaccess`) and `plugdev`; the portfolio daemon runs as the user, not root.

## Release gates

Before calling any build production-ready:

- [ ] C1 steps 1–2 done and verified by dumping the flash of a test unit
- [ ] H1 portal rebuilt
- [ ] M1 attestation replaced
- [ ] Pinned toolchain, reproducible build, published checksum
- [ ] Fuzzing of the CBOR/CTAP2, PSBT, EVM and vault parsers
- [ ] Negative tests: PIN lockout, corrupted vault, malformed CTAP frames
- [ ] Recovery tested on a spare unit before any eFuse is burned
