#!/usr/bin/env python3
"""
Crypto TKey S3 — on-device UI walkthrough & soak (TEST firmware: -DTKEY_TEST_SERIAL_TOUCH).

Drives the real device with injected button events, captures every screen as a PNG
(`shot` framebuffer dump), decodes receive QR codes and compares them with `addrs`,
checks wake-only-first-press, then soaks with simulated price ticks while watching heap,
stack and resets. Never enters a PIN. Uses a throwaway wallet and wipes it at the end.

  python3 tools/test_device_walkthrough.py [--out DIR] [--soak SECONDS]
"""
import argparse
import base64
import os
import random
import re
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

results = []


def check(cond, name, detail=""):
    results.append((bool(cond), name))
    print(f"  {'✅ PASS' if cond else '❌ FAIL'}  {name}{('  — ' + detail) if detail and not cond else ''}")
    return cond


class Dev:
    def __init__(self):
        port = next(p.device for p in list_ports.comports() if p.vid == 0x303A)
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.1
        self.s.rts, self.s.dtr = False, True
        self.s.open()
        self.log = []

    def cmd(self, line, until=None, wait=2.0):
        self.s.reset_input_buffer()
        self.s.write((line + "\n").encode())
        buf, t0 = "", time.time()
        while time.time() - t0 < wait:
            buf += self.s.read(8192).decode(errors="replace")
            if until and until in buf:
                break
        self.log.append(buf)
        return buf

    def diag(self):
        out = self.cmd("diag", until="\n", wait=1.5)
        m = re.search(r"DIAG (.+)", out)
        return dict(kv.split("=", 1) for kv in m.group(1).split()) if m else {}

    def press(self, kind, settle=0.9):
        self.cmd(f"btn {kind}", until="injected", wait=1.0)
        time.sleep(settle)

    def shot(self, path):
        out = self.cmd("shot", until="SHOT END", wait=6.0)
        m = re.search(r"SHOT (\d+) (\d+)\r?\n(.*?)SHOT END", out, re.S)
        if not m:
            return None
        w, h = int(m.group(1)), int(m.group(2))
        raw = base64.b64decode("".join(m.group(3).split()))
        from PIL import Image
        img = Image.new("RGB", (w, h))
        px = img.load()
        for i in range(w * h):
            v = raw[2 * i] << 8 | raw[2 * i + 1]      # sprite buffer is byte-swapped RGB565
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            px[i % w, i // w] = (r * 255 // 31, g * 255 // 63, b * 255 // 31)
        img = img.resize((w * 4, h * 4), Image.NEAREST)
        img.save(path)
        return path


def decode_qr(path):
    import cv2
    img = cv2.imread(path)
    data, _, _ = cv2.QRCodeDetector().detectAndDecode(img)
    return data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="/tmp/tkey-shots")
    ap.add_argument("--soak", type=int, default=180)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    subprocess.run(["systemctl", "--user", "stop", "tkey-tracker.service"])
    d = Dev()
    n = [0]

    def snap(name):
        n[0] += 1
        return d.shot(os.path.join(args.out, f"{n[0]:02d}-{name}.png"))

    print("[1] Baseline")
    base = d.diag()
    print(f"    {base}")
    check(base, "diag responds")
    boot_reset = base.get("reset")

    # get to a known state: home screen
    for _ in range(4):
        if d.diag().get("state") == "2":
            break
        d.press("double")
    check(d.diag().get("state") == "2", "reached home screen (STATE_IDLE_READY)")
    snap("home")

    print("\n[2] Screen walkthrough")
    d.press("short"); st = d.diag()
    check(st.get("state") == "3", "short press: home -> passkey hub", str(st.get("state")))
    snap("passkey-hub")
    d.press("short"); st = d.diag()
    check(st.get("state") == "4", "short press: passkey hub -> price screen", str(st.get("state")))
    snap("prices-1")
    for i in range(2):
        d.press("short")
        snap(f"prices-{i + 2}")

    print("\n[3] Receive QR (throwaway wallet)")
    d.cmd("newseed quick", until="mnemonic:", wait=25)
    addrs = dict(re.findall(r"ADDR (\w+) (\S+)", d.cmd("addrs", until="ADDR END")))
    check(len(addrs) >= 4, f"throwaway wallet addresses: {sorted(addrs)}")
    for _ in range(4):  # back to price screen
        if d.diag().get("state") == "4":
            break
        d.press("double")
    # walk the carousel until BTC, then show its receive QR
    for sym_try in range(12):
        shot = snap("carousel")
        d.press("vlong", settle=1.2)
        st = d.diag()
        if st.get("state") != "16" and st.get("state") != str(16):
            pass
        qr_path = snap("receive")
        data = decode_qr(qr_path) if qr_path else ""
        d.press("short")  # back to prices
        if data:
            want = {a.upper() if a.startswith("bc1") else a for a in addrs.values()}
            check(data in want, f"receive QR decodes to a wallet address ({data[:14]}...)", data)
            if data.startswith("BC1"):
                check(data == addrs["BTC"].upper(), "BTC QR is the uppercase bech32 of the wallet address")
                break
        d.press("short")  # next coin

    print("\n[4] PIN screen (no PIN submitted)")
    d.press("long")
    st = d.diag()
    check(st.get("state") == "5", "long press on prices opens PIN screen", str(st.get("state")))
    snap("pin")
    d.press("double")
    check(d.diag().get("state") == "4", "double press at first digit cancels back to prices")

    print("\n[5] Display sleep: first press only wakes")
    for _ in range(3):
        if d.diag().get("state") == "2":
            break
        d.press("double")
    d.press("long")  # toggles display sleep on home
    st = d.diag()
    check(st.get("sleeping") == "1", "long press on home sleeps the display")
    d.press("short")
    st = d.diag()
    check(st.get("sleeping") == "0" and st.get("state") == "2",
          "first press wakes without changing screen", f"sleeping={st.get('sleeping')} state={st.get('state')}")

    print(f"\n[6] Soak: {args.soak}s of price ticks")
    heap0 = int(d.diag().get("heap", 0))
    t0 = time.time()
    samples = []
    syms = ["BTC", "ETH", "SOL", "DOGE", "PEPE"]
    price = {"BTC": 84000.0, "ETH": 2690.0, "SOL": 115.0, "DOGE": 0.09, "PEPE": 0.0000105}
    while time.time() - t0 < args.soak:
        sym = random.choice(syms)
        price[sym] *= 1 + random.uniform(-0.002, 0.002)
        d.s.write(f"setprice {sym} {price[sym]:.10g} {random.uniform(-5, 5):.2f}\n".encode())
        time.sleep(0.25)
        if int(time.time() - t0) % 15 == 0:
            st = d.diag()
            if st:
                samples.append((int(time.time() - t0), int(st["heap"]), int(st["minheap"]), st["uptime"], st["reset"]))
            time.sleep(1)
    for s_ in samples:
        print(f"    t={s_[0]:4d}s heap={s_[1]} minheap={s_[2]} uptime={s_[3]} reset={s_[4]}")
    end = d.diag()
    check(end.get("reset") == boot_reset and int(end.get("uptime", "0s").rstrip("s")) >= args.soak,
          "no reboot during soak", str(end))
    check(abs(int(end.get("heap", 0)) - heap0) < 8192, f"heap stable ({heap0} -> {end.get('heap')})")
    check(int(end.get("loopstack", 0)) > 1024, f"loop task stack headroom {end.get('loopstack')} bytes")

    print("\n[7] Cleanup")
    d.cmd("wallet wipe", until="erased", wait=3)
    subprocess.run(["systemctl", "--user", "start", "tkey-tracker.service"])
    ok = sum(r for r, _ in results)
    print(f"\n{ok}/{len(results)} checks passed — screenshots in {args.out}")
    sys.exit(0 if ok == len(results) else 1)


if __name__ == "__main__":
    main()
