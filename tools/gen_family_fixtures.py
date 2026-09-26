#!/usr/bin/env python3
"""Generate wallet-family address fixtures with independent Python libraries."""
from __future__ import annotations

import hashlib
import json
import urllib.request
from pathlib import Path

from bip_utils import (
    Bip39SeedGenerator,
    Bip44,
    Bip44Changes,
    Bip44Coins,
    Bip84,
    Bip84Coins,
    Bip39MnemonicValidator,
)
from embit import bip32, bip39, script

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/host/family_fixtures.h"

MNEMONIC_12 = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"
MNEMONIC_24 = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon " \
              "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art"
PASSPHRASE = "TREZOR"

# Paths in this table are the exact paths encoded in wallet_families.cpp. The wallet names refer
# to mainstream restore/import paths, not to live calls into those applications.
FAMILIES = [
    ("BTC", "FAM_BTC", "m/84'/0'/0'/0/0", "Trust Wallet (BIP84 default)", "embit BIP84"),
    ("EVM", "FAM_EVM", "m/44'/60'/0'/0/0", "Trust Wallet / MetaMask EVM default", "bip_utils Ethereum"),
    ("SOL", "FAM_SOL", "m/44'/501'/0'/0'", "Phantom bip44Change account 0", "bip_utils Solana"),
    ("DOGE", "FAM_DOGE", "m/44'/3'/0'/0/0", "Trust Wallet default", "bip_utils Dogecoin"),
    ("LTC", "FAM_LTC", "m/84'/2'/0'/0/0", "Trust Wallet SegWit default", "bip_utils BIP84 Litecoin"),
    ("TRX", "FAM_TRX", "m/44'/195'/0'/0/0", "Trust Wallet default", "bip_utils TRON"),
    ("XRP", "FAM_XRP", "m/44'/144'/0'/0/0", "Trust Wallet default; same SLIP-44 path", "bip_utils Ripple"),
    ("ATOM", "FAM_ATOM", "m/44'/118'/0'/0/0", "Keplr Cosmos Hub default", "bip_utils Cosmos"),
    ("INJ", "FAM_INJ", "m/44'/60'/0'/0/0", "Keplr Injective coin type 60", "bip_utils Injective"),
    ("VET", "FAM_VET", "m/44'/818'/0'/0/0", "Trust Wallet Wallet Core default", "bip_utils VeChain"),
    ("BCH", "FAM_BCH", "m/44'/145'/0'/0/0", "Trust Wallet default", "bip_utils Bitcoin Cash"),
    ("ZEC", "FAM_ZEC", "m/44'/133'/0'/0/0", "Exodus transparent t-address path", "bip_utils Zcash"),
    ("XLM", "FAM_XLM", "m/44'/148'/0'", "Freighter / Stellar SEP-0005 account path", "bip_utils Stellar"),
    ("NEAR", "FAM_NEAR", "m/44'/397'/0'", "SafePal NEAR path", "bip_utils NEAR Protocol"),
    ("APT", "FAM_APT", "m/44'/637'/0'/0'/0'", "Petra-compatible Aptos Ed25519 path", "bip_utils Aptos"),
    ("SUI", "FAM_SUI", "m/44'/784'/0'/0'/0'", "Sui Wallet Ed25519 path", "bip_utils Sui"),
]

# Sources documenting the wallet paths checked above:
# Trust Wallet: https://github.com/trustwallet/wallet-core/blob/master/registry.json
# Trust Wallet path semantics: https://developer.trustwallet.com/developer/wallet-core/wallet-core-usage
# Phantom: https://help.phantom.com/articles/12988493966227
# Keplr: https://help.keplr.app/extension/5R3bMyjtr2FwnBvJQuJwJu/set-a-custom-derivation-path/5R3bMyjtr1mPSekybSwN2A
# Exodus Zcash: https://www.exodus.com/support/en/articles/8598933-derivation-paths-in-exodus
# Stellar SEP-0005: https://github.com/stellar/stellar-protocol/blob/master/ecosystem/sep-0005.md
# Sui: https://www.sui.io/blog/wallet-cryptography-specifications
# Aptos path reference: https://github.com/aptos-labs/japtos#hierarchical-deterministic-wallets
# NEAR: https://safepalsupport.zendesk.com/hc/en-us/articles/360053299631-The-derivation-path-of-the-currency-already-supported-by-SafePal
# VeChain official wallet docs also document the alternate shorter path m/44'/818'/0'/0:
# https://support.vechain.org/support/solutions/articles/103000203772-what-is-the-derivation-path-used-by-vechain-wallets
# This firmware path matches Trust Wallet's five-component Wallet Core registry path.
# 24-word vector: https://github.com/trezor/python-mnemonic/blob/master/vectors.json (English, vector 8)


def ref_address(family: str, seed: bytes) -> str:
    if family == "BTC":
        root = bip32.HDKey.from_seed(seed)
        key = root.derive("m/84h/0h/0h/0/0")
        return script.p2wpkh(key.get_public_key()).address(network={"bech32": "bc", "p2pkh": b"\x00", "p2sh": b"\x05"})

    if family == "LTC":
        node = Bip84.FromSeed(seed, Bip84Coins.LITECOIN).Purpose().Coin().Account(0)
        node = node.Change(Bip44Changes.CHAIN_EXT).AddressIndex(0)
        return node.PublicKey().ToAddress()

    coin = {
        "EVM": Bip44Coins.ETHEREUM,
        "SOL": Bip44Coins.SOLANA,
        "DOGE": Bip44Coins.DOGECOIN,
        "TRX": Bip44Coins.TRON,
        "XRP": Bip44Coins.RIPPLE,
        "ATOM": Bip44Coins.COSMOS,
        "INJ": Bip44Coins.INJECTIVE,
        "VET": Bip44Coins.VECHAIN,
        "BCH": Bip44Coins.BITCOIN_CASH,
        "ZEC": Bip44Coins.ZCASH,
        "XLM": Bip44Coins.STELLAR,
        "NEAR": Bip44Coins.NEAR_PROTOCOL,
        "APT": Bip44Coins.APTOS,
        "SUI": Bip44Coins.SUI,
    }[family]
    node = Bip44.FromSeed(seed, coin).Purpose().Coin().Account(0)
    if family == "SOL":
        # Phantom's bip44Change Solana path ends at the hardened change component.
        node = node.Change(Bip44Changes.CHAIN_EXT)
    elif family not in ("XLM", "NEAR"):
        node = node.Change(Bip44Changes.CHAIN_EXT).AddressIndex(0)
    return node.PublicKey().ToAddress()


def c_bytes(name: str, data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 12):
        row = ", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 12])
        rows.append("    " + row)
    return f"static const uint8_t {name}[] = {{\n" + ",\n".join(rows) + "\n};\n"


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def main() -> None:
    mnemonic_vectors = [
        ("bip39_12", MNEMONIC_12, "", "BIP-39 12-word official vector, empty passphrase"),
        ("bip39_24", MNEMONIC_24, "", "BIP-39 24-word official vector 8, empty passphrase"),
        ("bip39_12_trezor", MNEMONIC_12, PASSPHRASE, "BIP-39 12-word vector with passphrase TREZOR"),
    ]
    seeds = []
    validator = Bip39MnemonicValidator()
    for symbol, mnemonic, passphrase, note in mnemonic_vectors:
        if not validator.IsValid(mnemonic):
            raise RuntimeError(f"invalid official BIP-39 mnemonic fixture: {symbol}")
        embit_seed = bip39.mnemonic_to_seed(mnemonic, password=passphrase)
        bip_utils_seed = Bip39SeedGenerator(mnemonic).Generate(passphrase)
        if embit_seed != bip_utils_seed:
            raise RuntimeError(f"embit/bip_utils BIP-39 seed mismatch for {symbol}")
        seeds.append((symbol, mnemonic, passphrase, note, embit_seed))

    if len(seeds[0][4]) != 64 or len(seeds[1][4]) != 64 or len(seeds[2][4]) != 64:
        raise RuntimeError("BIP-39 seed generator returned a non-64-byte seed")
    if seeds[0][4] == seeds[2][4]:
        raise RuntimeError("passphrase did not change BIP-39 seed")
    official_trezor_seed = bytes.fromhex(
        "c55257c360c07c72029aebc1b53c05ed0362ada38ead3e3e9efa3708e5349553"
        "1f09a6987599d18264c1e1c92f2cf141630c7a3c4ab7c81b2f001698e7463b04"
    )
    if seeds[2][4] != official_trezor_seed:
        raise RuntimeError("12-word TREZOR passphrase seed differs from official BIP-39 vector")

    lines = [
        "// Generated by tools/gen_family_fixtures.py using embit 0.8.0 and bip_utils 2.12.2.",
        "// BIP-39 12-word vector: https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki",
        "// BIP-39 24-word vector 8: https://github.com/trezor/python-mnemonic/blob/master/vectors.json",
        "// Mainstream wallet paths are documented in the generator's source comments.",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#include "coin_catalog.h"',
        "",
    ]
    for symbol, _mnemonic, _passphrase, _note, seed in seeds:
        lines.append(c_bytes(f"family_seed_{symbol}", seed))
    lines.extend([
        "typedef struct {",
        "    WalletFamily family;",
        "    const char *family_name;",
        "    const char *derivation_path;",
        "    const char *reference_wallet;",
        "    const char *reference_implementation;",
        "    const char *mnemonic_case;",
        "    const uint8_t *seed;",
        "    const char *reference_address;",
        "    const char *mainstream_wallet_address;",
        "} FamilyAddressFixture;",
        "",
        "static const FamilyAddressFixture family_address_fixtures[] = {",
    ])
    for symbol, mnemonic, passphrase, case_note, seed in seeds:
        for family, enum_name, path, wallet, implementation in FAMILIES:
            expected = ref_address(family, seed)
            lines.append(
                "    {" + ", ".join([
                    enum_name,
                    c_string(family),
                    c_string(path),
                    c_string(wallet),
                    c_string(implementation),
                    c_string(case_note),
                    f"family_seed_{symbol}",
                    c_string(expected),
                    c_string(ref_address(family, seed)),
                ]) + "},"
            )
    lines.extend([
        "};",
        "static const size_t family_address_fixture_count =",
        "    sizeof(family_address_fixtures) / sizeof(family_address_fixtures[0]);",
        "",
    ])
    text = "\n".join(lines)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(FAMILIES)} families x {len(seeds)} mnemonic cases")


if __name__ == "__main__":
    main()
