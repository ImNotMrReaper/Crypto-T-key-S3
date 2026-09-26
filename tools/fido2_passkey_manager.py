#!/usr/bin/env python3
"""
fido2_passkey_manager.py — FIDO2 PIN & Passkey Manager (using native libfido2)
"""

import sys
import subprocess
import shutil

def find_hidraw():
    res = subprocess.run(["fido2-token", "-L"], capture_output=True, text=True)
    for line in res.stdout.splitlines():
        if "303a" in line or "Espressif" in line or "DEV" in line:
            return line.split(":")[0].strip()
    # fallback to first hidraw if any
    lines = res.stdout.strip().splitlines()
    if lines:
        return lines[0].split(":")[0].strip()
    return None

def inspect_device(dev):
    print(f"=== Crypto TKey S3 FIDO2 Status ({dev}) ===")
    res = subprocess.run(["fido2-token", "-I", dev], capture_output=True, text=True)
    print(res.stdout)

def set_pin(dev):
    print(f"[*] Setting Client PIN on {dev}...")
    print("👉 When prompted, enter your new PIN (min 4 characters), then TAP the TKey physical button.")
    subprocess.run(["fido2-token", "-S", dev])

def change_pin(dev):
    print(f"[*] Changing Client PIN on {dev}...")
    subprocess.run(["fido2-token", "-C", dev])

def main():
    if not shutil.which("fido2-token"):
        print("[-] Error: fido2-token utility not installed.")
        sys.exit(1)

    dev = find_hidraw()
    if not dev:
        print("[-] Error: No FIDO2 token detected on USB.")
        sys.exit(1)

    if len(sys.argv) > 1:
        cmd = sys.argv[1].lower()
        if cmd == "set-pin":
            set_pin(dev)
        elif cmd == "change-pin":
            change_pin(dev)
        elif cmd == "info":
            inspect_device(dev)
        else:
            print(f"Unknown command: {cmd}")
    else:
        inspect_device(dev)
        print("\nAvailable actions:")
        print("  python3 tools/fido2_passkey_manager.py set-pin")
        print("  python3 tools/fido2_passkey_manager.py change-pin")

if __name__ == "__main__":
    main()
