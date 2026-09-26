/**
 * psbt_core.h — PSBT (BIP-174) analysis and native SegWit (BIP-84 P2WPKH) signing.
 *
 * Pure logic: no SD card, no display, no wallet object, no private keys. The caller supplies
 * two callbacks: `derive` (path -> compressed public key) and `sign` (path + digest -> DER
 * signature), so key material stays in the wallet. Everything the user approves comes from
 * analyze(); sign() re-runs the same analysis and refuses to sign anything it would not show.
 *
 * Scope (fail closed on everything else):
 *   - PSBT v0 with an unsigned transaction whose inputs have empty scriptSigs
 *   - every input carries WITNESS_UTXO (so the fee is exact); NON_WITNESS_UTXO, if present,
 *     must hash to the prevout txid and agree with it
 *   - our inputs: P2WPKH at m/84'/0'/0'/{0,1}/i (coin type 1' when testnet) whose key the
 *     wallet re-derives; SIGHASH_ALL only
 *   - change: an output paying P2WPKH to a re-derived key at .../1/i; everything else is shown
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace PsbtCore {

static const size_t   MAX_PSBT_BYTES  = 32768;
static const size_t   MAX_INPUTS      = 16;
static const size_t   MAX_OUTPUTS     = 16;
static const uint64_t MAX_FEE_SATS    = 10000000ULL;   // 0.1 BTC: a higher fee is refused as a likely mistake or attack
static const size_t   MAX_ADDRESS_LEN = 100;

// path: BIP-32 indices (hardened ones have bit 31 set), depth entries long
typedef bool (*DeriveFn)(const uint32_t* path, size_t depth, uint8_t pubkey33[33], void* ctx);
typedef bool (*SignFn)(const uint32_t* path, size_t depth, const uint8_t digest32[32],
                       uint8_t* der, size_t derCapacity, size_t* derLength, void* ctx);
typedef int  (*RngFn)(void* ctx, unsigned char* out, size_t len);

struct Options {
    uint8_t  masterFingerprint[4];
    bool     testnet;
    DeriveFn derive;
    void*    deriveCtx;
    SignFn   sign;
    void*    signCtx;
};

struct DisplayOutput {
    char     address[MAX_ADDRESS_LEN];   // "bc1q...", "OP_RETURN", or "UNKNOWN SCRIPT (n bytes)"
    uint64_t amountSats;
};

struct Result {
    DisplayOutput outputs[MAX_OUTPUTS];  // every output that is NOT verified change
    size_t   outputCount;
    uint64_t externalSats;               // sum of the outputs above
    uint64_t changeSats;
    uint64_t feeSats;
    size_t   oursInputs;
    size_t   foreignInputs;
};

enum Error {
    OK = 0,
    ERR_ARGS,          // null pointer / missing callback
    ERR_FORMAT,        // not a well-formed PSBT v0 (framing, lengths, duplicates, trailing bytes)
    ERR_TX,            // unsigned transaction malformed or unsupported
    ERR_UTXO,          // an input lacks WITNESS_UTXO, or NON_WITNESS_UTXO disagrees
    ERR_SIGHASH,       // an input asks for something other than SIGHASH_ALL
    ERR_OWNERSHIP,     // an input claims our fingerprint but doesn't check out
    ERR_NOT_OURS,      // no input belongs to this wallet
    ERR_FEE,           // outputs exceed inputs, or the fee is above MAX_FEE_SATS
    ERR_FINALIZED,     // an input is already finalized
    ERR_SIGN,          // the sign callback failed
    ERR_SPACE,         // the output buffer is too small
};

// Validates the whole PSBT and fills what the user must approve.
Error analyze(const uint8_t* psbt, size_t psbtLen, const Options& opt, Result* result);

// Signs every input that belongs to the wallet (SIGHASH_ALL) and writes the PSBT with a
// PARTIAL_SIG added to each; every existing record is copied byte-for-byte. result may be null.
Error sign(const uint8_t* psbt, size_t psbtLen, const Options& opt,
           uint8_t* out, size_t outCapacity, size_t* outLen, Result* result);

// BIP-143 SIGHASH_ALL digest of one P2WPKH input of a non-witness-serialized transaction.
bool bip143Sighash(const uint8_t* unsignedTx, size_t txLen, size_t inputIndex,
                   uint64_t amountSats, const uint8_t pubkey33[33], uint8_t digest32[32]);

// Deterministic (RFC 6979) secp256k1 ECDSA, low-S, DER encoded (no sighash byte).
bool signMbedTlsSecp256k1(const uint8_t privateKey32[32], const uint8_t digest32[32],
                          RngFn rng, void* rngCtx, uint8_t* der, size_t derCapacity, size_t* derLength);

const char* errorName(Error e);

}  // namespace PsbtCore
