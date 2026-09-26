/**
 * psbt_core.cpp — PSBT (BIP-174) analysis and BIP-143 / BIP-84 P2WPKH signing.
 * See psbt_core.h for the scope. Every length read from the PSBT is checked by subtraction
 * against what is left, so no crafted length can wrap an offset.
 */

#include "psbt_core.h"
#include <string.h>
#include <stdio.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ripemd160.h>   // the ESP32 build lacks MBEDTLS_MD_RIPEMD160: call it directly
#include <mbedtls/ecdsa.h>
#include <mbedtls/platform_util.h>

namespace PsbtCore {

namespace {

const uint64_t MAX_MONEY = 2100000000000000ULL;
const uint32_t HARDENED  = 0x80000000u;

// PSBT key types (BIP-174)
const uint8_t GLOBAL_UNSIGNED_TX   = 0x00;
const uint8_t GLOBAL_VERSION       = 0xFB;
const uint8_t IN_NON_WITNESS_UTXO  = 0x00;
const uint8_t IN_WITNESS_UTXO      = 0x01;
const uint8_t IN_PARTIAL_SIG       = 0x02;
const uint8_t IN_SIGHASH_TYPE      = 0x03;
const uint8_t IN_BIP32_DERIVATION  = 0x06;
const uint8_t IN_FINAL_SCRIPTSIG   = 0x07;
const uint8_t IN_FINAL_WITNESS     = 0x08;
const uint8_t OUT_BIP32_DERIVATION = 0x02;

// ─── Bounded reader ──────────────────────────────────────────────────────────

struct Reader {
    const uint8_t* p;
    size_t n;
    size_t o;   // invariant: o <= n
};

bool take(Reader& r, size_t len, const uint8_t** out) {
    if (len > r.n - r.o) return false;
    if (out) *out = r.p + r.o;
    r.o += len;
    return true;
}

uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint64_t rd64(const uint8_t* p) {
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

void wr64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

// Bitcoin CompactSize; the shortest encoding is required
bool readCompact(Reader& r, uint64_t* v) {
    const uint8_t* b;
    if (!take(r, 1, &b)) return false;
    if (b[0] < 0xFD) {
        *v = b[0];
        return true;
    }
    size_t len = b[0] == 0xFD ? 2 : (b[0] == 0xFE ? 4 : 8);
    if (!take(r, len, &b)) return false;
    uint64_t x = 0;
    for (size_t i = 0; i < len; i++) x |= (uint64_t)b[i] << (8 * i);
    uint64_t minimum = len == 2 ? 0xFD : (len == 4 ? 0x10000 : 0x100000000ULL);
    if (x < minimum) return false;
    *v = x;
    return true;
}

// A CompactSize length followed by that many bytes
bool readVarBytes(Reader& r, const uint8_t** data, size_t* len) {
    uint64_t l;
    if (!readCompact(r, &l) || l > r.n - r.o) return false;
    *len = (size_t)l;
    return take(r, *len, data);
}

size_t writeCompact(uint8_t* p, uint64_t v) {
    if (v < 0xFD) { p[0] = (uint8_t)v; return 1; }
    if (v <= 0xFFFF) { p[0] = 0xFD; p[1] = (uint8_t)v; p[2] = (uint8_t)(v >> 8); return 3; }
    if (v <= 0xFFFFFFFFULL) { p[0] = 0xFE; wr32(p + 1, (uint32_t)v); return 5; }
    p[0] = 0xFF;
    wr64(p + 1, v);
    return 9;
}

// ─── Hashes ──────────────────────────────────────────────────────────────────

struct Sha {
    mbedtls_sha256_context c;
    Sha() { mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0); }
    ~Sha() { mbedtls_sha256_free(&c); }
    void add(const uint8_t* p, size_t n) { mbedtls_sha256_update(&c, p, n); }
    // SHA-256d of everything added
    void finishDouble(uint8_t out[32]) {
        uint8_t first[32];
        mbedtls_sha256_finish(&c, first);
        mbedtls_sha256(first, 32, out, 0);
        mbedtls_platform_zeroize(first, sizeof(first));
    }
};

void doubleSha(const uint8_t* p, size_t n, uint8_t out[32]) {
    Sha s;
    s.add(p, n);
    s.finishDouble(out);
}

void hash160(const uint8_t* p, size_t n, uint8_t out[20]) {
    uint8_t sha[32];
    mbedtls_sha256(p, n, sha, 0);
    mbedtls_ripemd160(sha, 32, out);
    mbedtls_platform_zeroize(sha, sizeof(sha));
}

// ─── Transactions ────────────────────────────────────────────────────────────

struct Span { size_t off, len; };   // a byte range inside the parsed buffer

struct TxInput {
    const uint8_t* prevout;   // 36 bytes: txid (internal byte order) || vout (LE)
    uint32_t sequence;
};

struct TxOutput {
    uint64_t value;
    Span script;
    Span whole;   // value || scriptLen || script, as serialized (for hashOutputs)
};

struct Tx {
    const uint8_t* p;
    size_t n;
    uint32_t version;
    uint32_t locktime;
    size_t ni, no;
    TxInput in[MAX_INPUTS];
    TxOutput out[MAX_OUTPUTS];
    uint8_t txid[32];   // SHA-256d of the non-witness serialization
};

// Parses a transaction. The unsigned PSBT tx must use the legacy serialization with empty
// scriptSigs (allowWitness = false); a NON_WITNESS_UTXO may be segwit-serialized, in which
// case the txid is computed over the non-witness parts only.
bool parseTx(const uint8_t* p, size_t n, bool allowWitness, bool requireEmptyScriptSig, Tx& t) {
    memset(&t, 0, sizeof(t));
    t.p = p;
    t.n = n;
    Reader r = {p, n, 0};
    const uint8_t* b;
    if (!take(r, 4, &b)) return false;
    t.version = rd32(b);

    bool segwit = false;
    if (r.o + 2 <= n && p[r.o] == 0x00) {   // BIP-144 marker + flag
        if (!allowWitness || p[r.o + 1] != 0x01) return false;
        segwit = true;
        r.o += 2;
    }
    size_t bodyStart = r.o;   // inputs + outputs: hashed as-is for the txid

    uint64_t count;
    if (!readCompact(r, &count) || count == 0 || count > MAX_INPUTS) return false;
    t.ni = (size_t)count;
    for (size_t i = 0; i < t.ni; i++) {
        const uint8_t* script;
        size_t scriptLen;
        if (!take(r, 36, &t.in[i].prevout) || !readVarBytes(r, &script, &scriptLen)) return false;
        if (requireEmptyScriptSig && scriptLen != 0) return false;
        if (!take(r, 4, &b)) return false;
        t.in[i].sequence = rd32(b);
    }
    if (!readCompact(r, &count) || count == 0 || count > MAX_OUTPUTS) return false;
    t.no = (size_t)count;
    for (size_t i = 0; i < t.no; i++) {
        size_t start = r.o;
        const uint8_t* script;
        size_t scriptLen;
        if (!take(r, 8, &b)) return false;
        t.out[i].value = rd64(b);
        if (t.out[i].value > MAX_MONEY || !readVarBytes(r, &script, &scriptLen)) return false;
        t.out[i].script = {(size_t)(script - p), scriptLen};
        t.out[i].whole = {start, r.o - start};
    }
    size_t bodyEnd = r.o;

    if (segwit) {   // one witness stack per input
        for (size_t i = 0; i < t.ni; i++) {
            uint64_t items;
            if (!readCompact(r, &items) || items > 10000) return false;
            for (uint64_t k = 0; k < items; k++) {
                const uint8_t* w;
                size_t wl;
                if (!readVarBytes(r, &w, &wl)) return false;
            }
        }
    }
    if (!take(r, 4, &b) || r.o != n) return false;   // locktime, then nothing more
    t.locktime = rd32(b);

    Sha s;
    s.add(p, 4);
    s.add(p + bodyStart, bodyEnd - bodyStart);
    s.add(p + n - 4, 4);
    s.finishDouble(t.txid);
    return true;
}

// Streams a (possibly segwit-serialized) previous transaction of any size, computing its
// txid over the non-witness parts and returning output `vout`.
bool prevTxOutput(const uint8_t* p, size_t n, uint32_t vout, uint8_t txid[32],
                  uint64_t* value, const uint8_t** script, size_t* scriptLen) {
    Reader r = {p, n, 0};
    const uint8_t* b;
    if (!take(r, 4, &b)) return false;
    bool segwit = false;
    if (r.o + 2 <= n && p[r.o] == 0x00) {
        if (p[r.o + 1] != 0x01) return false;
        segwit = true;
        r.o += 2;
    }
    size_t bodyStart = r.o;
    uint64_t ni, no;
    if (!readCompact(r, &ni) || ni == 0) return false;
    for (uint64_t i = 0; i < ni; i++) {
        const uint8_t* sc;
        size_t sl;
        if (!take(r, 36, nullptr) || !readVarBytes(r, &sc, &sl) || !take(r, 4, nullptr)) return false;
    }
    if (!readCompact(r, &no) || no == 0 || vout >= no) return false;
    bool found = false;
    for (uint64_t i = 0; i < no; i++) {
        const uint8_t* sc;
        size_t sl;
        if (!take(r, 8, &b) || !readVarBytes(r, &sc, &sl)) return false;
        if (i == vout) {
            *value = rd64(b);
            *script = sc;
            *scriptLen = sl;
            found = true;
        }
    }
    size_t bodyEnd = r.o;
    if (segwit) {
        for (uint64_t i = 0; i < ni; i++) {
            uint64_t items;
            if (!readCompact(r, &items)) return false;
            for (uint64_t k = 0; k < items; k++) {
                const uint8_t* w;
                size_t wl;
                if (!readVarBytes(r, &w, &wl)) return false;
            }
        }
    }
    if (!take(r, 4, nullptr) || r.o != n || !found) return false;
    Sha s;
    s.add(p, 4);
    s.add(p + bodyStart, bodyEnd - bodyStart);
    s.add(p + n - 4, 4);
    s.finishDouble(txid);
    return true;
}

}  // namespace

bool bip143Sighash(const uint8_t* unsignedTx, size_t txLen, size_t inputIndex,
                   uint64_t amountSats, const uint8_t pubkey33[33], uint8_t digest32[32]) {
    if (!unsignedTx || !pubkey33 || !digest32) return false;
    static Tx t;   // ~1 KB: kept off the small loop() stack
    if (!parseTx(unsignedTx, txLen, false, false, t) || inputIndex >= t.ni) return false;

    uint8_t hashPrevouts[32], hashSequence[32], hashOutputs[32], tmp[8];
    {
        Sha s;
        for (size_t i = 0; i < t.ni; i++) s.add(t.in[i].prevout, 36);
        s.finishDouble(hashPrevouts);
    }
    {
        Sha s;
        for (size_t i = 0; i < t.ni; i++) { wr32(tmp, t.in[i].sequence); s.add(tmp, 4); }
        s.finishDouble(hashSequence);
    }
    {
        Sha s;
        for (size_t i = 0; i < t.no; i++) s.add(t.p + t.out[i].whole.off, t.out[i].whole.len);
        s.finishDouble(hashOutputs);
    }

    // scriptCode for P2WPKH: OP_DUP OP_HASH160 <20-byte key hash> OP_EQUALVERIFY OP_CHECKSIG
    uint8_t scriptCode[26] = {0x19, 0x76, 0xA9, 0x14};
    hash160(pubkey33, 33, scriptCode + 4);
    scriptCode[24] = 0x88;
    scriptCode[25] = 0xAC;

    Sha s;
    wr32(tmp, t.version);                     s.add(tmp, 4);
    s.add(hashPrevouts, 32);
    s.add(hashSequence, 32);
    s.add(t.in[inputIndex].prevout, 36);
    s.add(scriptCode, sizeof(scriptCode));
    wr64(tmp, amountSats);                    s.add(tmp, 8);
    wr32(tmp, t.in[inputIndex].sequence);     s.add(tmp, 4);
    s.add(hashOutputs, 32);
    wr32(tmp, t.locktime);                    s.add(tmp, 4);
    wr32(tmp, 1);                             s.add(tmp, 4);   // SIGHASH_ALL
    s.finishDouble(digest32);
    return true;
}

// ─── Deterministic signing ──────────────────────────────────────────────────

bool signMbedTlsSecp256k1(const uint8_t privateKey32[32], const uint8_t digest32[32],
                          RngFn rng, void* rngCtx, uint8_t* der, size_t derCapacity, size_t* derLength) {
    if (!privateKey32 || !digest32 || !der || !derLength || derCapacity < 72) return false;
    *derLength = 0;
    mbedtls_ecp_group grp;
    mbedtls_mpi d, r, s, halfN;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_init(&halfN);
    uint8_t rb[32], sb[32];
    bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1) == 0 &&
              mbedtls_mpi_read_binary(&d, privateKey32, 32) == 0 &&
              mbedtls_mpi_cmp_int(&d, 1) >= 0 && mbedtls_mpi_cmp_mpi(&d, &grp.N) < 0 &&
              mbedtls_ecdsa_sign_det_ext(&grp, &r, &s, &d, digest32, 32, MBEDTLS_MD_SHA256, rng, rngCtx) == 0 &&
              mbedtls_mpi_copy(&halfN, &grp.N) == 0 && mbedtls_mpi_shift_r(&halfN, 1) == 0;
    // Low-S (BIP-146): a high-S signature is non-standard and won't relay
    if (ok && mbedtls_mpi_cmp_mpi(&s, &halfN) > 0) ok = mbedtls_mpi_sub_mpi(&s, &grp.N, &s) == 0;
    ok = ok && mbedtls_mpi_write_binary(&r, rb, 32) == 0 && mbedtls_mpi_write_binary(&s, sb, 32) == 0;
    if (ok) {
        // DER: 30 len 02 rlen r 02 slen s, each integer minimal and positive
        const uint8_t* ints[2] = {rb, sb};
        uint8_t body[70];
        size_t bl = 0;
        for (int k = 0; k < 2; k++) {
            const uint8_t* v = ints[k];
            size_t skip = 0;
            while (skip < 31 && v[skip] == 0) skip++;
            bool pad = v[skip] & 0x80;
            body[bl++] = 0x02;
            body[bl++] = (uint8_t)(32 - skip + pad);
            if (pad) body[bl++] = 0x00;
            memcpy(body + bl, v + skip, 32 - skip);
            bl += 32 - skip;
        }
        der[0] = 0x30;
        der[1] = (uint8_t)bl;
        memcpy(der + 2, body, bl);
        *derLength = bl + 2;
    }
    mbedtls_platform_zeroize(rb, sizeof(rb));
    mbedtls_platform_zeroize(sb, sizeof(sb));
    mbedtls_mpi_free(&d);   // mbedtls_mpi_free zeroizes the limbs
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&halfN);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

namespace {

// ─── Addresses (display only) ────────────────────────────────────────────────

uint32_t bech32Polymod(const uint8_t* v, size_t n) {
    static const uint32_t G[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
    uint32_t chk = 1;
    for (size_t i = 0; i < n; i++) {
        uint8_t top = chk >> 25;
        chk = ((chk & 0x1ffffff) << 5) ^ v[i];
        for (int k = 0; k < 5; k++) if ((top >> k) & 1) chk ^= G[k];
    }
    return chk;
}

// Segwit address: bech32 for v0, bech32m for v1+ (BIP-173 / BIP-350)
bool segwitAddress(const char* hrp, int version, const uint8_t* prog, size_t progLen, char* out, size_t cap) {
    static const char* CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    uint8_t data[1 + 65 + 6];
    size_t dl = 0;
    data[dl++] = (uint8_t)version;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < progLen; i++) {   // 8-bit -> 5-bit groups
        acc = (acc << 8) | prog[i];
        bits += 8;
        while (bits >= 5) { bits -= 5; data[dl++] = (acc >> bits) & 31; }
    }
    if (bits) data[dl++] = (acc << (5 - bits)) & 31;
    size_t hl = strlen(hrp);
    uint8_t chk[2 * 8 + 1 + sizeof(data) + 6];
    size_t cl = 0;
    for (size_t i = 0; i < hl; i++) chk[cl++] = hrp[i] >> 5;
    chk[cl++] = 0;
    for (size_t i = 0; i < hl; i++) chk[cl++] = hrp[i] & 31;
    memcpy(chk + cl, data, dl);
    cl += dl;
    memset(chk + cl, 0, 6);
    cl += 6;
    uint32_t mod = bech32Polymod(chk, cl) ^ (version == 0 ? 1 : 0x2bc830a3);
    if (hl + 1 + dl + 6 + 1 > cap) return false;
    size_t o = 0;
    memcpy(out, hrp, hl);
    o = hl;
    out[o++] = '1';
    for (size_t i = 0; i < dl; i++) out[o++] = CHARSET[data[i]];
    for (int i = 0; i < 6; i++) out[o++] = CHARSET[(mod >> (5 * (5 - i))) & 31];
    out[o] = '\0';
    return true;
}

bool base58Check(uint8_t versionByte, const uint8_t* hash20, char* out, size_t cap) {
    static const char* ALPHA = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint8_t buf[25], chk[32];
    buf[0] = versionByte;
    memcpy(buf + 1, hash20, 20);
    doubleSha(buf, 21, chk);
    memcpy(buf + 21, chk, 4);
    uint8_t digits[40] = {0};   // base-58 digits, little-endian
    size_t nd = 0;
    for (int i = 0; i < 25; i++) {
        uint32_t carry = buf[i];
        for (size_t k = 0; k < nd; k++) {
            carry += (uint32_t)digits[k] << 8;
            digits[k] = carry % 58;
            carry /= 58;
        }
        while (carry) { digits[nd++] = carry % 58; carry /= 58; }
    }
    size_t zeros = 0;
    while (zeros < 25 && buf[zeros] == 0) zeros++;
    if (zeros + nd + 1 > cap) return false;
    size_t o = 0;
    for (size_t i = 0; i < zeros; i++) out[o++] = '1';
    for (size_t i = 0; i < nd; i++) out[o++] = ALPHA[digits[nd - 1 - i]];
    out[o] = '\0';
    return true;
}

void describeScript(const uint8_t* s, size_t n, bool testnet, char* out, size_t cap) {
    const char* hrp = testnet ? "tb" : "bc";
    if (n == 22 && s[0] == 0x00 && s[1] == 0x14 && segwitAddress(hrp, 0, s + 2, 20, out, cap)) return;
    if (n == 34 && s[0] == 0x00 && s[1] == 0x20 && segwitAddress(hrp, 0, s + 2, 32, out, cap)) return;
    if (n == 34 && s[0] == 0x51 && s[1] == 0x20 && segwitAddress(hrp, 1, s + 2, 32, out, cap)) return;
    if (n == 25 && s[0] == 0x76 && s[1] == 0xA9 && s[2] == 0x14 && s[23] == 0x88 && s[24] == 0xAC &&
        base58Check(testnet ? 0x6F : 0x00, s + 3, out, cap)) return;
    if (n == 23 && s[0] == 0xA9 && s[1] == 0x14 && s[22] == 0x87 &&
        base58Check(testnet ? 0xC4 : 0x05, s + 2, out, cap)) return;
    if (n >= 1 && s[0] == 0x6A) { snprintf(out, cap, "OP_RETURN (%u bytes)", (unsigned)n); return; }
    snprintf(out, cap, "UNKNOWN SCRIPT (%u bytes)", (unsigned)n);
}

// ─── PSBT maps ───────────────────────────────────────────────────────────────

struct Record {
    const uint8_t* key;
    size_t keyLen;
    const uint8_t* value;
    size_t valueLen;
};

struct Map {
    size_t start, end;   // [start, end) covers the records; the 0x00 terminator is at end
    Record rec[24];   // more records than this in one map is refused (fail closed)
    size_t count;
};

// Reads records up to the 0x00 separator; rejects duplicate keys and empty keys
bool readMap(Reader& r, Map& m) {
    m.start = r.o;
    m.count = 0;
    while (true) {
        uint64_t keyLen;
        size_t before = r.o;
        if (!readCompact(r, &keyLen)) return false;
        if (keyLen == 0) {
            m.end = before;
            return true;
        }
        if (m.count == sizeof(m.rec) / sizeof(m.rec[0])) return false;
        Record& rc = m.rec[m.count];
        r.o = before;
        if (!readVarBytes(r, &rc.key, &rc.keyLen) || !readVarBytes(r, &rc.value, &rc.valueLen)) return false;
        for (size_t i = 0; i < m.count; i++) {
            if (m.rec[i].keyLen == rc.keyLen && memcmp(m.rec[i].key, rc.key, rc.keyLen) == 0) return false;
        }
        m.count++;
    }
}

const Record* findKey(const Map& m, uint8_t type, size_t keyLen) {
    for (size_t i = 0; i < m.count; i++) {
        if (m.rec[i].keyLen == keyLen && m.rec[i].key[0] == type) return &m.rec[i];
    }
    return nullptr;
}

struct Parsed {
    Tx tx;
    Map global;
    Map in[MAX_INPUTS];
    Map out[MAX_OUTPUTS];
    size_t txOff;                 // offset of the unsigned tx inside the PSBT
    size_t txLen;
    uint64_t inValue[MAX_INPUTS];
    bool ours[MAX_INPUTS];
    uint32_t path[MAX_INPUTS][5];
    uint8_t pubkey[MAX_INPUTS][33];
};

// Our BIP-84 key for a BIP32_DERIVATION record, re-derived by the wallet. Returns:
//  1 = our key at m/84'/coin'/0'/branch/i (branch in `branches`), 0 = not our fingerprint,
// -1 = our fingerprint but it doesn't check out (wrong path shape or a key we don't derive).
int checkDerivation(const Record& rc, const Options& opt, uint32_t branchMask,
                    uint32_t path[5], uint8_t pubkey[33]) {
    if (rc.keyLen != 34 || (rc.key[1] != 0x02 && rc.key[1] != 0x03)) return -1;
    if (rc.valueLen < 4 || (rc.valueLen - 4) % 4 != 0) return -1;
    if (memcmp(rc.value, opt.masterFingerprint, 4) != 0) return 0;
    if (rc.valueLen != 4 + 5 * 4) return -1;
    for (int i = 0; i < 5; i++) path[i] = rd32(rc.value + 4 + 4 * i);
    uint32_t coin = opt.testnet ? 1 : 0;
    if (path[0] != (84 | HARDENED) || path[1] != (coin | HARDENED) || path[2] != (0 | HARDENED)) return -1;
    if (path[3] > 1 || !((branchMask >> path[3]) & 1) || (path[4] & HARDENED)) return -1;
    uint8_t derived[33];
    if (!opt.derive(path, 5, derived, opt.deriveCtx) || memcmp(derived, rc.key + 1, 33) != 0) return -1;
    memcpy(pubkey, derived, 33);
    return 1;
}

bool isP2wpkhOf(const uint8_t* script, size_t n, const uint8_t pubkey[33]) {
    if (n != 22 || script[0] != 0x00 || script[1] != 0x14) return false;
    uint8_t h[20];
    hash160(pubkey, 33, h);
    return memcmp(script + 2, h, 20) == 0;
}

Error parseAndCheck(const uint8_t* psbt, size_t psbtLen, const Options& opt, Parsed& P, Result* res) {
    if (!psbt || !opt.derive || psbtLen > MAX_PSBT_BYTES) return ERR_ARGS;
    Reader r = {psbt, psbtLen, 0};
    const uint8_t* magic;
    if (!take(r, 5, &magic) || memcmp(magic, "psbt\xff", 5) != 0) return ERR_FORMAT;

    if (!readMap(r, P.global)) return ERR_FORMAT;
    const Record* txr = findKey(P.global, GLOBAL_UNSIGNED_TX, 1);
    if (!txr) return ERR_FORMAT;
    const Record* ver = findKey(P.global, GLOBAL_VERSION, 1);
    if (ver && (ver->valueLen != 4 || rd32(ver->value) != 0)) return ERR_FORMAT;   // PSBT v0 only
    for (size_t i = 0; i < P.global.count; i++) {   // a key type the spec defines as 1 byte must be
        if (P.global.rec[i].key[0] == GLOBAL_UNSIGNED_TX && P.global.rec[i].keyLen != 1) return ERR_FORMAT;
    }
    P.txOff = (size_t)(txr->value - psbt);
    P.txLen = txr->valueLen;
    if (!parseTx(txr->value, txr->valueLen, false, true, P.tx)) return ERR_TX;

    uint64_t totalIn = 0, totalOut = 0;
    for (size_t i = 0; i < P.tx.ni; i++) {
        Map& m = P.in[i];
        if (r.o == r.n || !readMap(r, m)) return ERR_FORMAT;
        if (findKey(m, IN_FINAL_SCRIPTSIG, 1) || findKey(m, IN_FINAL_WITNESS, 1)) return ERR_FINALIZED;
        const Record* sh = findKey(m, IN_SIGHASH_TYPE, 1);
        if (sh && (sh->valueLen != 4 || rd32(sh->value) != 1)) return ERR_SIGHASH;

        // WITNESS_UTXO: value (8) || script (CompactSize-prefixed) — required for an exact fee
        const Record* wu = findKey(m, IN_WITNESS_UTXO, 1);
        if (!wu) return ERR_UTXO;
        Reader u = {wu->value, wu->valueLen, 0};
        const uint8_t* b;
        const uint8_t* script;
        size_t scriptLen;
        if (!take(u, 8, &b) || !readVarBytes(u, &script, &scriptLen) || u.o != u.n) return ERR_UTXO;
        uint64_t value = rd64(b);
        if (value > MAX_MONEY) return ERR_UTXO;

        const Record* nwu = findKey(m, IN_NON_WITNESS_UTXO, 1);
        if (nwu) {   // the full previous tx must be the one the prevout names, and agree
            uint8_t txid[32];
            uint64_t pv;
            const uint8_t* ps;
            size_t pl;
            if (!prevTxOutput(nwu->value, nwu->valueLen, rd32(P.tx.in[i].prevout + 32), txid, &pv, &ps, &pl) ||
                memcmp(txid, P.tx.in[i].prevout, 32) != 0 || pv != value || pl != scriptLen ||
                memcmp(ps, script, scriptLen) != 0) {
                return ERR_UTXO;
            }
        }
        P.inValue[i] = value;
        if (value > MAX_MONEY - totalIn) return ERR_UTXO;
        totalIn += value;

        // Ownership: a derivation record with our fingerprint that re-derives to a P2WPKH
        // key paying exactly this UTXO. Our fingerprint on anything else is refused.
        P.ours[i] = false;
        for (size_t k = 0; k < m.count; k++) {
            if (m.rec[k].key[0] != IN_BIP32_DERIVATION) continue;
            uint32_t path[5];
            uint8_t pk[33];
            int c = checkDerivation(m.rec[k], opt, 0x3, path, pk);
            if (c < 0) return ERR_OWNERSHIP;
            if (c == 1) {
                if (P.ours[i] || !isP2wpkhOf(script, scriptLen, pk)) return ERR_OWNERSHIP;
                P.ours[i] = true;
                memcpy(P.path[i], path, sizeof(path));
                memcpy(P.pubkey[i], pk, 33);
            }
        }
    }

    Result local;
    Result& R = res ? *res : local;
    memset(&R, 0, sizeof(R));
    for (size_t i = 0; i < P.tx.no; i++) {
        Map& m = P.out[i];
        if (r.o == r.n || !readMap(r, m)) return ERR_FORMAT;
        const TxOutput& o = P.tx.out[i];
        const uint8_t* script = P.tx.p + o.script.off;
        totalOut += o.value;   // each <= MAX_MONEY and at most 16 of them: no overflow

        bool change = false;
        for (size_t k = 0; k < m.count; k++) {
            if (m.rec[k].key[0] != OUT_BIP32_DERIVATION) continue;
            uint32_t path[5];
            uint8_t pk[33];
            // Only branch 1 can be change; a receive-branch or unverifiable key is shown
            if (checkDerivation(m.rec[k], opt, 0x2, path, pk) == 1 && isP2wpkhOf(script, o.script.len, pk)) {
                change = true;
            }
        }
        if (change) {
            R.changeSats += o.value;
        } else {
            DisplayOutput& d = R.outputs[R.outputCount++];
            describeScript(script, o.script.len, opt.testnet, d.address, sizeof(d.address));
            d.amountSats = o.value;
            R.externalSats += o.value;
        }
    }
    if (r.o != r.n) return ERR_FORMAT;   // nothing may follow the last output map

    for (size_t i = 0; i < P.tx.ni; i++) {
        if (P.ours[i]) R.oursInputs++;
        else R.foreignInputs++;
    }
    if (R.oursInputs == 0) return ERR_NOT_OURS;
    if (totalOut > totalIn || totalIn - totalOut > MAX_FEE_SATS) return ERR_FEE;
    R.feeSats = totalIn - totalOut;
    return OK;
}

}  // namespace

// One parse state shared by analyze() and sign() (~14 KB on the ESP32): the firmware handles
// one PSBT at a time, so it lives in .bss instead of the small loop() stack.
static Parsed s_parsed;

Error analyze(const uint8_t* psbt, size_t psbtLen, const Options& opt, Result* result) {
    if (!result) return ERR_ARGS;
    return parseAndCheck(psbt, psbtLen, opt, s_parsed, result);
}

Error sign(const uint8_t* psbt, size_t psbtLen, const Options& opt,
           uint8_t* out, size_t outCapacity, size_t* outLen, Result* result) {
    if (!out || !outLen || !opt.sign) return ERR_ARGS;
    *outLen = 0;
    Parsed& P = s_parsed;
    Error e = parseAndCheck(psbt, psbtLen, opt, P, result);
    if (e != OK) return e;

    // Copy the PSBT, inserting one PARTIAL_SIG record before each of our inputs' terminators
    size_t o = 0, copied = 0;
    for (size_t i = 0; i < P.tx.ni; i++) {
        if (!P.ours[i]) continue;
        bool haveSig = false;   // leave an existing signature for this key untouched
        for (size_t k = 0; k < P.in[i].count; k++) {
            const Record& rc = P.in[i].rec[k];
            if (rc.key[0] == IN_PARTIAL_SIG && rc.keyLen == 34 && memcmp(rc.key + 1, P.pubkey[i], 33) == 0) haveSig = true;
        }
        if (haveSig) continue;

        uint8_t digest[32], der[80];
        size_t derLen = 0;
        if (!bip143Sighash(psbt + P.txOff, P.txLen, i, P.inValue[i], P.pubkey[i], digest) ||
            !opt.sign(P.path[i], 5, digest, der, sizeof(der) - 1, &derLen, opt.signCtx) ||
            derLen < 8 || derLen > 72) {
            mbedtls_platform_zeroize(digest, sizeof(digest));
            return ERR_SIGN;
        }
        mbedtls_platform_zeroize(digest, sizeof(digest));
        der[derLen++] = 0x01;   // SIGHASH_ALL

        size_t insertAt = P.in[i].end;
        size_t need = (insertAt - copied) + 1 + 34 + 1 + derLen;
        if (need > outCapacity - o) return ERR_SPACE;
        memcpy(out + o, psbt + copied, insertAt - copied);
        o += insertAt - copied;
        copied = insertAt;
        out[o++] = 34;                      // key length
        out[o++] = IN_PARTIAL_SIG;
        memcpy(out + o, P.pubkey[i], 33);
        o += 33;
        o += writeCompact(out + o, derLen);
        memcpy(out + o, der, derLen);
        o += derLen;
    }
    if (psbtLen - copied > outCapacity - o) return ERR_SPACE;
    memcpy(out + o, psbt + copied, psbtLen - copied);
    o += psbtLen - copied;
    *outLen = o;
    return OK;
}

const char* errorName(Error e) {
    static const char* names[] = {"OK", "bad arguments", "not a valid PSBT", "unsupported transaction",
                                  "missing or inconsistent UTXO", "sighash not ALL", "ownership check failed",
                                  "no input from this wallet", "fee invalid or too high", "input already finalized",
                                  "signing failed", "output too large"};
    return (unsigned)e < sizeof(names) / sizeof(names[0]) ? names[e] : "unknown";
}

}  // namespace PsbtCore
