#!/usr/bin/env python3
"""
Crypto TKey S3 — wallet persistence & serial hardening test (TEST firmware only:
built with -DTKEY_TEST_SERIAL_TOUCH). Creates a throwaway seed, checks it survives a
reboot and matches its cached addresses, then erases it. Never enters a PIN, so the
wrong-PIN counter is untouched.

  python3 tools/test_wallet_hw.py
"""
import glob
import os
import subprocess
import sys
import time

import serial

ESPTOOL = os.path.expanduser("~/.local/bin/esptool")
results = []


def check(cond, name, detail=""):
    results.append(bool(cond))
    print(f"  {'PASS' if cond else 'FAIL'}  {name}{('  — ' + detail) if detail and not cond else ''}")


def port():
    return sorted(glob.glob("/dev/ttyACM*"))[-1]


def open_port(timeout=10):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port(), 115200, 0.1
            s.rts, s.dtr = False, True
            s.open()
            return s
        except Exception:
            time.sleep(0.05)
    raise SystemExit("serial port not available")


def reboot():
    """1200-baud touch -> ROM, then watchdog reset into the app. Returns (serial, boot log, seconds)."""
    try:
        s = serial.Serial(port(), 1200)
        time.sleep(0.2)
        s.close()
    except Exception:
        pass
    for _ in range(40):
        if b"USB JTAG" in subprocess.run(["lsusb"], capture_output=True).stdout:
            break
        time.sleep(0.25)
    for _ in range(40):
        try:
            serial.Serial(port()).close()
            break
        except Exception:
            time.sleep(0.25)
    subprocess.run([ESPTOOL, "--chip", "esp32s3", "--port", port(), "--before", "no_reset",
                    "--after", "watchdog_reset", "chip_id"], capture_output=True)
    t0 = time.time()
    time.sleep(0.3)
    s = open_port()
    log = read_until(s, "Crypto TKey S3 Ready", 25)
    return s, log, time.time() - t0


def read_until(s, marker, timeout):
    buf = ""
    t0 = time.time()
    while time.time() - t0 < timeout:
        buf += s.read(4096).decode(errors="replace")
        if marker in buf:
            time.sleep(0.2)
            buf += s.read(4096).decode(errors="replace")
            break
    return buf


def cmd(s, line, marker, timeout=15):
    s.reset_input_buffer()
    s.write((line + "\n").encode())
    return read_until(s, marker, timeout)


def main():
    subprocess.run(["systemctl", "--user", "stop", "tkey-tracker.service"])
    print("[1] Boot")
    s, log, secs = reboot()
    check("Crypto TKey S3 Ready" in log, f"boots to ready ({secs:.1f}s after reset)")
    check("abandon" not in log and "0x9858EfFD" not in log, "no BIP-39 test-vector wallet at boot")
    check("Validating PIN" not in log, "PIN never echoed")
    if "Seed present" in log:
        print("    (a wallet seed already exists; skipping create/wipe to keep it)")
        keep = True
    else:
        check("No wallet seed yet" in log, "reports no wallet seed")
        keep = False

    print("\n[2] Mnemonic validation")
    good = "abandon " * 11 + "about"
    bad_checksum = "abandon " * 12
    typo = "abandon " * 11 + "abuot"
    check("valid: YES" in cmd(s, f"wallet check {good.strip()}", "valid:"), "accepts BIP-39 test vector")
    check("valid: NO" in cmd(s, f"wallet check {bad_checksum.strip()}", "valid:"), "rejects bad checksum")
    check("valid: NO" in cmd(s, f"wallet check {typo.strip()}", "valid:"), "rejects unknown word")

    print("\n[3] Locked-vault serial surface")
    check("locked" in cmd(s, "seed --allow-serial", "[VAULT]").lower(), "seed export refused while locked")
    check("disabled" in cmd(s, "setpin 1111", "Error"), "serial PIN change disabled")

    if keep:
        finish()
        return

    print("\n[4] Create + persist a throwaway seed")
    out = cmd(s, "newseed quick", "[SEED]", 30)
    check("mnemonic:" in out, "seed generated and stored")
    out = cmd(s, "wallet selftest", "after lock", 30)
    check("unlock=1" in out and "cached==derived:YES" in out, "decrypts, derives, matches cached addresses", out.strip()[-160:])
    check("mnemonic cleared=YES" in out, "lock() wipes the mnemonic from RAM")
    print("    " + next((l for l in out.splitlines() if "unlock=" in l), "").strip())

    print("\n[5] Survives reboot")
    s.close()
    s, log, secs = reboot()
    check("Seed present" in log, f"seed still present after reboot ({secs:.1f}s boot)")
    out = cmd(s, "wallet selftest", "after lock", 30)
    check("unlock=1" in out and "cached==derived:YES" in out, "reboot: decrypt + derive + match", out.strip()[-160:])

    print("\n[6] Clean up")
    cmd(s, "wallet wipe", "erased")
    s.close()
    s, log, _ = reboot()
    check("No wallet seed yet" in log, "throwaway seed removed")
    finish()


def finish():
    subprocess.run(["systemctl", "--user", "start", "tkey-tracker.service"])
    print(f"\n{sum(results)}/{len(results)} checks passed")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
