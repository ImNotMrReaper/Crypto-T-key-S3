#!/usr/bin/env python3
"""
Preview the setup page (src/portal_page.h) in a desktop browser with a mock /api.

  python3 tools/preview_portal.py [--port 8765] [--state fresh|provisioned|locked]

The mock state comes from tools/coin_catalog.json; saves are printed, never sent anywhere.
"""
import argparse
import json
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs

ROOT = Path(__file__).resolve().parent.parent
FAMS = ["BTC", "EVM", "SOL", "DOGE", "LTC", "TRX", "XRP", "ATOM", "INJ", "XLM", "NEAR", "APT", "VET", "BCH", "SUI", "ZEC"]
FAM_NAMES = ["Bitcoin", "EVM", "Solana", "Dogecoin", "Litecoin", "TRON", "XRP Ledger", "Cosmos", "Injective",
             "Stellar", "NEAR", "Aptos", "VeChain", "Bitcoin Cash", "Sui", "Zcash"]
CATS = {"CRYPTO": 0, "STABLE": 1, "MEME": 2}
ON = {"BTC", "ETH", "SOL", "AVAX", "INJ", "DOGE", "PEPE"}


def page():
    src = (ROOT / "src/portal_page.h").read_text()
    return re.search(r'R"PAGE\((.*)\)PAGE"', src, re.S).group(1)


def palette():
    out = {}
    for line in (ROOT / "tools/coin_palette.txt").read_text().splitlines():
        parts = line.split()
        if len(parts) == 4:
            out[parts[0]] = "%02x%02x%02x" % tuple(map(int, parts[1:]))
    return out


def state(kind):
    if kind == "locked":
        return {"auth": False, "provisioned": True, "lockedFor": 0}
    cat = json.loads((ROOT / "tools/coin_catalog.json").read_text())
    cat = cat["coins"] if isinstance(cat, dict) else cat
    pal = palette()
    coins = []
    for c in cat:
        rgb = pal.get(c["symbol"]) or ("%02x%02x%02x" % tuple(c["rgb"]) if c.get("rgb") else "8d89a1")
        coins.append([c["symbol"], c["name"], c["network"], FAMS.index(c["family"]), CATS[c["category"]], rgb,
                      1 if c["symbol"] in ON else 0])
    prov = kind == "provisioned"
    return {"auth": True, "csrf": "preview", "provisioned": prov, "hasPassword": prov, "hasPin": prov,
            "hasDuress": False, "hasSeed": prov, "key_name": "Reaper's T-Key" if prov else "T-KEY", "wallpaper": 1 if prov else 0,
            "theme": {"rgb": "7764d8", "fx": 1, "speed": 3, "brightness": 1, "activeCustom": -1, "customModes": []},
            "wifi": ["Reaper Home 5G"] if prov else [], "families": FAM_NAMES, "coins": coins,
            "policy": {"duress": "wipe", "panic": "wipe", "lockout": "wipe", "countdown": 3},
            "sd": {"mounted": True, "hasBackup": prov, "hasFullBackup": prov}}


class H(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith("/api/state"):
            self._send(200, json.dumps(state(self.server.kind)))
        elif self.path.startswith("/api/scan"):
            self._send(200, json.dumps([{"ssid": "Reaper Home 5G", "rssi": -48, "secure": True},
                                        {"ssid": "Coffee Shop", "rssi": -71, "secure": False}]))
        else:
            self._send(200, page(), "text/html; charset=utf-8")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        form = parse_qs(self.rfile.read(n).decode())
        shown = {k: ("***" if k in ("pin", "duress", "setup_pass", "password") or k.startswith("wp") else v[0])
                 for k, v in form.items()}
        print("POST", self.path, shown)
        self._send(200, '{"ok":true}')

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--state", choices=["fresh", "provisioned", "locked"], default="provisioned")
    a = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    srv.kind = a.state
    print(f"Setup page preview on http://127.0.0.1:{a.port} ({a.state})")
    srv.serve_forever()
