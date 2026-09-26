#!/usr/bin/env python3
"""
test_clearsign_suite.py — Automated Clear-Signing Verification Suite
===================================================================
Rigorous test suite for Crypto TKey S3 on-device clear-signing engine.
Tests:
  1. EIP-155 Legacy EVM Clear-Signing (Type 0)
  2. EIP-1559 Modern EVM Clear-Signing (Type 2)
  3. Canonical ERC-20 PEPE Token Transfer Calldata Parsing
  4. Unlimited Approval Drainer Warning Detection (0xfff... allowance)
  5. Zero-Serial Signing Invariant (verifies production rejection of remote sign triggers)

Usage:
  python3 tools/test_clearsign_suite.py [--port /dev/ttyACM*]
"""

import argparse
import glob
import os
import sys
import time

try:
    import serial
except ImportError:
    serial = None

PEPE_CONTRACT = bytes.fromhex("6982508145454ce325ddbe47a25d4ec3d2311933")
VITALIK_ADDR  = bytes.fromhex("d8da6bf26964af9d7eed9e03e53415d37aa96045")
DRAINER_SPENDER = bytes.fromhex("deadbeefdeadbeefdeadbeefdeadbeefdeadbeef")

# RLP Encoding Helpers
def rlp_bytes(b: bytes) -> bytes:
    if len(b) == 1 and b[0] < 0x80:
        return b
    elif len(b) <= 55:
        return bytes([0x80 + len(b)]) + b
    else:
        len_b = len(b).to_bytes((len(b).bit_length() + 7) // 8, 'big')
        return bytes([0xB7 + len(len_b)]) + len_b + b

def rlp_int(n: int) -> bytes:
    if n == 0:
        return b'\x80'
    b = n.to_bytes((n.bit_length() + 7) // 8, 'big')
    return rlp_bytes(b)

def rlp_list(items: list) -> bytes:
    payload = b''.join(items)
    if len(payload) <= 55:
        return bytes([0xC0 + len(payload)]) + payload
    else:
        len_b = len(payload).to_bytes((len(payload).bit_length() + 7) // 8, 'big')
        return bytes([0xF7 + len(len_b)]) + len_b + payload

def gen_pepe_transfer_tx(amount_tokens: int = 1_000_000) -> str:
    """Generate EIP-1559 transfer of PEPE."""
    selector = bytes.fromhex("a9059cbb")
    to_param = VITALIK_ADDR.rjust(32, b"\x00")
    val_param = (amount_tokens * (10**18)).to_bytes(32, "big")
    calldata = selector + to_param + val_param

    items = [
        rlp_int(1),                      # chainId (Ethereum Mainnet)
        rlp_int(15),                     # nonce
        rlp_int(2 * 10**9),              # maxPriorityFeePerGas (2 Gwei)
        rlp_int(28 * 10**9),             # maxFeePerGas (28 Gwei)
        rlp_int(65000),                  # gasLimit
        rlp_bytes(PEPE_CONTRACT),        # to
        rlp_int(0),                      # value (0 ETH)
        rlp_bytes(calldata),             # calldata
        rlp_list([])                     # accessList
    ]
    return (bytes([0x02]) + rlp_list(items)).hex()

def gen_unlimited_approve_tx() -> str:
    """Generate dangerous unlimited ERC-20 approval transaction."""
    selector = bytes.fromhex("095ea7b3")
    spender_param = DRAINER_SPENDER.rjust(32, b"\x00")
    unlimited_val = (2**256 - 1).to_bytes(32, "big")
    calldata = selector + spender_param + unlimited_val

    items = [
        rlp_int(1),
        rlp_int(16),
        rlp_int(2 * 10**9),
        rlp_int(25 * 10**9),
        rlp_int(50000),
        rlp_bytes(PEPE_CONTRACT),
        rlp_int(0),
        rlp_bytes(calldata),
        rlp_list([])
    ]
    return (bytes([0x02]) + rlp_list(items)).hex()

def gen_legacy_eth_tx(amount_eth: float = 0.025) -> str:
    """Generate legacy Type 0 ETH transfer."""
    val_wei = int(amount_eth * 10**18)
    items = [
        rlp_int(7),                      # nonce
        rlp_int(20 * 10**9),             # gasPrice
        rlp_int(21000),                  # gasLimit
        rlp_bytes(VITALIK_ADDR),         # to
        rlp_int(val_wei),                # value
        rlp_bytes(b""),                  # empty data
        rlp_int(1),                      # EIP-155 chainId
        rlp_int(0), rlp_int(0)
    ]
    return rlp_list(items).hex()

def run_test_suite(port: str = None):
    target_port = port or sorted(glob.glob("/dev/ttyACM*"))[-1]
    print(f"============================================================")
    print(f" Crypto TKey S3 — Clear-Signing Automated Test Suite")
    print(f" Port: {target_port}")
    print(f"============================================================\n")

    ser = serial.Serial(target_port, 115200, timeout=1.0)
    time.sleep(0.2)

    passed = 0
    total = 0

    def test_cmd(name: str, command: str, expected_substring: str) -> bool:
        nonlocal passed, total
        total += 1
        ser.reset_input_buffer()
        ser.write((command + "\n").encode())
        
        t0 = time.time()
        buf = ""
        while time.time() - t0 < 3.0:
            c = ser.read(1024).decode(errors="replace")
            if c:
                buf += c
                if expected_substring in buf:
                    break
            time.sleep(0.02)
        
        ok = expected_substring in buf
        status = "✅ PASS" if ok else "❌ FAIL"
        print(f"  {status}  {name}")
        if not ok:
            print(f"         Expected: {expected_substring}")
            print(f"         Output: {buf.strip()[:200]}")
        else:
            passed += 1
        return ok

    # Test 1: EIP-1559 PEPE Transfer Decoding
    pepe_hex = gen_pepe_transfer_tx(2_500_000)
    test_cmd("1. Decode EIP-1559 PEPE Transfer", f"decode_evm {pepe_hex}", "SEND PEPE")

    # Test 2: Unlimited Approval Drainer Warning
    drainer_hex = gen_unlimited_approve_tx()
    test_cmd("2. Detect Unlimited Allowance Drainer", f"decode_evm {drainer_hex}", "HIGH RISK: UNLIMITED TOKEN ALLOWANCE DRAINER DETECTED!")

    # Test 3: Legacy ETH Transfer Decoding
    eth_hex = gen_legacy_eth_tx(0.042)
    test_cmd("3. Decode Legacy Type-0 ETH Transfer", f"decode_evm {eth_hex}", "SEND ETH")

    # Test 4: Serial Signing Rejection in Production
    test_cmd("4. Reject Remote Serial Signing (Zero-Bypass)", f"sign_evm {eth_hex}", "Serial signing is disabled; review and sign on the device.")

    ser.close()
    print(f"\nResult: {passed}/{total} tests passed.")
    return passed == total

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="Serial port to test")
    args = parser.parse_args()
    success = run_test_suite(args.port)
    sys.exit(0 if success else 1)
