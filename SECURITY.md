# Security Policy — Crypto T-Key S3

## Scope

This policy covers the Crypto T-Key S3 firmware, host integration, build scripts, and production tooling in this repository.

This project is security-sensitive embedded software and is not represented as independently certified, production-ready, or suitable for protecting valuable funds without additional review and testing.

## Supported security baseline

Use the latest commit on the default branch only after reviewing its changes. There are currently no tagged security releases. Firmware builds should be made from a pinned commit with a recorded toolchain, board package version, library set, and SHA-256 hash of the resulting binary.

## Reporting a vulnerability

Please do not publish exploitable details in a public issue. Contact the repository owner through a private GitHub security advisory if enabled, or through a private GitHub message. Include:

- affected commit, firmware build, or file;
- hardware and toolchain details;
- reproduction steps or a minimal proof of concept;
- security impact and any required physical access;
- suggested mitigation, if known.

Allow reasonable time for validation and remediation before public disclosure. Do not include recovery seeds, private keys, passwords, Wi-Fi credentials, or other secrets in a report.

## High-risk areas

Treat these areas as especially sensitive:

- FIDO2 credential storage and CTAP2 message handling;
- PIN verification, lockout, and duress behavior;
- seed generation, derivation, signing, and memory zeroization;
- transaction parsing and clear-signing display logic;
- MicroSD vault encryption and restore paths;
- setup SoftAP, Wi-Fi credential handling, and serial commands;
- eFuse and Secure Boot tooling.

## User safety

Never run the production eFuse tool unless you have a tested recovery path and have confirmed the exact chip, bootloader, partition table, signing keys, and firmware image. eFuse changes are irreversible and can permanently disable development or recovery access.
