#pragma once
#include <Arduino.h>
#include "coin_catalog.h"

/**
 * Receive-address derivation for every wallet family in the coin catalog, from the
 * 64-byte BIP-39 seed, on each chain's standard wallet path:
 *
 *   BTC  m/84'/0'/0'/0/0   bech32 (bc1)          LTC  m/84'/2'/0'/0/0    bech32 (ltc1)
 *   EVM  m/44'/60'/0'/0/0  EIP-55 0x…            DOGE m/44'/3'/0'/0/0    base58 (D…)
 *   SOL  m/44'/501'/0'/0'  ed25519 base58         TRX  m/44'/195'/0'/0/0  base58 (T…)
 *   XRP  m/44'/144'/0'/0/0 ripple base58 (r…)    ATOM m/44'/118'/0'/0/0  bech32 (cosmos1)
 *   INJ  m/44'/60'/0'/0/0  bech32 (inj1)          XLM  m/44'/148'/0'      strkey (G…)
 *   NEAR m/44'/397'/0'     implicit hex           APT  m/44'/637'/0'/0'/0' sha3 0x…
 *   VET  m/44'/818'/0'/0/0 EIP-55 0x…             BCH  m/44'/145'/0'/0/0  CashAddr
 *   SUI  m/44'/784'/0'/0'/0' blake2b 0x…          ZEC  m/44'/133'/0'/0/0  t-addr (t1…)
 *
 * Every family is checked against bip_utils reference vectors
 * (tools/test_wallet_families.py) before it may show a receive address.
 */

#define FAMILY_ADDR_LEN 100

namespace WalletFamilies {
    bool deriveAddress(WalletFamily fam, const uint8_t seed[64], char out[FAMILY_ADDR_LEN]);
    const char* name(WalletFamily fam);   // human-readable family name
}
