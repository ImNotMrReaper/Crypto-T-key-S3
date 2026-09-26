#!/usr/bin/env python3
"""Generate independent PSBT fixtures using embit and pinned BIP-174 vectors."""
from __future__ import annotations

import base64
import hashlib
import html
import re
import urllib.request
from pathlib import Path

from embit import bip32, bip39, psbt, script, transaction
from embit.networks import NETWORKS
from embit.ec import Signature

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/host/psbt_fixtures.h"
BIP174_COMMIT = "8c4d9e258b8cec913ffe4bddeb1d96e9f0c01ae5"
BIP174_URL = f"https://raw.githubusercontent.com/bitcoin/bips/{BIP174_COMMIT}/bip-0174.mediawiki"
MNEMONIC = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"
SEED = bip39.mnemonic_to_seed(MNEMONIC)
ROOT_KEY = bip32.HDKey.from_seed(SEED)
FP = ROOT_KEY.my_fingerprint
ACCOUNT = ROOT_KEY.derive("m/84h/0h/0h")
RECEIVE = ACCOUNT.derive("0/0")
CHANGE = ACCOUNT.derive("1/0")
RECEIVE3 = ACCOUNT.derive("0/3")


def child(path: str):
    return ROOT_KEY.derive(path)


def der_path(path: str):
    parts = path.split("/")[1:]
    result = []
    for item in parts:
        hard = item.endswith("'") or item.endswith("h")
        n = int(item[:-1] if hard else item)
        result.append(n | (0x80000000 if hard else 0))
    return result


def derivation(path: str):
    return psbt.DerivationPath(FP, der_path(path))


def p2wpkh(key):
    return script.p2wpkh(key.get_public_key())


def p2pkh(key):
    return script.p2pkh(key.get_public_key())


def p2tr(key):
    return script.p2tr(key.get_public_key())


def txid(tag: int) -> bytes:
    return hashlib.sha256(f"codex-psbt-fixture-{tag}".encode()).digest()


def new_psbt(inputs, outputs):
    tx = transaction.Transaction(
        version=2,
        vin=[transaction.TransactionInput(txid(tag), vout) for tag, vout, _value, _spk, _meta in inputs],
        vout=[transaction.TransactionOutput(value, spk) for value, spk, _meta in outputs],
        locktime=0,
    )
    result = psbt.PSBT(tx)
    for scope, (_tag, _vout, value, spk, meta) in zip(result.inputs, inputs):
        scope.witness_utxo = transaction.TransactionOutput(value, spk)
        if meta:
            pub, path = meta
            scope.bip32_derivations[pub] = derivation(path)
    for scope, (_value, _spk, meta) in zip(result.outputs, outputs):
        if meta:
            pub, path = meta
            scope.bip32_derivations[pub] = derivation(path)
    return result


def serialize(p):
    return p.serialize()


def addr(spk, network="main"):
    return spk.address(network=NETWORKS[network])


def hexstr(b):
    return b.hex()


def bip174_vectors():
    request = urllib.request.Request(BIP174_URL, headers={"User-Agent": "psbt-fixture-generator/1.0"})
    source = urllib.request.urlopen(request, timeout=30).read().decode("utf-8")
    section = source.split("==Test Vectors==", 1)[1]
    next_heading = re.search(r"(?m)^==[^\n]*==\s*$", section)
    if next_heading:
        section = section[:next_heading.start()]
    cases = []
    valid_start = section.find("The following are valid PSBTs:")
    chunks = list(re.finditer(r"(?m)^\* Case: ", section))
    for index, match in enumerate(chunks):
        start = match.end()
        end = chunks[index + 1].start() if index + 1 < len(chunks) else len(section)
        chunk = section[start:end]
        name = html.unescape(chunk.splitlines()[0].strip())
        is_valid = match.start() > valid_start
        hx = re.search(r"\*\* Bytes in Hex: <pre>(.*?)</pre>", chunk, re.S)
        b64 = re.search(r"\*\* Base64 String: <pre>(.*?)</pre>", chunk, re.S)
        if not hx or not b64:
            continue
        raw_hex = re.sub(r"\s+", "", html.unescape(hx.group(1)))
        raw_b64 = re.sub(r"\s+", "", html.unescape(b64.group(1)))
        try:
            data = bytes.fromhex(raw_hex)
            if base64.b64decode(raw_b64, validate=True) != data:
                raise ValueError("hex/base64 mismatch")
        except (ValueError, base64.binascii.Error) as exc:
            raise RuntimeError(f"invalid BIP-174 source vector {name}: {exc}") from exc
        cases.append((name, data, is_valid))
    if not cases:
        raise RuntimeError("no BIP-174 vectors extracted")
    return cases


def fixture_data():
    foreign = bip32.HDKey.from_seed(bytes.fromhex("01" * 32))
    external_a = bip32.HDKey.from_seed(bytes.fromhex("02" * 32)).derive("m/0/0")
    external_b = bip32.HDKey.from_seed(bytes.fromhex("03" * 32)).derive("m/0/0")

    f1 = new_psbt(
        [(1, 0, 100000, p2wpkh(RECEIVE), (RECEIVE.get_public_key(), "m/84h/0h/0h/0/0"))],
        [(60000, p2wpkh(external_a), None), (39000, p2wpkh(CHANGE), (CHANGE.get_public_key(), "m/84h/0h/0h/1/0"))],
    )
    f1_hash = f1.sighash(0, sighash=1)
    f1_signed = psbt.PSBT.parse(serialize(f1))
    signed_count = f1_signed.sign_with(ROOT_KEY, sighash=1)
    if signed_count != 1:
        raise RuntimeError(f"expected one embit signature, got {signed_count}")
    f1_sig = next(iter(f1_signed.inputs[0].partial_sigs.values()))
    if f1_sig[-1] != 1 or not RECEIVE.get_public_key().verify(
        Signature.parse(f1_sig[:-1]), f1_hash
    ):
        raise RuntimeError("embit F6 signature does not verify against the F1 sighash")

    f2 = new_psbt(
        [
            (2, 1, 70000, p2wpkh(RECEIVE), (RECEIVE.get_public_key(), "m/84h/0h/0h/0/0")),
            (3, 0, 50000, p2wpkh(CHANGE), (CHANGE.get_public_key(), "m/84h/0h/0h/1/0")),
        ],
        [
            (40000, p2tr(external_a), None),
            (30000, p2pkh(external_b), None),
            (49000, p2wpkh(CHANGE), (CHANGE.get_public_key(), "m/84h/0h/0h/1/0")),
        ],
    )
    f3 = new_psbt(
        [
            (4, 0, 100000, p2wpkh(RECEIVE), (RECEIVE.get_public_key(), "m/84h/0h/0h/0/0")),
            (5, 2, 80000, p2wpkh(foreign), None),
        ],
        [(175000, p2wpkh(external_a), None)],
    )
    # A claimed wallet derivation that does not match the output script.
    wrong_change = new_psbt(
        [(6, 0, 100000, p2wpkh(RECEIVE), (RECEIVE.get_public_key(), "m/84h/0h/0h/0/0"))],
        [(99000, p2wpkh(external_b), (child("m/84h/0h/0h/1/5").get_public_key(), "m/84h/0h/0h/1/5"))],
    )
    f5 = new_psbt(
        [(7, 0, 100000, p2wpkh(RECEIVE), (RECEIVE.get_public_key(), "m/84h/0h/0h/0/0"))],
        [(99000, p2wpkh(RECEIVE3), (RECEIVE3.get_public_key(), "m/84h/0h/0h/0/3"))],
    )

    receive_addr = addr(p2wpkh(RECEIVE))
    if receive_addr != "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu":
        raise RuntimeError(f"BIP-84 receive address mismatch: {receive_addr}")
    return {
        "F0_MASTER_FINGERPRINT": FP,
        "F0_RECEIVE_ADDRESS": receive_addr,
        "F0_RECEIVE_PUBKEY": RECEIVE.get_public_key().sec(),
        "F0_CHANGE_ADDRESS": addr(p2wpkh(CHANGE)),
        "F0_CHANGE_PUBKEY": CHANGE.get_public_key().sec(),
        "F1_PSBT": serialize(f1),
        "F1_EXTERNAL_ADDRESS": addr(p2wpkh(external_a)),
        "F1_EXTERNAL_COUNT": 1,
        "F1_EXTERNAL_SAT": 60000,
        "F1_CHANGE_SAT": 39000,
        "F1_FEE_SAT": 1000,
        "F1_SIGHASH": f1_hash,
        "F2_PSBT": serialize(f2),
        "F2_EXTERNAL_P2TR": addr(p2tr(external_a)),
        "F2_EXTERNAL_P2PKH": addr(p2pkh(external_b)),
        "F2_EXTERNAL_COUNT": 2,
        "F2_EXTERNAL_P2TR_SAT": 40000,
        "F2_EXTERNAL_P2PKH_SAT": 30000,
        "F2_CHANGE_SAT": 49000,
        "F3_PSBT": serialize(f3),
        "F3_OURS": 1,
        "F3_FOREIGN": 1,
        "F3_EXPECTED_SIGNED_INPUTS": 1,
        "F3_OURS_SIGHASH": f3.sighash(0, sighash=1),
        "F4_PSBT": serialize(wrong_change),
        "F4_CHANGE_SAT": 0,
        "F4_OUTPUT_SAT": 99000,
        "F4_ACTUAL_ADDRESS": addr(p2wpkh(external_b)),
        "F5_PSBT": serialize(f5),
        "F5_CHANGE_SAT": 0,
        "F5_EXTERNAL_SAT": 99000,
        "F5_EXPECTED_EXTERNAL_ADDRESS": addr(p2wpkh(RECEIVE3)),
        "F6_SIGNED_PSBT": serialize(f1_signed),
        "F6_SIGNATURE_WITH_SIGHASH": f1_sig,
    }


def as_c_array(name, data):
    rows = []
    for offset in range(0, len(data), 12):
        row = ", ".join(f"0x{x:02x}" for x in data[offset:offset + 12])
        rows.append("    " + row)
    body = ",\n".join(rows)
    return f"static const uint8_t {name}[] = {{\n{body}\n}};\nstatic const size_t {name}_len = sizeof({name});\n"


def render():
    values = fixture_data()
    vectors = bip174_vectors()
    lines = [
        "// Generated by tools/gen_psbt_fixtures.py using embit 0.8.0 and the",
        f"// pinned BIP-174 source at {BIP174_COMMIT}.",
        "// Regenerate with: ~/.venvs/tkey-tests/bin/python tools/gen_psbt_fixtures.py",
        f"// Source: https://github.com/bitcoin/bips/blob/{BIP174_COMMIT}/bip-0174.mediawiki",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include <stdbool.h>",
        "",
    ]
    byte_values = {k: v for k, v in values.items() if isinstance(v, bytes)}
    for name, value in byte_values.items():
        lines.append(as_c_array(name.lower(), value))
    for name, value in values.items():
        if isinstance(value, str):
            lines.append(f'static const char {name.lower()}[] = "{value}";')
        elif isinstance(value, int):
            lines.append(f"static const uint64_t {name.lower()} = {value}ULL;")
    lines += ["", "// BIP-174 upstream test vectors (source labels preserved).", ""]
    vector_rows = []
    for i, (name, data, is_valid) in enumerate(vectors):
        safe = re.sub(r"[^a-zA-Z0-9]+", "_", name).strip("_").lower()
        array_name = f"bip174_vector_{i}_{safe}"
        lines.append(f"// BIP-174 {'valid' if is_valid else 'invalid'} case: {name}")
        lines.append(as_c_array(array_name, data))
        vector_rows.append((name, array_name, is_valid))
    lines += [
        "typedef struct { const char *note; const uint8_t *bytes; size_t len; bool valid; } BIP174Fixture;",
        "static const BIP174Fixture bip174_vectors[] = {",
    ]
    for note, array_name, is_valid in vector_rows:
        escaped = note.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('    {"%s", %s, sizeof(%s), %s},' % (escaped, array_name, array_name, "true" if is_valid else "false"))
    lines += ["};", "static const size_t bip174_vector_count = sizeof(bip174_vectors) / sizeof(bip174_vectors[0]);"]
    return "\n".join(lines).rstrip() + "\n"


def main():
    text = render()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} ({len(text)} chars)")


if __name__ == "__main__":
    main()
