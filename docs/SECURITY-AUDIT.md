# Crypto T-Key S3 Security Audit

**Review type:** repository-level defensive audit  
**Review basis:** source inspection of the current `main` tree  
**Status:** remediation required before production use  
**Date:** 2026-09-24

## Executive summary

The repository contains useful security boundaries—physical user presence, production-only restrictions around serial signing, constant-time PIN-hash comparison, AES-GCM authentication for MicroSD containers, and a dry-run default for eFuse tooling. Those controls reduce risk, but they do not make the device a certified hardware wallet or security key.

The highest-priority concerns are password/key derivation strength, unauthenticated setup-portal exposure, plaintext Wi-Fi credential storage, incomplete secure deletion semantics, and insufficient validation around irreversible eFuse operations.

## Findings

### High — Setup portal authentication is not hardened

The setup password is represented as a plain SHA-256 digest and the SoftAP is an unauthenticated local HTTP service during setup. There is no visible rate limit, lockout, session expiry, CSRF protection, or transport authentication. A nearby attacker who can reach the portal can brute-force weak passwords or abuse configuration endpoints.

**Recommended remediation:** use a memory-hard password KDF where feasible, enforce a long random setup secret, add login throttling and lockout, expire authenticated sessions, validate request origin/state, and clearly require physical presence before enabling configuration changes.

### High — Wi-Fi credentials are stored directly in NVS

`WifiManager` stores SSIDs and passwords in the `wifi_cfg` NVS namespace. Device flash access, firmware compromise, or an unsafe debug configuration can expose them.

**Recommended remediation:** encrypt credentials under a device-bound key, minimize retention, provide an explicit erase operation, and document that Wi-Fi provisioning is not appropriate for hostile environments.

### High — Vault KDF work factor is outdated

The MicroSD vault derives its AES key with PBKDF2-HMAC-SHA256 using 10,000 iterations. This is a weak offline-guessing cost for a short numeric PIN, even though the key is device-bound.

**Recommended remediation:** migrate to a calibrated work factor or an embedded-friendly memory-hard KDF, version the container format, and provide an explicit migration path so existing backups remain recoverable. Do not silently change the value without a format/version migration.

### Medium — Encrypted-container authentication coverage is narrow

The vault authenticates the magic value as AAD, but the header fields such as version, flags, and payload length are not all bound as authenticated metadata. Malformed or manipulated metadata therefore deserves additional validation before allocation and decryption.

**Recommended remediation:** authenticate a canonical serialized header, validate version, flags, file size, and maximum payload length, and reject trailing or truncated data.

### Medium — Secure deletion is not guaranteed on MicroSD

The wipe routine writes a fixed amount of zero data before deleting files. Flash translation layers and wear-leveling mean overwrite-and-delete cannot guarantee physical erasure from removable media.

**Recommended remediation:** describe this as logical deletion only, destroy the encryption key first, use a bounded file-size overwrite where useful, and require destruction or controlled reformatting of the card for high-assurance disposal.

### Medium — eFuse tool needs stronger production gates

The tool correctly warns that eFuse operations are irreversible and defaults to simulation, but the burn path does not appear to verify every subprocess return code or validate the full Secure Boot/Flash Encryption provisioning sequence. The simulation text also describes protections beyond what the burn path actually performs.

**Recommended remediation:** make the dry-run mode explicit, require a signed build manifest and device identity confirmation, stop immediately on any failed command, print a post-burn summary, and keep the simulated plan exactly aligned with the commands executed.

### Medium — Release reproducibility is not established

The repository does not currently pin all Arduino core/library versions, publish a lockfile, or provide a CI build that records the toolchain and artifact digest.

**Recommended remediation:** pin the board package and libraries, add a reproducible compile workflow, archive build metadata, and publish checksums for release artifacts.

### Low — Repository licensing and release status are unclear

No license is declared and there are no security release tags. Users may incorrectly assume production support or redistribution rights.

**Recommended remediation:** add an explicit license decision, versioned releases, a changelog, and a security-status statement to every release.

## Existing positive controls

- Physical-button user presence is required for production signing paths.
- PIN comparisons use a constant-time comparison helper.
- PIN hashes and legacy plaintext PINs are migrated away from the old storage field.
- AES-256-GCM includes an authentication tag for vault payloads.
- Sensitive temporary buffers are explicitly zeroized in several paths.
- eFuse tooling has a prominent irreversible-operation warning and dry-run behavior.
- Wi-Fi is normally disabled while a USB host is present.

These controls still require hardware tests, code review, and adversarial testing.

## Upgrade gates

Before a production claim, require:

- clean build from a pinned toolchain;
- unit and negative tests for CBOR, CTAP2, PSBT, EVM parsing, PIN lockout, and vault corruption;
- fuzzing of all host- and MicroSD-controlled parsers;
- verification that secrets never enter serial logs or host-side telemetry;
- physical recovery testing before any eFuse burn;
- independent review of signing display logic and address derivation;
- documented backup, restore, and lost-device procedures.

## Scope limitation

This document is a source-level audit, not a penetration test, cryptographic proof, hardware fault-injection assessment, or certification. Findings should be revalidated after firmware changes.
