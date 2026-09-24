#!/usr/bin/env python3
"""
burn_production_efuses.py — ESP32-S3 eFuse inspector for Crypto T-Key S3
=========================================================================
Read-only. Shows the security-relevant eFuses of a connected T-Dongle S3 and prints
the correct hardening sequence (see docs/SECURITY-AUDIT.md, findings C1/C2).

It deliberately does NOT burn anything. The previous burn path set SPI_BOOT_CRYPT_CNT
without a flash-encryption key or encrypted firmware, which permanently bricks the chip.
Flash encryption and Secure Boot must be provisioned by an ESP-IDF–built bootloader
(release mode) that generates the key on-chip and encrypts the image in place.

  python3 tools/burn_production_efuses.py            # show the hardening plan
  python3 tools/burn_production_efuses.py --summary  # read the live eFuse state
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

SECURITY_EFUSES = (
    "SPI_BOOT_CRYPT_CNT", "SECURE_BOOT_EN", "DIS_DOWNLOAD_MODE", "ENABLE_SECURITY_DOWNLOAD",
    "DIS_USB_JTAG", "DIS_PAD_JTAG", "DIS_DOWNLOAD_MANUAL_ENCRYPT", "KEY_PURPOSE_",
    "RD_DIS", "SOFT_DIS_JTAG",
)

PLAN = """
Crypto T-Key S3 — hardening sequence (test on a spare unit first)

 1. Chip-bound wallet key (one irreversible key block, device stays reflashable)
      espefuse --chip esp32s3 burn_key BLOCK_KEY5 <random32.bin> HMAC_UP
      Firmware derives the seed/master-secret wrapping key through the HMAC peripheral,
      so a flash dump is useless without this chip.
 2. Flash encryption (release) + Secure Boot v2
      Build the bootloader with ESP-IDF (CONFIG_SECURE_FLASH_ENC_ENABLED,
      CONFIG_SECURE_BOOT_V2_ENABLED, release mode) and flash it once. The bootloader
      generates the XTS-AES key on-chip, encrypts the app and burns the fuses itself.
      Never burn SPI_BOOT_CRYPT_CNT by hand.
 3. Download-mode lockdown — only after USB/OTA updates of signed images work
      ENABLE_SECURITY_DOWNLOAD (or DIS_DOWNLOAD_MODE), DIS_USB_JTAG, DIS_PAD_JTAG.
"""


def find_espefuse():
    for name in ("espefuse", "espefuse.py"):
        path = shutil.which(name) or os.path.expanduser(f"~/.local/bin/{name}")
        if os.access(path, os.X_OK):
            return path
    return None


def detect_port():
    for port in sorted(glob.glob("/dev/ttyACM*")):
        vendor = f"/sys/class/tty/{os.path.basename(port)}/device/../idVendor"
        try:
            if open(vendor).read().strip() == "303a":
                return port
        except OSError:
            pass
    return None


def summary(espefuse, port):
    res = subprocess.run([espefuse, "--chip", "esp32s3", "--port", port, "summary"],
                         capture_output=True, text=True, timeout=60)
    if res.returncode != 0:
        print(res.stderr.strip() or res.stdout.strip(), file=sys.stderr)
        return False
    for line in res.stdout.splitlines():
        if line.strip().startswith(SECURITY_EFUSES):
            print(line.rstrip())
    return True


def main():
    ap = argparse.ArgumentParser(description="Read-only ESP32-S3 eFuse inspector (Crypto T-Key S3)")
    ap.add_argument("--summary", "-s", action="store_true", help="read security eFuses from the device")
    ap.add_argument("--port", "-p", help="serial port (default: auto-detect VID 0x303A)")
    ap.add_argument("--burn-now", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args.burn_now:
        print("Refusing: burning is disabled. See docs/SECURITY-AUDIT.md (C2) and the plan below.", file=sys.stderr)
        print(PLAN, file=sys.stderr)
        sys.exit(2)

    if not args.summary:
        print(PLAN)
        return

    espefuse = find_espefuse()
    if not espefuse:
        sys.exit("espefuse not found — install esptool (pip install esptool)")
    port = args.port or detect_port()
    if not port:
        sys.exit("no T-Dongle S3 found (VID 0x303A); pass --port")
    print(f"Security eFuses on {port} (put the dongle in bootloader mode first):")
    sys.exit(0 if summary(espefuse, port) else 1)


if __name__ == "__main__":
    main()
