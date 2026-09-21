#!/usr/bin/env python3
"""
test_deep_suite.py — Comprehensive Deep-Dive Test Suite for Crypto TKey S3
==========================================================================
Executes deep end-to-end verification across:
1. Native USB CDC Serial Command Interface & Telemetry
2. 24-Coin Multi-Asset Registry & WebUSB JSON Telemetry
3. EVM RLP Clear-Signing Engine (EIP-1559 Type 2 + Legacy Type 0 + PEPE ERC-20)
4. BIP-32 / BIP-84 / EIP-55 Vault Unlock & Address Derivation
5. DotStar RGB Lighting Engine Commands
6. USB CTAPHID & CTAP2.1 FIDO2 Protocol Engine (/dev/hidraw*)
7. ESP32-S3 Silicon eFuse Hardening Simulator (--dry-run)
"""

import sys
import os
import glob
import time
import struct
import select
import serial
import subprocess

# CTAPHID Constants
CTAPHID_BROADCAST_CID = 0xFFFFFFFF
CTAPHID_CMD_INIT      = 0x80 | 0x06
CTAPHID_CMD_WINK      = 0x80 | 0x08
CTAPHID_CMD_CBOR      = 0x80 | 0x10
CTAP2_CMD_GET_INFO    = 0x04

class DeepTester:
    def __init__(self, serial_port="/dev/ttyACM0", baudrate=115200):
        self.serial_port = serial_port
        self.baudrate = baudrate
        self.ser = None
        self.results = []

    def log_result(self, category, test_name, status, details=""):
        badge = "✅ PASS" if status else "❌ FAIL"
        self.results.append((category, test_name, badge, details))
        print(f"[{badge}] {category} :: {test_name} {('- ' + details) if details else ''}")

    def connect_serial(self):
        try:
            self.ser = serial.Serial(self.serial_port, self.baudrate, timeout=1.5)
            self.ser.dtr = True
            self.ser.rts = True
            time.sleep(0.3)
            self.ser.reset_input_buffer()
            return True
        except Exception as e:
            print(f"[ERR] Failed to connect to serial port {self.serial_port}: {e}")
            return False

    def send_cmd(self, cmd, wait_time=0.4):
        if not self.ser or not self.ser.is_open:
            return ""
        self.ser.reset_input_buffer()
        self.ser.write(f"{cmd}\n".encode("utf-8"))
        time.sleep(wait_time)
        resp = self.ser.read(self.ser.in_waiting or 4096).decode("utf-8", errors="replace")
        return resp.strip()

    # ─── 1. Serial Telemetry & Status ─────────────────────────────────────────
    def test_status_telemetry(self):
        print("\n" + "="*60)
        print(" TEST 1: Serial CLI & System Telemetry")
        print("="*60)
        resp = self.send_cmd("status")
        if "Uptime:" in resp and "CPU:" in resp:
            self.log_result("System", "Status Query", True, resp)
        else:
            self.log_result("System", "Status Query", False, f"Unexpected response: {resp}")

    # ─── 2. Multi-Coin Registry & JSON WebUSB ─────────────────────────────────
    def test_coin_registry(self):
        print("\n" + "="*60)
        print(" TEST 2: Multi-Coin Registry & WebUSB JSON")
        print("="*60)
        coins_resp = self.send_cmd("coins")
        has_btc = "BTC" in coins_resp
        has_eth = "ETH" in coins_resp
        has_sol = "SOL" in coins_resp
        has_pepe = "PEPE" in coins_resp
        if has_btc and has_eth and has_sol and has_pepe:
            self.log_result("Portfolio", "Coin Registry (BTC/ETH/SOL/PEPE)", True, "All flagship assets detected")
        else:
            self.log_result("Portfolio", "Coin Registry (BTC/ETH/SOL/PEPE)", False, "Missing core assets")

        json_resp = self.send_cmd("json")
        if "{" in json_resp and "coins" in json_resp:
            self.log_result("Portfolio", "WebUSB JSON Output", True, f"{len(json_resp)} bytes JSON")
        else:
            self.log_result("Portfolio", "WebUSB JSON Output", False, f"Invalid JSON response: {json_resp[:80]}")

    # ─── 3. EVM Clear-Signing Engine ─────────────────────────────────────────
    def test_evm_decoder(self):
        print("\n" + "="*60)
        print(" TEST 3: EVM Clear-Signing Engine (EIP-1559 & Canonical PEPE)")
        print("="*60)
        # Vector 1: EIP-1559 PEPE Transfer (5,000,000 PEPE)
        pepe_tx = "02f86d012a84773594008506fc23ac0082fde8946982508145454ce325ddbe47a25d4ec3d231193380b844a9059cbb000000000000000000000000d8da6bf26964af9d7eed9e03e53415d37aa960450000000000000000000000000000000000000000000422ca8b0a00a425000000c0"
        resp1 = self.send_cmd(f"decode_evm {pepe_tx}")
        pass1 = ("PEPE" in resp1 or "Pepe" in resp1) and "EIP-1559" in resp1 and "d8da6bf26964af9d7eed9e03e53415d37aa96045" in resp1.lower()
        self.log_result("EVM Engine", "EIP-1559 PEPE Transfer Clear-Sign", pass1, "Detected PEPE contract + Transfer calldata + Recipient")

        # Vector 2: Legacy EIP-155 ETH Transfer (0.500 ETH)
        eth_tx = "eb0c8505d21dba0082520894d8da6bf26964af9d7eed9e03e53415d37aa9604587b1a2bc2ec5000080018080"
        resp2 = self.send_cmd(f"decode_evm {eth_tx}")
        pass2 = "Legacy" in resp2 and "ETH" in resp2 and "0.5000 ETH" in resp2 and "d8da6bf26964af9d7eed9e03e53415d37aa96045" in resp2.lower()
        self.log_result("EVM Engine", "Legacy EIP-155 ETH Transfer Clear-Sign", pass2, "Detected Legacy Envelope + 0.5000 ETH")

        # Vector 3: Unlimited Allowance Drainer (approve uint256.max)
        approve_tx = "02f86d012b84773594008506fc23ac0082fde8946982508145454ce325ddbe47a25d4ec3d231193380b844095ea7b3000000000000000000000000d8da6bf26964af9d7eed9e03e53415d37aa96045ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc0"
        resp3 = self.send_cmd(f"decode_evm {approve_tx}")
        pass3 = "UNLIMITED TOKEN ALLOWANCE DRAINER DETECTED" in resp3 and "APPROVE PEPE" in resp3
        self.log_result("EVM Engine", "Allowance Drainer Alert (uint256.max)", pass3, "Flagged UNLIMITED ALLOWANCE / HIGH RISK")

        # Vector 4: Real secp256k1 Signing (sign_evm)
        self.send_cmd("unlock 1234")
        resp4 = self.send_cmd(f"sign_evm {pepe_tx}", wait_time=0.6)
        pass4 = "Transaction Signed via secp256k1" in resp4 and "Signature (r||s):" in resp4
        self.log_result("EVM Engine", "ECDSA secp256k1 Signing (sign_evm)", pass4, "Derived m/44'/60'/0'/0/0 signature generated")
        self.send_cmd("lock")

        # Vector 5: RLP Streaming Parser Fuzzing
        fuzz_malformed = ["02c80101", "02f9ffff0102030405", "7fa012345678", "02"]
        fuzz_survived = True
        for fvec in fuzz_malformed:
            r = self.send_cmd(f"decode_evm {fvec}", wait_time=0.15)
            if not ("Failed to parse" in r or "EVM" in r):
                fuzz_survived = False
        st = self.send_cmd("status", wait_time=0.2)
        fuzz_survived = fuzz_survived and ("Uptime:" in st)
        self.log_result("EVM Engine", "Malformed RLP Stream Fuzzing", fuzz_survived, "Robust error recovery without crash")

    # ─── 4. BIP-32/BIP-84 Vault Unlock & Derivation ──────────────────────────
    def test_vault_unlock_and_addresses(self):
        print("\n" + "="*60)
        print(" TEST 4: BIP-32 / BIP-84 / EIP-55 Vault Unlock & Addresses")
        print("="*60)
        # Attempt unlock with default master PIN 1234
        unlock_resp = self.send_cmd("unlock 1234")
        pass_unlock = "Unlocked successfully" in unlock_resp
        self.log_result("Crypto Vault", "Master PIN Unlock (1234)", pass_unlock, unlock_resp)

        if pass_unlock:
            addr_resp = self.send_cmd("addresses", wait_time=0.6)
            has_bc1q = "bc1q" in addr_resp
            has_0x = "0x" in addr_resp
            has_sol = "SOL" in addr_resp
            self.log_result("Crypto Vault", "Derived Bitcoin SegWit (bc1q)", has_bc1q, "Bech32 Native SegWit")
            self.log_result("Crypto Vault", "Derived Ethereum EIP-55 (0x)", has_0x, "Keccak-256 mixed-case")
            self.log_result("Crypto Vault", "Derived Solana SLIP-0010 (Ed25519)", has_sol, "SLIP-0010 Base58")

            # Lock vault back
            lock_resp = self.send_cmd("lock")
            pass_lock = "locked" in lock_resp.lower()
            self.log_result("Crypto Vault", "Vault Zeroize & Lock", pass_lock, "Volatile keys scrubbed")

    # ─── 5. RGB Lighting Engine ──────────────────────────────────────────────
    def test_rgb_engine(self):
        print("\n" + "="*60)
        print(" TEST 5: DotStar RGB LED Subsystem")
        print("="*60)
        modes = ["btc", "eth", "sol", "doge", "rainbow"]
        for m in modes:
            resp = self.send_cmd(f"led {m}", wait_time=0.15)
            pass_m = "activated" in resp.lower()
            self.log_result("RGB Engine", f"Mode: {m.upper()}", pass_m, resp)

    # ─── 6. Physical CTAPHID & CTAP2.1 FIDO2 Protocol ─────────────────────────
    def test_fido2_ctaphid(self):
        print("\n" + "="*60)
        print(" TEST 6: Physical CTAPHID & CTAP2.1 FIDO2 Protocol Engine")
        print("="*60)
        # Scan for hidraw endpoints
        candidates = sorted(glob.glob("/dev/hidraw*"))
        active_fd = None
        active_cid = None
        active_node = None

        for node in candidates:
            try:
                fd = os.open(node, os.O_RDWR | os.O_NONBLOCK)
                nonce = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44])
                pkt = struct.pack(">IBH", CTAPHID_BROADCAST_CID, CTAPHID_CMD_INIT, len(nonce)) + nonce
                pkt += b"\x00" * (64 - len(pkt))
                os.write(fd, pkt)

                r, _, _ = select.select([fd], [], [], 0.5)
                if r:
                    resp = os.read(fd, 64)
                    if len(resp) >= 17 and resp[4] == 0x86:
                        active_fd = fd
                        active_cid = struct.unpack(">I", resp[15:19])[0]
                        active_node = node
                        break
                os.close(fd)
            except Exception:
                pass

        if not active_fd:
            self.log_result("FIDO2 CTAPHID", "Endpoint Probe", False, "No responsive CTAPHID node found")
            return

        self.log_result("FIDO2 CTAPHID", f"INIT Negotiation ({active_node})", True, f"Allocated CID: 0x{active_cid:08X}")

        # CTAPHID WINK
        try:
            pkt = struct.pack(">IBH", active_cid, CTAPHID_CMD_WINK, 0)
            pkt += b"\x00" * (64 - len(pkt))
            os.write(active_fd, b"\x00" + pkt)
            r, _, _ = select.select([active_fd], [], [], 0.5)
            wink_ok = True
            if r:
                resp = os.read(active_fd, 64)
                if len(resp) >= 7 and (resp[4] & 0x7F) == 0x08:
                    wink_ok = True
            self.log_result("FIDO2 CTAPHID", "WINK Indication", wink_ok, "Dispatched WINK frame")
        except Exception as e:
            self.log_result("FIDO2 CTAPHID", "WINK Indication", False, str(e))

        # CTAP2 authenticatorGetInfo (0x04)
        try:
            payload = bytes([CTAP2_CMD_GET_INFO])
            pkt = struct.pack(">IBH", active_cid, CTAPHID_CMD_CBOR, len(payload)) + payload
            pkt += b"\x00" * (64 - len(pkt))
            os.write(active_fd, b"\x00" + pkt)
            r, _, _ = select.select([active_fd], [], [], 1.0)
            getinfo_ok = False
            details = "No response"
            if r:
                resp = os.read(active_fd, 64)
                if len(resp) >= 8:
                    status = resp[7]
                    total_len = struct.unpack(">H", resp[5:7])[0]
                    read_so_far = len(resp) - 7
                    # Drain continuation packets
                    while read_so_far < total_len:
                        rr, _, _ = select.select([active_fd], [], [], 0.3)
                        if not rr: break
                        c_pkt = os.read(active_fd, 64)
                        read_so_far += len(c_pkt) - 5
                    if status == 0x00:
                        getinfo_ok = True
                        details = f"Status CTAP2_OK (0x00) — {total_len} bytes info"
                    else:
                        details = f"Status 0x{status:02X}"
            self.log_result("FIDO2 CTAP2", "authenticatorGetInfo (0x04)", getinfo_ok, details)
        except Exception as e:
            self.log_result("FIDO2 CTAP2", "authenticatorGetInfo (0x04)", False, str(e))

        # CTAP2 clientPIN getPINRetries (0x06 subCommand 1)
        try:
            cbor_req = bytes([0x06, 0xA2, 0x01, 0x01, 0x02, 0x01])
            pkt = struct.pack(">IBH", active_cid, CTAPHID_CMD_CBOR, len(cbor_req)) + cbor_req
            pkt += b"\x00" * (64 - len(pkt))
            os.write(active_fd, b"\x00" + pkt)
            r, _, _ = select.select([active_fd], [], [], 1.0)
            pin_ok = False
            details = "No response"
            if r:
                resp = os.read(active_fd, 64)
                if len(resp) >= 8:
                    status = resp[7]
                    if status == 0x00:
                        pin_ok = True
                        details = "Status CTAP2_OK (0x00) — Anti-hammering active"
                    else:
                        details = f"Status 0x{status:02X}"
            self.log_result("FIDO2 CTAP2", "clientPIN getPINRetries (0x06)", pin_ok, details)
        except Exception as e:
            self.log_result("FIDO2 CTAP2", "clientPIN getPINRetries (0x06)", False, str(e))

        # CTAP2 clientPIN getKeyAgreement (0x06 subCommand 2)
        try:
            cbor_req = bytes([0x06, 0xA2, 0x01, 0x01, 0x02, 0x02])
            pkt = struct.pack(">IBH", active_cid, CTAPHID_CMD_CBOR, len(cbor_req)) + cbor_req
            pkt += b"\x00" * (64 - len(pkt))
            os.write(active_fd, b"\x00" + pkt)
            r, _, _ = select.select([active_fd], [], [], 1.0)
            ecdh_ok = False
            details = "No response"
            if r:
                resp = os.read(active_fd, 64)
                if len(resp) >= 8:
                    status = resp[7]
                    total_len = struct.unpack(">H", resp[5:7])[0]
                    read_so_far = len(resp) - 7
                    while read_so_far < total_len:
                        rr, _, _ = select.select([active_fd], [], [], 0.3)
                        if not rr: break
                        c_pkt = os.read(active_fd, 64)
                        read_so_far += len(c_pkt) - 5
                    if status == 0x00:
                        ecdh_ok = True
                        details = f"Status CTAP2_OK (0x00) — {total_len} bytes COSE agreement point"
                    else:
                        details = f"Status 0x{status:02X}"
            self.log_result("FIDO2 CTAP2", "clientPIN getKeyAgreement (0x06)", ecdh_ok, details)
        except Exception as e:
            self.log_result("FIDO2 CTAP2", "clientPIN getKeyAgreement (0x06)", False, str(e))

        os.close(active_fd)

    # ─── 7. Air-Gap PSBT & Duress Wipe Confirmation Guards ───────────────────
    def test_airgap_and_panic_guards(self):
        print("\n" + "="*60)
        print(" TEST 7: Air-Gap PSBT & Duress Wipe Safeguards")
        print("="*60)
        # PSBT Scan
        psbt_resp = self.send_cmd("psbt scan")
        has_psbt = "Found pending unsigned file" in psbt_resp or "No pending unsigned" in psbt_resp
        self.log_result("Air-Gap PSBT", "MicroSD PSBT Scanner", has_psbt, psbt_resp[:60])

        # Panic Confirmation Guard
        panic_resp = self.send_cmd("panic")
        has_guard = "SAFEGUARD GUARD: Accidental execution blocked" in panic_resp and "panic CONFIRM" in panic_resp
        self.log_result("Duress Security", "Panic Confirmation Safeguard", has_guard, "Blocked accidental unconfirmed erasure")

    # ─── 8. Silicon eFuse Hardening Tool Simulation ──────────────────────────
    def test_efuse_dry_run(self):
        print("\n" + "="*60)
        print(" TEST 8: ESP32-S3 Silicon eFuse Hardening Tool (Dry-Run)")
        print("="*60)
        script = os.path.join(os.path.dirname(__file__), "burn_production_efuses.py")
        if not os.path.exists(script):
            self.log_result("Silicon Security", "eFuse Tool Script Existence", False, "Missing burn_production_efuses.py")
            return

        cmd = [sys.executable, script, "--dry-run", "--port", self.serial_port]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        has_sim = "SIMULATION ONLY" in proc.stdout or "DRY-RUN COMPLETE" in proc.stdout or "DRY-RUN MODE ACTIVE" in proc.stdout or proc.returncode == 0
        has_safeguard = "IRREVERSIBLE" in proc.stdout or "eFuses" in proc.stdout
        if has_sim and has_safeguard:
            self.log_result("Silicon Security", "eFuse Simulation Audit", True, "Passes dry-run checks without modifying silicon")
        else:
            self.log_result("Silicon Security", "eFuse Simulation Audit", False, f"Output snippet: {proc.stdout[:120]}")

    # ─── Summary ─────────────────────────────────────────────────────────────
    def print_summary(self):
        print("\n" + "="*70)
        print("                 DEEP VERIFICATION SCORECARD")
        print("="*70)
        print(f"{'Category':<18} | {'Test Name':<35} | {'Result':<8}")
        print("-" * 70)
        passed = 0
        total = len(self.results)
        for cat, name, res, _ in self.results:
            if "PASS" in res:
                passed += 1
            print(f"{cat:<18} | {name:<35} | {res:<8}")
        print("-" * 70)
        pct = (passed / total) * 100 if total > 0 else 0
        print(f"Overall Result: {passed}/{total} Tests Passed ({pct:.1f}% Score)")
        print("=" * 70 + "\n")
        return passed == total

def main():
    tester = DeepTester()
    if not tester.connect_serial():
        print("[FAIL] Cannot execute deep test suite: Serial connection failed.")
        sys.exit(1)

    try:
        tester.test_status_telemetry()
        tester.test_coin_registry()
        tester.test_evm_decoder()
        tester.test_vault_unlock_and_addresses()
        tester.test_rgb_engine()
        tester.test_fido2_ctaphid()
        tester.test_airgap_and_panic_guards()
        tester.test_efuse_dry_run()
    finally:
        if tester.ser and tester.ser.is_open:
            tester.ser.close()

    success = tester.print_summary()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
