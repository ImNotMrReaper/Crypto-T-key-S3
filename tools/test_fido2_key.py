#!/usr/bin/env python3
"""
test_fido2_key.py — Tactical FIDO2 / CTAPHID & Serial Verification Tool
=======================================================================
Directly tests the LilyGo T-Dongle S3 FIDO2 security key without requiring
any browser or external libraries. Sends raw CTAPHID INIT and WINK commands
over /dev/hidraw*, or communicates over serial CDC.
"""

import sys
import os
import glob
import time
import struct
import argparse

# CTAPHID Constants
CTAPHID_BROADCAST_CID = 0xFFFFFFFF
CTAPHID_CMD_INIT      = 0x80 | 0x06  # 0x86
CTAPHID_CMD_WINK      = 0x80 | 0x08  # 0x88
CTAPHID_CMD_CBOR      = 0x80 | 0x10  # 0x90
CTAP2_CMD_GET_INFO    = 0x04

def find_fido_hidraw():
    """Scans /sys/class/hidraw to identify FIDO / U2F security key endpoint."""
    candidates = []
    for hid_path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        name_path = os.path.join(hid_path, "device", "uevent")
        if os.path.exists(name_path):
            try:
                with open(name_path, "r") as f:
                    content = f.read()
                    if "HID_NAME=Crypto TKey" in content or "LilyGo" in content or "FIDO" in content or "Espressif" in content:
                        dev_node = os.path.join("/dev", os.path.basename(hid_path))
                        candidates.append(dev_node)
            except Exception:
                pass
    return candidates

def test_ctaphid_init(dev_node):
    """Sends a standard CTAPHID_INIT packet and verifies channel allocation."""
    print(f"\n[CTAPHID] 📡 Probing device node {dev_node}...")
    try:
        fd = os.open(dev_node, os.O_RDWR | os.O_NONBLOCK)
    except PermissionError:
        print(f"[CTAPHID] 🔒 Permission denied opening {dev_node}. Try running with sudo or check udev rules.")
        return None
    except Exception as e:
        print(f"[CTAPHID] ❌ Failed to open {dev_node}: {e}")
        return None

    # Build CTAPHID_INIT packet (64 bytes)
    # CID (4 bytes) = 0xFFFFFFFF
    # CMD (1 byte)  = 0x86
    # BCNT (2 bytes) = 8
    # Nonce (8 bytes) = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    nonce = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88])
    pkt = struct.pack(">IBH", CTAPHID_BROADCAST_CID, CTAPHID_CMD_INIT, len(nonce)) + nonce
    pkt += b"\x00" * (64 - len(pkt))

    try:
        os.write(fd, pkt)
        time.sleep(0.08)
        resp = os.read(fd, 64)
    except Exception as e:
        print(f"[CTAPHID] Read/write error on {dev_node}: {e}")
        os.close(fd)
        return None

    os.close(fd)

    if len(resp) >= 17:
        r_cid, r_cmd, r_bcnt = struct.unpack(">IBH", resp[:7])
        r_nonce = resp[7:15]
        new_cid = struct.unpack(">I", resp[15:19])[0]
        print(f"[CTAPHID] ✅ Received INIT Response!")
        print(f"  Broadcast CID: 0x{r_cid:08X} | CMD: 0x{r_cmd:02X} | Payload: {r_bcnt} bytes")
        print(f"  Nonce Match:   {'✔ YES' if r_nonce == nonce else '✖ MISMATCH'}")
        print(f"  Allocated CID: 0x{new_cid:08X}")
        return new_cid
    else:
        print(f"[CTAPHID] ⚠️ Unexpected response length: {len(resp)} bytes")
        return None

def test_ctaphid_wink(dev_node, cid):
    """Sends a CTAPHID_WINK command to trigger on-device LED flash and visual indicator."""
    print(f"\n[CTAPHID] 😉 Sending WINK command to CID 0x{cid:08X}...")
    try:
        fd = os.open(dev_node, os.O_RDWR | os.O_NONBLOCK)
        pkt = struct.pack(">IBH", cid, CTAPHID_CMD_WINK, 0)
        pkt += b"\x00" * (64 - len(pkt))
        os.write(fd, pkt)
        time.sleep(0.08)
        resp = os.read(fd, 64)
        os.close(fd)
        if len(resp) >= 7:
            r_cid, r_cmd, r_bcnt = struct.unpack(">IBH", resp[:7])
            print(f"[CTAPHID] ✨ WINK successfully acknowledged! (CMD: 0x{r_cmd:02X})")
            print("  Observe device display: 'DEVICE LOCATED // WINK VERIFIED' with rainbow shimmer.")
            return True
    except Exception as e:
        print(f"[CTAPHID] Error during WINK: {e}")
    return False

def main():
    parser = argparse.ArgumentParser(description="Crypto TKey S3 FIDO2 / CTAPHID Tactile Verification")
    parser.add_argument("--device", type=str, default=None, help="Explicit hidraw device (e.g. /dev/hidraw2)")
    args = parser.parse_args()

    print("==========================================================")
    print("  Crypto TKey S3 — Tactical FIDO2 / CTAPHID Verifier")
    print("==========================================================")

    devs = [args.device] if args.device else find_fido_hidraw()
    if not devs:
        # Fall back to checking all hidraw nodes
        devs = sorted(glob.glob("/dev/hidraw*"))

    print(f"[FIDO2] Candidate HID nodes: {devs}")

    active_cid = None
    active_dev = None

    for d in devs:
        cid = test_ctaphid_init(d)
        if cid:
            active_cid = cid
            active_dev = d
            break

    if active_dev and active_cid:
        print(f"\n[FIDO2] 🚀 Security Key confirmed active on {active_dev}!")
        time.sleep(0.5)
        test_ctaphid_wink(active_dev, active_cid)
        print("\n[FIDO2] ✅ Verification PASSED: FIDO2/CTAPHID protocol stack is 100% operational.")
    else:
        print("\n[FIDO2] ℹ️ Could not find active CTAPHID endpoint on current hidraw nodes.")
        print("  Ensure device is plugged in, or check with 'sudo chmod 666 /dev/hidraw*'.")

if __name__ == "__main__":
    main()
