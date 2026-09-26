#!/usr/bin/env python3
"""
Crypto TKey S3 — read-only hardware smoke test (safe on the key you use every day).

Nothing here changes the key: no PIN, no credential, no seed, no NVS write. It checks
  * CTAPHID: INIT, WINK-free PING echo of small and max-size (2048 B) messages sent back to back
    (the 36-packet burst that used to overflow the 8-packet RX queue), repeated
  * CTAP2 getInfo contents (versions, hmac-secret, maxMsgSize, algorithms)
  * malformed CBOR (a byte string whose 64-bit length wraps the decoder offset, truncated maps,
    unknown commands) gets an error status, never a user-presence prompt or a crash
  * serial `diag`: the key stayed up (uptime grows) and dropped no HID packets (hiddrop=0)
  * serial `addrs`: public address export terminates

  ~/.venvs/tkey-tests/bin/python tools/test_smoke_hw.py [--port /dev/ttyACM0]
"""
import argparse
import os
import re
import struct
import sys
import time

import serial
from fido2.ctap2 import Ctap2
from fido2.hid import CTAPHID, CtapHidDevice

VID, PID = 0x303A, 0x1001
results = []


def check(cond, name, detail=""):
    results.append(bool(cond))
    print(f"  {'✔' if cond else '✘'} {name}" + (f"  ({detail})" if detail and not cond else ""))
    return cond


def find_key():
    for dev in CtapHidDevice.list_devices():
        d = dev.descriptor
        if d.vid == VID and d.pid == PID:
            return dev
    return None


def serial_cmd(port, line, end, timeout=6.0):
    # rts=False/dtr=True: opening the port must not reset the key
    s = serial.Serial()
    s.port, s.baudrate, s.timeout, s.rts, s.dtr = port, 115200, 0.2, False, True
    s.open()
    try:
        s.reset_input_buffer()
        s.write((line + "\n").encode())
        buf, t0 = "", time.time()
        while time.time() - t0 < timeout:
            buf += s.read(4096).decode(errors="replace")
            if re.search(end, buf):
                return buf
        return buf
    finally:
        s.close()


def diag(port):
    out = serial_cmd(port, "diag", r"DIAG [^\n]*\n")
    m = re.search(r"DIAG ([^\n]*)", out)
    return dict(kv.split("=", 1) for kv in m.group(1).split() if "=" in kv) if m else {}


def cbor_status(dev, payload):
    """Raw CTAPHID_CBOR call; returns the CTAP status byte (None if the transport failed)."""
    try:
        resp = dev.call(CTAPHID.CBOR, payload)
        return resp[0] if resp else None
    except Exception as e:  # a CTAPHID-level error is also a rejection, but record it
        print(f"    transport error: {e}")
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=os.environ.get("TKEY_PORT", "/dev/ttyACM0"))
    ap.add_argument("--rounds", type=int, default=20, help="max-size PING rounds")
    args = ap.parse_args()

    print("== Serial ==")
    before = diag(args.port)
    check(before, "diag answers", "no DIAG line: is tkey-tracker.service holding the port?")

    dev = find_key()
    if not check(dev is not None, "FIDO HID interface present (303a:1001)"):
        return finish()

    print("== CTAPHID ==")
    for n in (0, 1, 57, 58, 116, 117, 1024, 2048):
        data = os.urandom(n)
        check(dev.call(CTAPHID.PING, data) == data, f"PING echo {n} B")
    ok = all(dev.call(CTAPHID.PING, os.urandom(2048)) is not None for _ in range(args.rounds))
    burst = [os.urandom(2048) for _ in range(args.rounds)]
    check(ok and all(dev.call(CTAPHID.PING, b) == b for b in burst),
          f"{args.rounds}× back-to-back 2048 B PING (36-packet bursts)")

    print("== CTAP2 getInfo ==")
    info = Ctap2(dev).get_info()
    check("FIDO_2_0" in info.versions and "U2F_V2" in info.versions, "versions FIDO_2_0 + U2F_V2")
    check("hmac-secret" in (info.extensions or []), "hmac-secret advertised")
    check(info.max_msg_size == 2048, "maxMsgSize 2048", str(info.max_msg_size))
    check(any(a.get("alg") == -7 for a in (info.algorithms or [])), "ES256 advertised")

    print("== Malformed CBOR (must be rejected without a touch prompt) ==")
    # makeCredential {1: bstr of length 2^64-9} — the decoder-offset wrap (finding F4)
    wrap = bytes([0x01, 0xA1, 0x01, 0x5B]) + struct.pack(">Q", 2**64 - 9) + b"\x00\x00"
    t0 = time.time()
    st = cbor_status(dev, wrap)
    check(st not in (None, 0x00) and time.time() - t0 < 2, "length-wrap makeCredential rejected fast",
          f"status={st}")
    for name, payload in [
        ("truncated map", bytes([0x01, 0xA5, 0x01])),
        ("map claims 2^32 entries", bytes([0x02, 0xBA, 0xFF, 0xFF, 0xFF, 0xFF])),
        ("indefinite-length map", bytes([0x02, 0xBF, 0xFF])),
        ("deep nesting", bytes([0x01, 0xA1, 0x10]) + bytes([0x81]) * 40 + b"\x00"),
        ("unknown command 0x55", bytes([0x55])),
    ]:
        t0 = time.time()
        st = cbor_status(dev, payload)
        check(st not in (None, 0x00) and time.time() - t0 < 2, f"{name} rejected", f"status={st}")
    check(Ctap2(dev).get_info().aaguid is not None, "key still answers after the malformed inputs")

    print("== Serial after the run ==")
    after = diag(args.port)
    if check(after, "diag answers"):
        check(int(after.get("uptime", "0").rstrip("s")) >= int(before.get("uptime", "0").rstrip("s")),
              "no reboot during the run", f"before={before.get('uptime')} after={after.get('uptime')}")
        check(after.get("hiddrop", "missing") == "0", "no HID packets dropped", f"hiddrop={after.get('hiddrop')}")
    out = serial_cmd(args.port, "addrs", r"ADDR END")
    check("ADDR END" in out, "addrs export terminates")
    return finish()


def finish():
    passed = sum(results)
    print(f"\n{passed}/{len(results)} checks passed")
    return 0 if results and all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
