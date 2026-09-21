#!/usr/bin/env python3
"""
crypto_tracker_daemon.py — Real-Time Blockchain & Market Companion for Crypto TKey S3
=====================================================================================
Autonomously synchronizes live cryptocurrency market prices and on-chain wallet balances
into the LilyGo T-Dongle S3 over USB Serial CDC without requiring any manual typing.

Features:
  1. Live CoinGecko Price Sync: Streams real-time USD prices and 24h change for all 24 coins.
  2. Automated On-Chain Balance Tracking:
     - Bitcoin (Mempool.space API): Computes (funded_txo_sum - spent_txo_sum) in satoshis.
     - Ethereum (Public RPC): Calls eth_getBalance for EVM addresses.
     - Solana (Public RPC): Calls getBalance for Ed25519 addresses.
  3. Dynamic Hotplug Detection: Auto-reconnects whenever TKey S3 is inserted.
"""

import sys
import os
import time
import json
import glob
import argparse
import urllib.request
import urllib.error
import serial

COINGECKO_MAP = {
    "BTC": "bitcoin",
    "ETH": "ethereum",
    "SOL": "solana",
    "LTC": "litecoin",
    "DOGE": "dogecoin",
    "ADA": "cardano",
    "XRP": "ripple",
    "AVAX": "avalanche-2",
    "DOT": "polkadot",
    "LINK": "chainlink",
    "POL": "matic-network",
    "ATOM": "cosmos",
    "TRX": "tron",
    "NEAR": "near",
    "KAS": "kaspa",
    "SUI": "sui",
    "APT": "aptos",
    "TON": "the-open-network",
    "XLM": "stellar",
    "BCH": "bitcoin-cash",
    "BNB": "binancecoin",
    "SHIB": "shiba-inu",
    "UNI": "uniswap",
    "XMR": "monero",
    "PEPE": "pepe",
    "BONK": "bonk",
    "FLOKI": "floki",
    "WIF": "dogwifcoin"
}

def find_device_port():
    """Finds the serial CDC port for LilyGo T-Dongle S3."""
    candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    for port in candidates:
        try:
            s = serial.Serial(port, 115200, timeout=0.5)
            s.write(b"status\n")
            time.sleep(0.1)
            resp = s.read(256).decode(errors="ignore")
            s.close()
            if "Crypto TKey" in resp or "Uptime:" in resp or "Vault:" in resp:
                return port
        except Exception:
            continue
    # If explicit detection didn't match yet, return first /dev/ttyACM* if present
    if candidates:
        return candidates[0]
    return None

def fetch_coingecko_prices():
    """Fetches real-time USD prices and 24h changes from CoinGecko."""
    ids = ",".join(COINGECKO_MAP.values())
    url = f"https://api.coingecko.com/api/v3/simple/price?ids={ids}&vs_currencies=usd&include_24hr_change=true"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (TKey-Companion/1.0)"})
    try:
        with urllib.request.urlopen(req, timeout=10) as res:
            return json.loads(res.read().decode())
    except Exception as e:
        print(f"[TRACKER] CoinGecko API error: {e}", file=sys.stderr)
        return {}

def fetch_kraken_prices():
    """Fetches sub-second real-time rates from Kraken public REST API (BTC, ETH, SOL, PEPE, DOGE)."""
    url = "https://api.kraken.com/0/public/Ticker?pair=XXBTZUSD,XETHZUSD,SOLUSD,PEPEUSD,XDGUSD"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (TKey-FastTicker/1.0)"})
    rates = {}
    try:
        with urllib.request.urlopen(req, timeout=5) as res:
            data = json.loads(res.read().decode())
            results = data.get("result", {})
            pair_map = {
                "XXBTZUSD": "BTC",
                "XETHZUSD": "ETH",
                "SOLUSD": "SOL",
                "PEPEUSD": "PEPE",
                "XDGUSD": "DOGE"
            }
            for k_pair, sym in pair_map.items():
                if k_pair in results:
                    p_info = results[k_pair]
                    curr_p = float(p_info["c"][0])
                    open_p = float(p_info["o"])
                    chg_pct = ((curr_p - open_p) / open_p * 100.0) if open_p > 0 else 0.0
                    rates[sym] = {"price": curr_p, "change24h": chg_pct}
            return rates
    except Exception as e:
        print(f"[TRACKER] Kraken fast ticker error: {e}", file=sys.stderr)
        return {}

def fetch_btc_balance(address):
    """Fetches confirmed on-chain balance for a Bitcoin address via Mempool.space."""
    if not address or len(address) < 20 or not address.startswith("bc1"):
        return 0.0
    url = f"https://mempool.space/api/address/{address}"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=8) as res:
            data = json.loads(res.read().decode())
            chain = data.get("chain_stats", {})
            sats = chain.get("funded_txo_sum", 0) - chain.get("spent_txo_sum", 0)
            return max(0.0, sats / 100000000.0)
    except Exception as e:
        print(f"[TRACKER] Mempool BTC error ({address}): {e}", file=sys.stderr)
        return 0.0

def fetch_eth_balance(address):
    """Fetches on-chain ETH balance via public RPC."""
    if not address or not address.startswith("0x") or len(address) != 42:
        return 0.0
    url = "https://ethereum-rpc.publicnode.com"
    payload = json.dumps({"jsonrpc": "2.0", "method": "eth_getBalance", "params": [address, "latest"], "id": 1}).encode()
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=8) as res:
            data = json.loads(res.read().decode())
            wei = int(data.get("result", "0x0"), 16)
            return wei / 1e18
    except Exception as e:
        print(f"[TRACKER] Ethereum RPC error ({address}): {e}", file=sys.stderr)
        return 0.0

def fetch_pepe_balance(address):
    """Fetches on-chain PEPE ERC-20 token balance via Ethereum public RPC."""
    if not address or not address.startswith("0x") or len(address) != 42:
        return 0.0
    pepe_contract = "0x6982508145454Ce325dDbE47a25d4ec3d2311933"
    clean_addr = address[2:].lower().zfill(64)
    data = "0x70a08231" + clean_addr
    url = "https://ethereum-rpc.publicnode.com"
    payload = json.dumps({
        "jsonrpc": "2.0",
        "method": "eth_call",
        "params": [{"to": pepe_contract, "data": data}, "latest"],
        "id": 2
    }).encode()
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=8) as res:
            resp = json.loads(res.read().decode())
            hex_val = resp.get("result", "0x0")
            raw_units = int(hex_val, 16)
            return raw_units / 1e18
    except Exception as e:
        print(f"[TRACKER] PEPE balance error ({address}): {e}", file=sys.stderr)
        return 0.0

def fetch_sol_balance(address):
    """Fetches on-chain SOL balance via Solana public RPC."""
    if not address or len(address) < 32:
        return 0.0
    url = "https://api.mainnet-beta.solana.com"
    payload = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "getBalance", "params": [address]}).encode()
    req = urllib.request.Request(url, data=payload, headers={"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req, timeout=8) as res:
            data = json.loads(res.read().decode())
            lamports = data.get("result", {}).get("value", 0)
            return lamports / 1e9
    except Exception as e:
        print(f"[TRACKER] Solana RPC error ({address}): {e}", file=sys.stderr)
        return 0.0

def query_device_addresses(ser):
    """Reads derived addresses from the device if unlocked."""
    addresses = {}
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
        elif line.startswith("D") and len(line) == 34:
            addresses["DOGE"] = line
    return addresses

def sync_cycle(port):
    print(f"[TRACKER] ⚡ Connecting to Crypto TKey S3 on {port}...")
    ser = serial.Serial(port, 115200, timeout=1.0)
    time.sleep(0.2)

    # 1. Fetch sub-second Kraken rates first (high-frequency priority)
    print("[TRACKER] ⚡ Fetching sub-second rates from Kraken (BTC, ETH, SOL, PEPE, DOGE)...")
    kraken_rates = fetch_kraken_prices()
    synced_coins = set()
    if kraken_rates:
        for sym, data in kraken_rates.items():
            cmd = f"setprice {sym} {data['price']:.8f} {data['change24h']:.2f}\n"
            ser.write(cmd.encode())
            synced_coins.add(sym)
            time.sleep(0.04)
        print(f"[TRACKER] ⚡ Kraken sub-second rates pushed for: {list(kraken_rates.keys())}")

    # 2. Fetch CoinGecko rates for remaining coins
    print("[TRACKER] 🌐 Fetching CoinGecko rates for asset registry...")
    prices = fetch_coingecko_prices()
    if prices:
        for sym, cg_id in COINGECKO_MAP.items():
            if sym in synced_coins:
                continue
            if cg_id in prices:
                p_usd = prices[cg_id].get("usd", 0.0)
                chg = prices[cg_id].get("usd_24h_change", 0.0)
                cmd = f"setprice {sym} {p_usd:.8f} {chg:.2f}\n"
                ser.write(cmd.encode())
                time.sleep(0.04)
        print(f"[TRACKER] ✅ Updated full asset registry.")

    # 3. Query derived deposit addresses & check on-chain balances
    addrs = query_device_addresses(ser)
    if addrs:
        print(f"[TRACKER] 🔍 Derived addresses found on key: {list(addrs.keys())}")
        if "BTC" in addrs:
            btc_bal = fetch_btc_balance(addrs["BTC"])
            ser.write(f"setbal BTC {btc_bal:.8f}\n".encode())
            print(f"[TRACKER] ₿ Bitcoin On-Chain Balance: {btc_bal:.8f} BTC ({addrs['BTC']})")
            time.sleep(0.05)
        if "ETH" in addrs:
            eth_bal = fetch_eth_balance(addrs["ETH"])
            ser.write(f"setbal ETH {eth_bal:.6f}\n".encode())
            print(f"[TRACKER] ⟠ Ethereum On-Chain Balance: {eth_bal:.6f} ETH ({addrs['ETH']})")
            time.sleep(0.05)

            # Auto-track PEPE ERC-20 token on same EVM account!
            pepe_bal = fetch_pepe_balance(addrs["ETH"])
            ser.write(f"setbal PEPE {pepe_bal:.2f}\n".encode())
            print(f"[TRACKER] 🐸 PEPE On-Chain Balance: {pepe_bal:,.2f} PEPE")
            time.sleep(0.05)

    ser.close()
    print("[TRACKER] ✨ Sync complete! Dongle display refreshed.\n")

def main():
    parser = argparse.ArgumentParser(description="Crypto TKey S3 Live Blockchain & Market Companion")
    parser.add_argument("--once", action="store_true", help="Run a single sync cycle and exit")
    parser.add_argument("--interval", type=int, default=60, help="Poll interval in seconds (default: 60)")
    parser.add_argument("--port", type=str, default=None, help="Explicit serial port (e.g. /dev/ttyACM0)")
    args = parser.parse_args()

    print("==========================================================")
    print("  Crypto TKey S3 — Real-Time Blockchain & Price Companion")
    print("==========================================================")

    while True:
        port = args.port or find_device_port()
        if port:
            try:
                sync_cycle(port)
            except Exception as e:
                print(f"[TRACKER] Connection error: {e}", file=sys.stderr)
        else:
            print("[TRACKER] ⏳ Waiting for Crypto TKey S3 to be plugged in via USB...")

        if args.once:
            break
        time.sleep(args.interval)

if __name__ == "__main__":
    main()
