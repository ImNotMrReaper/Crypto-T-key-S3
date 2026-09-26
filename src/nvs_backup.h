/**
 * nvs_backup.h — Full, device-bound encrypted backup of the key's settings and secrets.
 *
 * The backup (SD /vault/full.tkb) holds every NVS entry needed to rebuild this key: PIN and
 * duress hashes, the setup-password hash, the encrypted wallet seed and its device key, FIDO
 * master secret + passkeys, Wi-Fi networks, theme, emergency policy and coin selection.
 *
 * Lock: AES-256-GCM with key = PBKDF2-HMAC-SHA256(setup password, salt || chip id), where the chip
 * id is the ESP32-S3's factory-fused MAC. The file alone (e.g. the card in a computer) is useless;
 * it opens only on THIS chip and only with the setup password. The chip id survives a wipe, so a
 * wiped key can restore itself. Every header byte is authenticated (GCM AAD).
 *
 * The pure part (pack/unpack/seal/open) is host-tested; readNvs()/writeNvs() touch the real NVS.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace NvsBackup {

static const size_t   MAX_BLOB       = 24576;   // plaintext bundle cap (NVS is 20 KB)
static const uint32_t DEFAULT_ITERS  = 120000;  // ~2-3 s on the ESP32-S3 at 240 MHz
static const uint32_t MIN_ITERS      = 10000;
static const uint32_t MAX_ITERS      = 1000000;
static const size_t   CHIP_ID_LEN    = 6;

// Same codes as ESP-IDF's nvs_type_t: for integers the low nibble is the width in bytes
enum RecType : uint8_t {
    REC_U8 = 0x01, REC_I8 = 0x11, REC_U16 = 0x02, REC_I16 = 0x12,
    REC_U32 = 0x04, REC_I32 = 0x14, REC_U64 = 0x08, REC_I64 = 0x18,
    REC_STR = 0x21, REC_BLOB = 0x42
};
inline bool isIntType(uint8_t t) { return (t & 0xEF) == 0x01 || (t & 0xEF) == 0x02 || (t & 0xEF) == 0x04 || (t & 0xEF) == 0x08; }
inline uint8_t intWidth(uint8_t t) { return t & 0x0F; }

struct Record {
    char     ns[16];      // NVS namespace (max 15 chars)
    char     key[16];     // NVS key (max 15 chars)
    uint8_t  type;        // RecType
    uint16_t len;         // value length
    const uint8_t* value; // points into the bundle buffer
};

// Bundle = "TKFB" | u8 version | u16 count | records: ns\0 key\0 type u16len value
// Appends one record; false if it doesn't fit or a field is invalid.
bool pack(uint8_t* buf, size_t cap, size_t* len, const char* ns, const char* key,
          uint8_t type, const uint8_t* value, uint16_t valueLen);
bool packBegin(uint8_t* buf, size_t cap, size_t* len);
// Strictly parses a bundle; calls onRecord for each. False on ANY malformation.
bool unpack(const uint8_t* buf, size_t len, bool (*onRecord)(const Record& r, void* ctx), void* ctx);

// Encrypted container: header "TKEY_FULL" | u16 ver | u32 iters | salt[16] | iv[12] | u32 len,
// then ciphertext and a 16-byte GCM tag. The whole header is AAD.
size_t sealedSize(size_t plainLen);
bool seal(const uint8_t* plain, size_t plainLen, const char* password, const uint8_t chipId[CHIP_ID_LEN],
          uint32_t iters, uint8_t* out, size_t outCap, size_t* outLen);
bool open(const uint8_t* in, size_t inLen, const char* password, const uint8_t chipId[CHIP_ID_LEN],
          uint8_t* plainOut, size_t plainCap, size_t* plainLen);

// Device side (real NVS / eFuse MAC): snapshot the listed namespaces into a bundle, write one back.
bool readNvs(const char* const* namespaces, uint8_t* buf, size_t cap, size_t* len);
bool writeNvs(const uint8_t* bundle, size_t len);   // clears each namespace it restores first
bool chipId(uint8_t out[CHIP_ID_LEN]);

extern const char* const NAMESPACES[];   // what a full backup contains

}  // namespace NvsBackup
