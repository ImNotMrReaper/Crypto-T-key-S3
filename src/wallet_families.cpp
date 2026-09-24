#include "wallet_families.h"
#include "bip32_engine.h"
#include <KeccakCore.h>
#include <SHA3.h>
#include <BLAKE2b.h>
#include <Ed25519.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>

extern "C" {
#include "mbedtls/ripemd160.h"
}

#define HARD 0x80000000u

// ─── Hash helpers ────────────────────────────────────────────────────────────

static void hash160(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint8_t sha[32];
    mbedtls_sha256(data, len, sha, 0);
    mbedtls_ripemd160(sha, 32, out);
}

static void keccak256(const uint8_t* data, size_t len, uint8_t out[32]) {
    KeccakCore k;
    k.setCapacity(512);
    k.update(data, len);
    k.pad(0x01);
    k.extract(out, 32);
}

static void ethAddressBytes(const uint8_t pubUncompressed[65], uint8_t out[20]) {
    uint8_t h[32];
    keccak256(pubUncompressed + 1, 64, h);
    memcpy(out, h + 12, 20);
}

static void toHex(const uint8_t* d, size_t n, char* out) {
    static const char* HEXCH = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = HEXCH[d[i] >> 4];
        out[2 * i + 1] = HEXCH[d[i] & 15];
    }
    out[2 * n] = '\0';
}

// ─── Encodings ───────────────────────────────────────────────────────────────

static const char* B58_BTC = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char* B58_XRP = "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";

static void base58(const uint8_t* data, size_t len, const char* alphabet, char* out, size_t outMax) {
    uint8_t buf[64];
    memcpy(buf, data, len);
    char tmp[100];
    size_t t = 0, zeros = 0;
    while (zeros < len && buf[zeros] == 0) zeros++;
    size_t start = zeros;
    while (start < len) {
        uint32_t rem = 0;
        for (size_t i = start; i < len; i++) {
            uint32_t acc = rem * 256 + buf[i];
            buf[i] = acc / 58;
            rem = acc % 58;
        }
        tmp[t++] = alphabet[rem];
        while (start < len && buf[start] == 0) start++;
    }
    size_t o = 0;
    for (size_t i = 0; i < zeros && o + 1 < outMax; i++) out[o++] = alphabet[0];
    while (t > 0 && o + 1 < outMax) out[o++] = tmp[--t];
    out[o] = '\0';
}

static void base58check(const uint8_t* prefix, size_t plen, const uint8_t* payload, size_t len,
                        const char* alphabet, char* out, size_t outMax) {
    uint8_t buf[48];
    memcpy(buf, prefix, plen);
    memcpy(buf + plen, payload, len);
    uint8_t h1[32], h2[32];
    mbedtls_sha256(buf, plen + len, h1, 0);
    mbedtls_sha256(h1, 32, h2, 0);
    memcpy(buf + plen + len, h2, 4);
    base58(buf, plen + len + 4, alphabet, out, outMax);
}

static size_t convertBits(const uint8_t* in, size_t inLen, uint8_t* out, int fromBits, int toBits) {
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    const uint32_t maxv = (1u << toBits) - 1;
    for (size_t i = 0; i < inLen; i++) {
        acc = (acc << fromBits) | in[i];
        bits += fromBits;
        while (bits >= toBits) {
            bits -= toBits;
            out[n++] = (acc >> bits) & maxv;
        }
    }
    if (bits > 0) out[n++] = (acc << (toBits - bits)) & maxv;
    return n;
}

static const char* B32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

// Plain bech32 (BIP-173 checksum, no witness version): Cosmos-SDK style addresses
static void bech32Plain(const char* hrp, const uint8_t* data, size_t len, char* out) {
    uint8_t v[80];
    size_t n = convertBits(data, len, v, 8, 5);
    auto polymod = [](uint32_t chk) {
        static const uint32_t G[5] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
        uint32_t b = chk >> 25;
        chk = (chk & 0x1ffffff) << 5;
        for (int i = 0; i < 5; i++) if ((b >> i) & 1) chk ^= G[i];
        return chk;
    };
    uint32_t chk = 1;
    for (const char* p = hrp; *p; p++) chk = polymod(chk) ^ (*p >> 5);
    chk = polymod(chk);
    for (const char* p = hrp; *p; p++) chk = polymod(chk) ^ (*p & 31);
    for (size_t i = 0; i < n; i++) chk = polymod(chk) ^ v[i];
    for (int i = 0; i < 6; i++) chk = polymod(chk);
    chk ^= 1;
    size_t o = strlen(hrp);
    memcpy(out, hrp, o);
    out[o++] = '1';
    for (size_t i = 0; i < n; i++) out[o++] = B32_CHARSET[v[i]];
    for (int i = 0; i < 6; i++) out[o++] = B32_CHARSET[(chk >> (5 * (5 - i))) & 31];
    out[o] = '\0';
}

// Bitcoin Cash CashAddr (P2PKH, version byte 0)
static void cashAddr(const uint8_t h160[20], char* out) {
    uint8_t payload[21];
    payload[0] = 0x00;
    memcpy(payload + 1, h160, 20);
    uint8_t v[40];
    size_t n = convertBits(payload, 21, v, 8, 5);
    auto polymod = [](const uint8_t* d, size_t len) {
        uint64_t c = 1;
        for (size_t i = 0; i < len; i++) {
            uint8_t c0 = c >> 35;
            c = ((c & 0x07ffffffffULL) << 5) ^ d[i];
            if (c0 & 0x01) c ^= 0x98f2bc8e61ULL;
            if (c0 & 0x02) c ^= 0x79b76d99e2ULL;
            if (c0 & 0x04) c ^= 0xf33e5fb3c4ULL;
            if (c0 & 0x08) c ^= 0xae2eabe2a8ULL;
            if (c0 & 0x10) c ^= 0x1e4f43e470ULL;
        }
        return c ^ 1;
    };
    const char* prefix = "bitcoincash";
    uint8_t chk[64];
    size_t k = 0;
    for (const char* p = prefix; *p; p++) chk[k++] = *p & 31;
    chk[k++] = 0;
    memcpy(chk + k, v, n);
    k += n;
    for (int i = 0; i < 8; i++) chk[k++] = 0;
    uint64_t mod = polymod(chk, k);
    size_t o = strlen(prefix);
    memcpy(out, prefix, o);
    out[o++] = ':';
    for (size_t i = 0; i < n; i++) out[o++] = B32_CHARSET[v[i]];
    for (int i = 0; i < 8; i++) out[o++] = B32_CHARSET[(mod >> (5 * (7 - i))) & 31];
    out[o] = '\0';
}

// Stellar strkey: base32(version || key || crc16-xmodem little-endian)
static void stellarStrkey(const uint8_t pub[32], char* out) {
    uint8_t d[35];
    d[0] = 6 << 3;  // 'G' account id
    memcpy(d + 1, pub, 32);
    uint16_t crc = 0;
    for (int i = 0; i < 33; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    d[33] = crc & 0xff;
    d[34] = crc >> 8;
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    uint8_t v[64];
    size_t n = convertBits(d, 35, v, 8, 5);
    for (size_t i = 0; i < n; i++) out[i] = A[v[i]];
    out[n] = '\0';
}

// ─── Key derivation ──────────────────────────────────────────────────────────

static bool secpNode(const uint8_t seed[64], const uint32_t* path, size_t depth, Bip32Node* node) {
    Bip32Node master;
    bool ok = Bip32Engine::initMasterNode(seed, &master) && Bip32Engine::derivePath(&master, path, depth, node);
    Bip32Engine::secureZero(&master, sizeof(master));
    return ok;
}

// SLIP-0010 ed25519 (all hardened), returns the 32-byte public key
static bool ed25519Pub(const uint8_t seed[64], const uint32_t* path, size_t depth, uint8_t pub[32]) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    uint8_t I[64], k[32], c[32];
    if (mbedtls_md_hmac(md, (const uint8_t*)"ed25519 seed", 12, seed, 64, I) != 0) return false;
    memcpy(k, I, 32);
    memcpy(c, I + 32, 32);
    for (size_t s = 0; s < depth; s++) {
        uint8_t data[37];
        data[0] = 0;
        memcpy(data + 1, k, 32);
        uint32_t idx = path[s] | HARD;
        data[33] = idx >> 24; data[34] = idx >> 16; data[35] = idx >> 8; data[36] = idx;
        if (mbedtls_md_hmac(md, c, 32, data, 37, I) != 0) return false;
        memcpy(k, I, 32);
        memcpy(c, I + 32, 32);
        mbedtls_platform_zeroize(data, sizeof(data));
    }
    Ed25519::derivePublicKey(pub, k);
    mbedtls_platform_zeroize(I, sizeof(I));
    mbedtls_platform_zeroize(k, sizeof(k));
    mbedtls_platform_zeroize(c, sizeof(c));
    return true;
}

bool WalletFamilies::deriveAddress(WalletFamily fam, const uint8_t seed[64], char out[FAMILY_ADDR_LEN]) {
    out[0] = '\0';
    Bip32Node n;
    uint8_t h[32], pub[32];
    bool ok = true;
    switch (fam) {
        case FAM_BTC: {
            uint8_t priv[32];
            ok = Bip32Engine::deriveBtcSegwitAddress(seed, out, priv);
            mbedtls_platform_zeroize(priv, 32);
            break;
        }
        case FAM_EVM: {
            uint8_t priv[32];
            ok = Bip32Engine::deriveEthAddress(seed, out, priv);
            mbedtls_platform_zeroize(priv, 32);
            break;
        }
        case FAM_SOL: {
            uint8_t priv[32];
            ok = Bip32Engine::deriveSolAddress(seed, out, priv);
            mbedtls_platform_zeroize(priv, 32);
            break;
        }
        case FAM_DOGE: {
            const uint32_t p[] = {44 | HARD, 3 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            hash160(n.pubKeyCompressed, 33, h);
            const uint8_t pre[] = {0x1E};
            base58check(pre, 1, h, 20, B58_BTC, out, FAMILY_ADDR_LEN);
            break;
        }
        case FAM_LTC: {
            const uint32_t p[] = {84 | HARD, 2 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            hash160(n.pubKeyCompressed, 33, h);
            Bip32Engine::bech32Encode("ltc", h, 20, out);
            break;
        }
        case FAM_TRX: {
            const uint32_t p[] = {44 | HARD, 195 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            ethAddressBytes(n.pubKeyUncompressed, h);
            const uint8_t pre[] = {0x41};
            base58check(pre, 1, h, 20, B58_BTC, out, FAMILY_ADDR_LEN);
            break;
        }
        case FAM_XRP: {
            const uint32_t p[] = {44 | HARD, 144 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            hash160(n.pubKeyCompressed, 33, h);
            const uint8_t pre[] = {0x00};
            base58check(pre, 1, h, 20, B58_XRP, out, FAMILY_ADDR_LEN);
            break;
        }
        case FAM_ATOM: {
            const uint32_t p[] = {44 | HARD, 118 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            hash160(n.pubKeyCompressed, 33, h);
            bech32Plain("cosmos", h, 20, out);
            break;
        }
        case FAM_INJ: {
            const uint32_t p[] = {44 | HARD, 60 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            ethAddressBytes(n.pubKeyUncompressed, h);
            bech32Plain("inj", h, 20, out);
            break;
        }
        case FAM_VET: {
            const uint32_t p[] = {44 | HARD, 818 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            ethAddressBytes(n.pubKeyUncompressed, h);
            Bip32Engine::eip55Encode(h, out);
            break;
        }
        case FAM_BCH: {
            const uint32_t p[] = {44 | HARD, 145 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            hash160(n.pubKeyCompressed, 33, h);
            cashAddr(h, out);
            break;
        }
        case FAM_ZEC: {
            const uint32_t p[] = {44 | HARD, 133 | HARD, 0 | HARD, 0, 0};
            if (!(ok = secpNode(seed, p, 5, &n))) break;
            hash160(n.pubKeyCompressed, 33, h);
            const uint8_t pre[] = {0x1C, 0xB8};
            base58check(pre, 2, h, 20, B58_BTC, out, FAMILY_ADDR_LEN);
            break;
        }
        case FAM_XLM: {
            const uint32_t p[] = {44, 148, 0};
            if (!(ok = ed25519Pub(seed, p, 3, pub))) break;
            stellarStrkey(pub, out);
            break;
        }
        case FAM_NEAR: {
            const uint32_t p[] = {44, 397, 0};
            if (!(ok = ed25519Pub(seed, p, 3, pub))) break;
            toHex(pub, 32, out);
            break;
        }
        case FAM_APT: {
            const uint32_t p[] = {44, 637, 0, 0, 0};
            if (!(ok = ed25519Pub(seed, p, 5, pub))) break;
            SHA3_256 sha3;
            sha3.update(pub, 32);
            const uint8_t scheme = 0x00;   // single-key ed25519
            sha3.update(&scheme, 1);
            sha3.finalize(h, 32);
            out[0] = '0'; out[1] = 'x';
            toHex(h, 32, out + 2);
            break;
        }
        case FAM_SUI: {
            const uint32_t p[] = {44, 784, 0, 0, 0};
            if (!(ok = ed25519Pub(seed, p, 5, pub))) break;
            BLAKE2b b2;
            b2.reset(32);
            const uint8_t flag = 0x00;     // ed25519 signature scheme flag
            b2.update(&flag, 1);
            b2.update(pub, 32);
            b2.finalize(h, 32);
            out[0] = '0'; out[1] = 'x';
            toHex(h, 32, out + 2);
            break;
        }
        default:
            ok = false;
    }
    Bip32Engine::secureZero(&n, sizeof(n));
    return ok && out[0];
}

const char* WalletFamilies::name(WalletFamily fam) {
    static const char* N[FAM_COUNT] = {"Bitcoin", "EVM", "Solana", "Dogecoin", "Litecoin", "TRON", "XRP Ledger",
                                       "Cosmos", "Injective", "Stellar", "NEAR", "Aptos", "VeChain",
                                       "Bitcoin Cash", "Sui", "Zcash"};
    return fam < FAM_COUNT ? N[fam] : "?";
}
