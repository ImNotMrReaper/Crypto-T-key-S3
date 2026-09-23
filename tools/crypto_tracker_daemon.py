#!/usr/bin/env python3
"""
crypto_tracker_daemon.py — Real-Time Live Price Streaming for Crypto TKey S3
=============================================================================
Architecture:
  • Kraken WebSocket (wss://ws.kraken.com/v2): sub-second live tick stream for
    top coins — pushes price to device every ~1 second as new ticks arrive.
  • CoinGecko REST fallback: refreshes long-tail coins every 30 seconds.
  • On-chain balances: Mempool/ETH RPC/SOL RPC queried every 5 minutes.
  • Serial CDC: all updates pushed to /dev/ttyACM0 at 115200 baud via setprice / setbal.
"""

import sys
import os
import time
import json
import glob
import argparse
import threading
import urllib.request
import urllib.error
import serial

try:
    import websocket
    HAS_WEBSOCKET = True
except ImportError:
    HAS_WEBSOCKET = False

# ─── CoinGecko ID Map (full 44-coin registry) ─────────────────────────────────
COINGECKO_MAP = {
    # ── CRYPTO ────────────────────────────────────────────────────────────────
    "BTC":    "bitcoin",
    "ETH":    "ethereum",
    "SOL":    "solana",
    "BNB":    "binancecoin",
    "XRP":    "ripple",
    "ADA":    "cardano",
    "AVAX":   "avalanche-2",
    "DOT":    "polkadot",
    "LINK":   "chainlink",
    "LTC":    "litecoin",
    "BCH":    "bitcoin-cash",
    "ATOM":   "cosmos",
    "POL":    "matic-network",
    "TRX":    "tron",
    "NEAR":   "near",
    "SUI":    "sui",
    "APT":    "aptos",
    "TON":    "the-open-network",
    "XLM":   "stellar",
    "ALGO":   "algorand",
    "HBAR":   "hedera-hashgraph",
    "VET":    "vechain",
    "FIL":    "filecoin",
    "ICP":    "internet-computer",
    "TAO":    "bittensor",
    "INJ":    "injective-protocol",
    "ARB":    "arbitrum",
    "OP":     "optimism",
    "KAS":    "kaspa",
    "XMR":    "monero",
    "EGLD":   "elrond-erd-2",
    "UNI":    "uniswap",
    # ── MEME COINS ────────────────────────────────────────────────────────────
    "DOGE":   "dogecoin",
    "SHIB":   "shiba-inu",
    "PEPE":   "pepe",
    "BONK":   "bonk",
    "FLOKI":  "floki",
    "WIF":    "dogwifcoin",
    "BRETT":  "based-brett",
    "MOG":    "mog-coin",
    "TURBO":  "turbo",
    "POPCAT": "popcat",
    "NEIRO":  "neiro-ethereum",
    "GOAT":   "goatseus-maximus",
}

# ─── Kraken WebSocket v2 subscription pairs ───────────────────────────────────
# Maps Kraken instrument names -> our symbol
KRAKEN_WS_PAIRS = {
    "BTC/USD":   "BTC",
    "ETH/USD":   "ETH",
    "SOL/USD":   "SOL",
    "BNB/USD":   "BNB",
    "XRP/USD":   "XRP",
    "DOGE/USD":  "DOGE",
    "ADA/USD":   "ADA",
    "LTC/USD":   "LTC",
    "DOT/USD":   "DOT",
    "LINK/USD":  "LINK",
    "BCH/USD":   "BCH",
    "AVAX/USD":  "AVAX",
    "ATOM/USD":  "ATOM",
    "UNI/USD":   "UNI",
    "XMR/USD":   "XMR",
    "PEPE/USD":  "PEPE",
    "BONK/USD":  "BONK",
    "WIF/USD":   "WIF",
    "FLOKI/USD": "FLOKI",
}

# ─── Shared live price cache (updated by WebSocket thread) ────────────────────
_price_cache = {}          # { "BTC": {"price": 86533.0, "change24h": +1.2}, ... }
_price_lock  = threading.Lock()
_open_prices = {}          # { "BTC": open_price } for 24h change calc

# ─── Serial port reference (set by main thread) ───────────────────────────────
_serial_port = None
_serial_lock = threading.Lock()
_last_push   = {}          # { "BTC": last_pushed_price } — skip if unchanged


def _safe_serial_write(line: str):
    """Thread-safe write to serial port."""
    with _serial_lock:
        if _serial_port and _serial_port.is_open:
            try:
                _serial_port.write(line.encode())
            except Exception as e:
                print(f"[TRACKER] Serial write error: {e}", file=sys.stderr)


def _push_price(sym: str, price: float, chg: float):
    """Push a price update to device if it changed meaningfully."""
    global _last_push
    prev = _last_push.get(sym, None)
    # Push if: first time, or price moved ≥ 0.01% (avoids flooding on micro-noise)
    if prev is None or abs(price - prev) / max(prev, 1e-12) >= 0.0001:
        _last_push[sym] = price
        try:
            cmd = f"setprice {sym} {float(price):.8f} {float(chg):.2f}\n"
            _safe_serial_write(cmd)
        except (TypeError, ValueError) as e:
            print(f"[TRACKER] ⚠ Push error {sym}: {e}", file=sys.stderr)


# ─── Kraken WebSocket Thread ──────────────────────────────────────────────────
def _kraken_ws_thread():
    """
    Connects to Kraken WebSocket v2, subscribes to ticker for all available
    pairs, and continuously updates _price_cache. Reconnects on disconnect.
    Runs as a daemon thread.
    """
    if not HAS_WEBSOCKET:
        return

    pairs = list(KRAKEN_WS_PAIRS.keys())
    subscribe_msg = json.dumps({
        "method": "subscribe",
        "params": {
            "channel": "ticker",
            "symbol": pairs
        }
    })

    def on_message(ws, raw):
        try:
            msg = json.loads(raw)
            if msg.get("channel") != "ticker":
                return
            for item in msg.get("data", []):
                sym_pair = item.get("symbol", "")
                our_sym  = KRAKEN_WS_PAIRS.get(sym_pair)
                if not our_sym:
                    continue
                price = float(item.get("last", 0))
                if price <= 0:
                    continue
                # 24h change: (last - open_24h) / open_24h * 100
                open_24 = float(item.get("open_24h", price))
                chg = ((price - open_24) / open_24 * 100.0) if open_24 > 0 else 0.0
                with _price_lock:
                    _price_cache[our_sym] = {"price": price, "change24h": chg}
                _push_price(our_sym, price, chg)
        except Exception as e:
            print(f"[WS] Parse error: {e}", file=sys.stderr)

    def on_open(ws):
        ws.send(subscribe_msg)
        print("[TRACKER] ✅ Kraken WebSocket connected — live tick stream active")

    def on_error(ws, err):
        print(f"[TRACKER] ⚠ Kraken WS error: {err}", file=sys.stderr)

    def on_close(ws, code, msg):
        print(f"[TRACKER] 🔌 Kraken WS closed ({code}), reconnecting in 5s...")

    while True:
        try:
            ws = websocket.WebSocketApp(
                "wss://ws.kraken.com/v2",
                on_open=on_open,
                on_message=on_message,
                on_error=on_error,
                on_close=on_close
            )
            ws.run_forever(ping_interval=20, ping_timeout=10)
        except Exception as e:
            print(f"[TRACKER] WS thread exception: {e}", file=sys.stderr)
        time.sleep(5)


# ─── CoinGecko REST Fallback ──────────────────────────────────────────────────
def fetch_coingecko_prices():
    """Fetches USD prices and 24h changes from CoinGecko for all registered coins."""
    ids = ",".join(COINGECKO_MAP.values())
    url = (f"https://api.coingecko.com/api/v3/simple/price"
           f"?ids={ids}&vs_currencies=usd&include_24hr_change=true")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (TKey-Companion/1.0)"})
    try:
        with urllib.request.urlopen(req, timeout=12) as res:
            return json.loads(res.read().decode())
    except Exception as e:
        print(f"[TRACKER] CoinGecko error: {e}", file=sys.stderr)
        return {}


def fetch_kraken_rest():
    """Kraken REST fallback for top pairs — used when WebSocket unavailable."""
    url = ("https://api.kraken.com/0/public/Ticker"
           "?pair=XXBTZUSD,XETHZUSD,SOLUSD,PEPEUSD,XDGUSD,BNBUSD,XXRPZUSD")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (TKey-FastTicker/1.0)"})
    rates = {}
    try:
        with urllib.request.urlopen(req, timeout=5) as res:
            data = json.loads(res.read().decode())
            results = data.get("result", {})
            pair_map = {
                "XXBTZUSD": "BTC", "XETHZUSD": "ETH", "SOLUSD": "SOL",
                "PEPEUSD":  "PEPE", "XDGUSD": "DOGE", "BNBUSD": "BNB",
                "XXRPZUSD": "XRP",
            }
            for k_pair, sym in pair_map.items():
                if k_pair in results:
                    info  = results[k_pair]
                    curr  = float(info["c"][0])
                    open_ = float(info["o"])
                    chg   = ((curr - open_) / open_ * 100.0) if open_ > 0 else 0.0
                    rates[sym] = {"price": curr, "change24h": chg}
    except Exception as e:
        print(f"[TRACKER] Kraken REST error: {e}", file=sys.stderr)
    return rates


# ─── On-Chain Balance Fetchers ────────────────────────────────────────────────
def fetch_btc_balance(address):
    if not address or len(address) < 20 or not address.startswith("bc1"):
        return 0.0
    url = f"https://mempool.space/api/address/{address}"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=8) as res:
            data  = json.loads(res.read().decode())
            chain = data.get("chain_stats", {})
            sats  = chain.get("funded_txo_sum", 0) - chain.get("spent_txo_sum", 0)
            return max(0.0, sats / 100_000_000.0)
    except Exception as e:
        print(f"[TRACKER] Mempool BTC error ({address}): {e}", file=sys.stderr)
        return 0.0


def fetch_eth_balance(address):
    if not address or not address.startswith("0x") or len(address) != 42:
        return 0.0
    url     = "https://ethereum-rpc.publicnode.com"
    payload = json.dumps({"jsonrpc": "2.0", "method": "eth_getBalance",
                          "params": [address, "latest"], "id": 1}).encode()
    req = urllib.request.Request(url, data=payload,
                                 headers={"Content-Type": "application/json",
                                          "User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=8) as res:
            data = json.loads(res.read().decode())
            return int(data.get("result", "0x0"), 16) / 1e18
    except Exception as e:
        print(f"[TRACKER] ETH RPC error: {e}", file=sys.stderr)
        return 0.0


def fetch_pepe_balance(address):
    if not address or not address.startswith("0x") or len(address) != 42:
        return 0.0
    pepe_contract = "0x6982508145454Ce325dDbE47a25d4ec3d2311933"
    clean_addr    = address[2:].lower().zfill(64)
    data          = "0x70a08231" + clean_addr
    url           = "https://ethereum-rpc.publicnode.com"
    payload       = json.dumps({"jsonrpc": "2.0", "method": "eth_call",
                                "params": [{"to": pepe_contract, "data": data}, "latest"],
                                "id": 2}).encode()
    req = urllib.request.Request(url, data=payload,
                                 headers={"Content-Type": "application/json",
                                          "User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=8) as res:
            resp    = json.loads(res.read().decode())
            hex_val = resp.get("result", "0x0")
            return int(hex_val, 16) / 1e18
    except Exception as e:
        print(f"[TRACKER] PEPE balance error: {e}", file=sys.stderr)
        return 0.0


def fetch_sol_balance(address):
    if not address or len(address) < 32:
        return 0.0
    url     = "https://api.mainnet-beta.solana.com"
    payload = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "getBalance",
                          "params": [address]}).encode()
    req = urllib.request.Request(url, data=payload,
                                 headers={"Content-Type": "application/json",
                                          "User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=8) as res:
            data     = json.loads(res.read().decode())
            lamports = data.get("result", {}).get("value", 0)
            return lamports / 1e9
    except Exception as e:
        print(f"[TRACKER] Solana RPC error: {e}", file=sys.stderr)
        return 0.0


def query_device_addresses(ser):
    """Reads derived addresses from the device if unlocked."""
    addresses = {}
    try:
        ser.reset_input_buffer()
        ser.write(b"addresses\n")
        time.sleep(0.3)
        raw = ser.read(2048).decode(errors="ignore")
        for line in raw.splitlines():
            line = line.strip()
            if line.startswith("bc1"):
                addresses["BTC"] = line
            elif line.startswith("0x") and len(line) == 42:
                addresses["ETH"] = line
            elif len(line) >= 32 and not line.startswith("bc1") and not line.startswith("0x"):
                addresses["SOL"] = line
    except Exception as e:
        print(f"[TRACKER] Address query error: {e}", file=sys.stderr)
    return addresses


# ─── Main Sync Cycle ──────────────────────────────────────────────────────────
def sync_cycle(port, ws_active=False):
    """
    One full sync cycle:
      1. Open serial
      2. If WebSocket NOT active: fetch Kraken REST + CoinGecko, push all prices
      3. If WebSocket IS active: CoinGecko fills gaps for coins not on WS
      4. Query on-chain balances
    """
    global _serial_port

    print(f"[TRACKER] ⚡ Connecting to Crypto TKey S3 on {port}...")
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 2.0
    ser.rts = False
    ser.dtr = True
    ser.open()
    time.sleep(0.4)

    with _serial_lock:
        _serial_port = ser

    synced_coins = set()

    # ── 1. Fast tier: Kraken REST (instant, no WS required) ──────────────────
    if not ws_active:
        print("[TRACKER] ⚡ Fetching Kraken sub-second rates...")
        kraken = fetch_kraken_rest()
        for sym, info in kraken.items():
            p, chg = info["price"], info["change24h"]
            with _price_lock:
                _price_cache[sym] = {"price": p, "change24h": chg}
            try:
                ser.write(f"setprice {sym} {p:.8f} {chg:.2f}\n".encode())
            except (TypeError, ValueError):
                pass
            synced_coins.add(sym)
            time.sleep(0.03)
        print(f"[TRACKER] ⚡ Kraken REST pushed: {sorted(synced_coins)}")

    # ── 2. CoinGecko: fill remaining coins ───────────────────────────────────
    print("[TRACKER] 🌐 Fetching CoinGecko extended registry...")
    prices = fetch_coingecko_prices()
    if prices:
        for sym, cg_id in COINGECKO_MAP.items():
            if sym in synced_coins:
                continue
            coin_data = prices.get(cg_id)
            if coin_data is None:
                continue
            p   = coin_data.get("usd") or 0.0
            chg = coin_data.get("usd_24h_change") or 0.0
            with _price_lock:
                _price_cache[sym] = {"price": float(p), "change24h": float(chg)}
            try:
                ser.write(f"setprice {sym} {float(p):.8f} {float(chg):.2f}\n".encode())
                synced_coins.add(sym)
            except (TypeError, ValueError) as e:
                print(f"[TRACKER] ⚠ Skipping {sym}: {e}", file=sys.stderr)
            time.sleep(0.03)
        print(f"[TRACKER] ✅ Full registry synced ({len(synced_coins)} coins)")

    # ── 3. On-chain balances ──────────────────────────────────────────────────
    addrs = query_device_addresses(ser)
    if addrs:
        print(f"[TRACKER] 🔍 Addresses: {list(addrs.keys())}")
        if "BTC" in addrs:
            bal = fetch_btc_balance(addrs["BTC"]) or 0.0
            try:
                ser.write(f"setbal BTC {bal:.8f}\n".encode())
            except (TypeError, ValueError):
                pass
            time.sleep(0.05)
        if "ETH" in addrs:
            bal = fetch_eth_balance(addrs["ETH"]) or 0.0
            try:
                ser.write(f"setbal ETH {bal:.6f}\n".encode())
            except (TypeError, ValueError):
                pass
            time.sleep(0.05)
            bal = fetch_pepe_balance(addrs["ETH"]) or 0.0
            try:
                ser.write(f"setbal PEPE {bal:.2f}\n".encode())
            except (TypeError, ValueError):
                pass
            time.sleep(0.05)

    with _serial_lock:
        _serial_port = None
    ser.close()
    print("[TRACKER] ✨ Sync cycle complete!\n")


# ─── Live Push Loop (when WebSocket is active) ────────────────────────────────
def live_push_loop(port, coingecko_interval=30, balance_interval=300):
    """
    Opens a persistent serial connection and:
      - Receives real-time prices from the WebSocket thread via _price_cache
      - Continuously pushes new ticks to device as they arrive (~1s cadence)
      - Re-fetches CoinGecko every `coingecko_interval` seconds for long-tail coins
      - Re-checks balances every `balance_interval` seconds
    """
    global _serial_port

    print(f"[TRACKER] 🔌 Opening persistent serial on {port}...")
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = 115200
        ser.timeout = 2.0
        ser.rts = False
        ser.dtr = True
        ser.open()
    except Exception as e:
        print(f"[TRACKER] Serial open error: {e}", file=sys.stderr)
        return

    time.sleep(0.4)
    with _serial_lock:
        _serial_port = ser

    last_coingecko = 0
    last_balance   = 0
    synced_cg      = set()

    print("[TRACKER] 🚀 Live push loop active — streaming real-time ticks to LCD...")

    try:
        while True:
            now = time.time()

            # ── Push any WebSocket price updates ─────────────────────────────
            with _price_lock:
                snapshot = dict(_price_cache)
            for sym, info in snapshot.items():
                _push_price(sym, info["price"], info["change24h"])

            # ── CoinGecko refresh for long-tail coins ─────────────────────────
            if now - last_coingecko >= coingecko_interval:
                last_coingecko = now
                prices = fetch_coingecko_prices()
                if prices:
                    ws_syms = set(KRAKEN_WS_PAIRS.values())
                    for sym, cg_id in COINGECKO_MAP.items():
                        if sym in ws_syms:
                            continue  # WS is authoritative for these
                        coin_data = prices.get(cg_id)
                        if not coin_data:
                            continue
                        p   = coin_data.get("usd") or 0.0
                        chg = coin_data.get("usd_24h_change") or 0.0
                        with _price_lock:
                            _price_cache[sym] = {"price": float(p), "change24h": float(chg)}
                        _push_price(sym, float(p), float(chg))
                    print(f"[TRACKER] 🌐 CoinGecko long-tail refresh complete")

            # ── Balance refresh ───────────────────────────────────────────────
            if now - last_balance >= balance_interval:
                last_balance = now
                addrs = query_device_addresses(ser)
                if addrs:
                    if "BTC" in addrs:
                        bal = fetch_btc_balance(addrs["BTC"]) or 0.0
                        try:
                            ser.write(f"setbal BTC {bal:.8f}\n".encode())
                        except Exception:
                            pass
                    if "ETH" in addrs:
                        bal = fetch_eth_balance(addrs["ETH"]) or 0.0
                        try:
                            ser.write(f"setbal ETH {bal:.6f}\n".encode())
                        except Exception:
                            pass
                        bal = fetch_pepe_balance(addrs["ETH"]) or 0.0
                        try:
                            ser.write(f"setbal PEPE {bal:.2f}\n".encode())
                        except Exception:
                            pass

            time.sleep(1.0)

    except (serial.SerialException, OSError) as e:
        print(f"[TRACKER] Serial disconnected: {e}", file=sys.stderr)
    finally:
        with _serial_lock:
            _serial_port = None
        try:
            ser.close()
        except Exception:
            pass


def find_device_port():
    candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    for port in candidates:
        try:
            s = serial.Serial()
            s.port = port
            s.baudrate = 115200
            s.timeout = 0.5
            s.rts = False
            s.dtr = True
            s.open()
            s.write(b"status\n")
            time.sleep(0.15)
            resp = s.read(256).decode(errors="ignore")
            s.close()
            if "Uptime:" in resp or "Vault:" in resp:
                return port
        except Exception:
            continue
    if candidates:
        return candidates[0]
    return None


# ─── Entry Point ──────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Crypto TKey S3 — Real-Time Live Price Streaming Daemon")
    parser.add_argument("--once",     action="store_true",
                        help="Run a single sync cycle and exit (no WebSocket)")
    parser.add_argument("--interval", type=int, default=5,
                        help="Fallback REST poll interval in seconds (default: 5, WebSocket preferred)")
    parser.add_argument("--port",     type=str, default=None,
                        help="Explicit serial port (e.g. /dev/ttyACM0)")
    parser.add_argument("--no-ws",    action="store_true",
                        help="Disable WebSocket, use REST-only polling")
    args = parser.parse_args()

    print("=" * 62)
    print("  Crypto TKey S3 — Real-Time Live Price Streaming Daemon")
    print("  Kraken WebSocket: sub-second tick stream")
    print("  CoinGecko REST:   30s refresh for long-tail coins")
    print("  On-Chain Balances: 5-minute refresh")
    print("=" * 62)

    use_ws = HAS_WEBSOCKET and not args.no_ws and not args.once

    if use_ws:
        print("[TRACKER] 🔌 Starting Kraken WebSocket daemon thread...")
        ws_thread = threading.Thread(target=_kraken_ws_thread, daemon=True)
        ws_thread.start()
        time.sleep(2.0)  # Give WS time to connect before opening serial
    else:
        if not HAS_WEBSOCKET:
            print("[TRACKER] ⚠ websocket-client not installed — using REST polling")
            print("[TRACKER]   Install: pip3 install websocket-client")

    while True:
        port = args.port or find_device_port()
        if not port:
            print("[TRACKER] ⏳ Waiting for Crypto TKey S3 to be plugged in...")
            time.sleep(3)
            continue

        if args.once:
            sync_cycle(port, ws_active=False)
            break
        elif use_ws:
            print(f"[TRACKER] 📡 Device found on {port} — entering live push loop")
            live_push_loop(port, coingecko_interval=30, balance_interval=300)
            print("[TRACKER] 🔄 Serial disconnected, waiting for reconnect...")
            time.sleep(3)
        else:
            # REST-only fast polling (every `interval` seconds)
            try:
                sync_cycle(port, ws_active=False)
            except Exception as e:
                print(f"[TRACKER] Sync error: {e}", file=sys.stderr)
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
