#include "nvs_backup.h"
#include <stdlib.h>
#include <string.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>
#ifndef HOST_TEST
#include <nvs.h>
#include <esp_mac.h>
#include "crypto_p256.h"
#endif

namespace NvsBackup {

const char* const NAMESPACES[] = {"vault_sec", "wallet_seed", "fido_vault", "fido_rk", "wifi_cfg",
                                  "ui_theme", "emerg_pol", "portfolio_v2", "portfolio_sec", nullptr};

static const uint8_t BUNDLE_MAGIC[4] = {'T', 'K', 'F', 'B'};
static const uint8_t BUNDLE_VERSION  = 1;
static const char    FILE_MAGIC[9]   = {'T', 'K', 'E', 'Y', '_', 'F', 'U', 'L', 'L'};
static const uint16_t FILE_VERSION   = 1;
static const size_t  SALT_LEN = 16, IV_LEN = 12, TAG_LEN = 16;
static const size_t  HEADER_LEN = sizeof(FILE_MAGIC) + 2 + 4 + SALT_LEN + IV_LEN + 4;   // 47

static void wr16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (24 - 8 * i)); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static bool validName(const char* s) {
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > 15) return false;
    for (size_t i = 0; i < n; i++) if (s[i] < 0x21 || s[i] > 0x7E) return false;
    return true;
}

static bool validType(uint8_t t) {
    return isIntType(t) || t == REC_STR || t == REC_BLOB;
}

// ─── Bundle ─────────────────────────────────────────────────────────────────

bool packBegin(uint8_t* buf, size_t cap, size_t* len) {
    if (!buf || !len || cap < 7) return false;
    memcpy(buf, BUNDLE_MAGIC, 4);
    buf[4] = BUNDLE_VERSION;
    wr16(buf + 5, 0);
    *len = 7;
    return true;
}

bool pack(uint8_t* buf, size_t cap, size_t* len, const char* ns, const char* key,
          uint8_t type, const uint8_t* value, uint16_t valueLen) {
    if (!buf || !len || *len < 7 || !validName(ns) || !validName(key) || !validType(type)) return false;
    if (isIntType(type) && valueLen != intWidth(type)) return false;
    if (valueLen && !value) return false;
    size_t nl = strlen(ns) + 1, kl = strlen(key) + 1, need = nl + kl + 1 + 2 + valueLen;
    uint16_t count = rd16(buf + 5);
    if (count == 0xFFFF || need > cap - *len) return false;
    uint8_t* p = buf + *len;
    memcpy(p, ns, nl);
    memcpy(p + nl, key, kl);
    p[nl + kl] = type;
    wr16(p + nl + kl + 1, valueLen);
    if (valueLen) memcpy(p + nl + kl + 3, value, valueLen);
    *len += need;
    wr16(buf + 5, count + 1);
    return true;
}

// A NUL-terminated name of 1..15 printable characters starting at off
static bool readName(const uint8_t* buf, size_t len, size_t* off, char out[16]) {
    size_t i = 0;
    while (*off + i < len && buf[*off + i] != 0) {
        if (i == 15) return false;
        out[i] = (char)buf[*off + i];
        i++;
    }
    if (*off + i >= len || i == 0) return false;   // no terminator, or empty
    out[i] = '\0';
    *off += i + 1;
    return validName(out);
}

bool unpack(const uint8_t* buf, size_t len, bool (*onRecord)(const Record& r, void* ctx), void* ctx) {
    if (!buf || len < 7 || memcmp(buf, BUNDLE_MAGIC, 4) != 0 || buf[4] != BUNDLE_VERSION) return false;
    uint16_t count = rd16(buf + 5);
    size_t off = 7;
    for (uint16_t i = 0; i < count; i++) {
        Record r;
        if (!readName(buf, len, &off, r.ns) || !readName(buf, len, &off, r.key)) return false;
        if (3 > len - off) return false;
        r.type = buf[off];
        r.len = rd16(buf + off + 1);
        off += 3;
        if (!validType(r.type) || r.len > len - off) return false;
        if (isIntType(r.type) && r.len != intWidth(r.type)) return false;
        r.value = buf + off;
        off += r.len;
        if (onRecord && !onRecord(r, ctx)) return false;
    }
    return off == len;   // nothing may follow the last record
}

// ─── Encrypted container ────────────────────────────────────────────────────

size_t sealedSize(size_t plainLen) {
    return HEADER_LEN + plainLen + TAG_LEN;
}

static bool deriveKey(const char* password, const uint8_t* salt, const uint8_t chipId[CHIP_ID_LEN],
                      uint32_t iters, uint8_t key[32]) {
    uint8_t saltChip[SALT_LEN + CHIP_ID_LEN];
    memcpy(saltChip, salt, SALT_LEN);
    memcpy(saltChip + SALT_LEN, chipId, CHIP_ID_LEN);   // binds the key to this chip
    int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char*)password, strlen(password),
                                           saltChip, sizeof(saltChip), iters, 32, key);
    mbedtls_platform_zeroize(saltChip, sizeof(saltChip));
    return rc == 0;
}

#ifdef HOST_TEST
void hostRandom(uint8_t* out, size_t len);   // provided by the test
#define RANDOM(p, n) hostRandom(p, n)
#else
#define RANDOM(p, n) CryptoP256::secureRandom(p, n)
#endif

bool seal(const uint8_t* plain, size_t plainLen, const char* password, const uint8_t chipId[CHIP_ID_LEN],
          uint32_t iters, uint8_t* out, size_t outCap, size_t* outLen) {
    if (outLen) *outLen = 0;
    if (!plain || !password || !password[0] || !chipId || !out || !outLen) return false;
    if (plainLen == 0 || plainLen > MAX_BLOB || iters < MIN_ITERS || iters > MAX_ITERS) return false;
    if (outCap < sealedSize(plainLen)) return false;
    uint8_t* h = out;
    memcpy(h, FILE_MAGIC, sizeof(FILE_MAGIC));
    wr16(h + 9, FILE_VERSION);
    wr32(h + 11, iters);
    RANDOM(h + 15, SALT_LEN);
    RANDOM(h + 31, IV_LEN);
    wr32(h + 43, (uint32_t)plainLen);
    uint8_t key[32];
    if (!deriveKey(password, h + 15, chipId, iters, key)) return false;
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
              mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plainLen, h + 31, IV_LEN, h, HEADER_LEN,
                                        plain, out + HEADER_LEN, TAG_LEN, out + HEADER_LEN + plainLen) == 0;
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(key, sizeof(key));
    if (!ok) {
        mbedtls_platform_zeroize(out, outCap);
        return false;
    }
    *outLen = sealedSize(plainLen);
    return true;
}

bool open(const uint8_t* in, size_t inLen, const char* password, const uint8_t chipId[CHIP_ID_LEN],
          uint8_t* plainOut, size_t plainCap, size_t* plainLen) {
    if (plainLen) *plainLen = 0;
    if (!in || !password || !chipId || !plainOut || !plainLen || inLen < HEADER_LEN + TAG_LEN) return false;
    if (memcmp(in, FILE_MAGIC, sizeof(FILE_MAGIC)) != 0 || rd16(in + 9) != FILE_VERSION) return false;
    uint32_t iters = rd32(in + 11), n = rd32(in + 43);
    // Bounds before any work: a hostile file can't demand huge iterations or buffers
    if (iters < MIN_ITERS || iters > MAX_ITERS || n == 0 || n > MAX_BLOB || n > plainCap) return false;
    if (inLen - HEADER_LEN - TAG_LEN != n) return false;   // exact size: no truncation, no trailing data
    uint8_t key[32];
    if (!deriveKey(password, in + 15, chipId, iters, key)) return false;
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
              mbedtls_gcm_auth_decrypt(&gcm, n, in + 31, IV_LEN, in, HEADER_LEN, in + HEADER_LEN + n, TAG_LEN,
                                       in + HEADER_LEN, plainOut) == 0;
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(key, sizeof(key));
    if (!ok) {
        mbedtls_platform_zeroize(plainOut, plainCap);
        return false;
    }
    *plainLen = n;
    return true;
}

// ─── Device side ────────────────────────────────────────────────────────────
#ifndef HOST_TEST

bool chipId(uint8_t out[CHIP_ID_LEN]) {
    return esp_efuse_mac_get_default(out) == ESP_OK;   // factory MAC, fused at manufacture
}

bool readNvs(const char* const* namespaces, uint8_t* buf, size_t cap, size_t* len) {
    if (!packBegin(buf, cap, len)) return false;
    const size_t VAL_CAP = 4000;   // largest single entry (a passkey slot or the attestation cert)
    uint8_t* val = (uint8_t*)malloc(VAL_CAP);   // heap only while a backup runs
    if (!val) return false;
    for (size_t n = 0; namespaces[n]; n++) {
        nvs_handle_t h;
        if (nvs_open(namespaces[n], NVS_READONLY, &h) != ESP_OK) continue;   // namespace not created yet
        nvs_iterator_t it = nullptr;
        esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, namespaces[n], NVS_TYPE_ANY, &it);
        bool ok = true;
        while (ok && e == ESP_OK) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            size_t sz = 0;
            uint8_t type = 0;
            if (isIntType((uint8_t)info.type)) {
                type = (uint8_t)info.type;
                sz = intWidth(type);
                uint64_t v = 0;   // every integer type read into the low bytes (little-endian)
                switch (info.type) {
                    case NVS_TYPE_U8:  ok = nvs_get_u8(h, info.key, (uint8_t*)&v) == ESP_OK; break;
                    case NVS_TYPE_I8:  ok = nvs_get_i8(h, info.key, (int8_t*)&v) == ESP_OK; break;
                    case NVS_TYPE_U16: ok = nvs_get_u16(h, info.key, (uint16_t*)&v) == ESP_OK; break;
                    case NVS_TYPE_I16: ok = nvs_get_i16(h, info.key, (int16_t*)&v) == ESP_OK; break;
                    case NVS_TYPE_U32: ok = nvs_get_u32(h, info.key, (uint32_t*)&v) == ESP_OK; break;
                    case NVS_TYPE_I32: ok = nvs_get_i32(h, info.key, (int32_t*)&v) == ESP_OK; break;
                    case NVS_TYPE_U64: ok = nvs_get_u64(h, info.key, (uint64_t*)&v) == ESP_OK; break;
                    case NVS_TYPE_I64: ok = nvs_get_i64(h, info.key, (int64_t*)&v) == ESP_OK; break;
                    default: ok = false;
                }
                memcpy(val, &v, sz);
            } else if (info.type == NVS_TYPE_STR) {
                sz = VAL_CAP;
                ok = nvs_get_str(h, info.key, (char*)val, &sz) == ESP_OK;
                type = REC_STR;
            } else if (info.type == NVS_TYPE_BLOB) {
                sz = VAL_CAP;
                ok = nvs_get_blob(h, info.key, val, &sz) == ESP_OK;
                type = REC_BLOB;
            } else {
                ok = false;   // a type the firmware never writes: refuse rather than drop it silently
            }
            ok = ok && sz <= 0xFFFF && pack(buf, cap, len, namespaces[n], info.key, type, val, (uint16_t)sz);
            e = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        nvs_close(h);
        mbedtls_platform_zeroize(val, VAL_CAP);
        if (!ok) {
            free(val);
            return false;
        }
    }
    free(val);
    return true;
}

struct WriteCtx {
    char* str;          // 4001-byte scratch for string values (heap, only during a restore)
    char current[16];
    nvs_handle_t h;
    bool open;
    bool failed;
};

static bool writeRecord(const Record& r, void* p) {
    WriteCtx* c = (WriteCtx*)p;
    if (!c->open || strcmp(c->current, r.ns) != 0) {
        if (c->open) {
            nvs_commit(c->h);
            nvs_close(c->h);
            c->open = false;
        }
        bool known = false;   // only restore namespaces a backup is allowed to contain
        for (size_t i = 0; NAMESPACES[i]; i++) known |= strcmp(NAMESPACES[i], r.ns) == 0;
        if (!known || nvs_open(r.ns, NVS_READWRITE, &c->h) != ESP_OK) return false;
        nvs_erase_all(c->h);   // the namespace becomes exactly what the backup holds
        strcpy(c->current, r.ns);
        c->open = true;
    }
    esp_err_t e;
    if (isIntType(r.type)) {
        uint64_t v = 0;
        memcpy(&v, r.value, r.len);
        switch (r.type) {
            case REC_U8:  e = nvs_set_u8(c->h, r.key, (uint8_t)v); break;
            case REC_I8:  e = nvs_set_i8(c->h, r.key, (int8_t)v); break;
            case REC_U16: e = nvs_set_u16(c->h, r.key, (uint16_t)v); break;
            case REC_I16: e = nvs_set_i16(c->h, r.key, (int16_t)v); break;
            case REC_U32: e = nvs_set_u32(c->h, r.key, (uint32_t)v); break;
            case REC_I32: e = nvs_set_i32(c->h, r.key, (int32_t)v); break;
            case REC_U64: e = nvs_set_u64(c->h, r.key, v); break;
            default:      e = nvs_set_i64(c->h, r.key, (int64_t)v); break;
        }
    } else if (r.type == REC_STR) {
        char* s = c->str;
        if (r.len >= 4001) return false;
        size_t n = (r.len && r.value[r.len - 1] == 0) ? r.len - 1 : r.len;   // stored with its NUL
        memcpy(s, r.value, n);
        s[n] = '\0';
        e = nvs_set_str(c->h, r.key, s);
    } else e = nvs_set_blob(c->h, r.key, r.value, r.len);
    return e == ESP_OK;
}

bool writeNvs(const uint8_t* bundle, size_t len) {
    if (!unpack(bundle, len, nullptr, nullptr)) return false;   // validate everything before writing anything
    WriteCtx c = {};
    c.str = (char*)malloc(4001);
    if (!c.str) return false;
    bool ok = unpack(bundle, len, writeRecord, &c);
    free(c.str);
    if (c.open) {
        nvs_commit(c.h);
        nvs_close(c.h);
    }
    return ok;
}

#endif  // HOST_TEST

}  // namespace NvsBackup
