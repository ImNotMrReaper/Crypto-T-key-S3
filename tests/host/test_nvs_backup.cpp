// Full device-bound backup (src/nvs_backup.cpp): bundle format + encrypted container.
#include "test.h"
#include "nvs_backup.h"
#include <random>
#include <string>
#include <string.h>
#include <vector>

namespace NvsBackup { void hostRandom(uint8_t* out, size_t len) { static std::mt19937 r(7); for (size_t i = 0; i < len; i++) out[i] = (uint8_t)r(); } }
using namespace NvsBackup;

static const uint8_t CHIP[6] = {0x7c, 0xdf, 0xa1, 0x00, 0xbe, 0x9c};
static const uint8_t OTHER_CHIP[6] = {0x7c, 0xdf, 0xa1, 0x00, 0xbe, 0x9d};
static const uint32_t IT = MIN_ITERS;   // fast for tests; the device uses DEFAULT_ITERS

struct Seen { std::vector<std::string> keys; std::vector<std::vector<uint8_t>> vals; };
static bool collect(const Record& r, void* p) {
    Seen* s = (Seen*)p;
    s->keys.push_back(std::string(r.ns) + "/" + r.key + "#" + std::to_string(r.type));
    s->vals.push_back(std::vector<uint8_t>(r.value, r.value + r.len));
    return true;
}

static size_t sampleBundle(uint8_t* b, size_t cap) {
    size_t len;
    uint8_t u8 = 3, blob[300];
    uint32_t u32 = 0xA1B2C3D4;
    for (int i = 0; i < 300; i++) blob[i] = (uint8_t)(i * 7);
    CHECK(packBegin(b, cap, &len));
    CHECK(pack(b, cap, &len, "vault_sec", "pin_len", REC_U8, &u8, 1));
    CHECK(pack(b, cap, &len, "vault_sec", "pin_hash", REC_BLOB, blob, 32));
    CHECK(pack(b, cap, &len, "fido_rk", "rk07", REC_BLOB, blob, 300));
    CHECK(pack(b, cap, &len, "vault_sec", "setup_pw2", REC_STR, (const uint8_t*)"20000$ab$cd", 12));
    CHECK(pack(b, cap, &len, "fido_vault", "sig_counter", REC_U32, (uint8_t*)&u32, 4));
    int32_t i32 = -12345;   // Preferences::putInt stores I32 (the first device backup failed on it)
    CHECK(pack(b, cap, &len, "portfolio_v2", "idx", REC_I32, (uint8_t*)&i32, 4));
    return len;
}

TEST(bundle_round_trip) {
    static uint8_t b[4096];
    size_t len = sampleBundle(b, sizeof(b));
    Seen s;
    CHECK(unpack(b, len, collect, &s));
    CHECK(s.keys.size() == 6);
    CHECK(s.keys[5] == "portfolio_v2/idx#20" && s.vals[5].size() == 4);
    CHECK(s.keys[0] == "vault_sec/pin_len#1" && s.vals[0][0] == 3);
    CHECK(s.keys[2] == "fido_rk/rk07#66" && s.vals[2].size() == 300 && s.vals[2][299] == (uint8_t)(299 * 7));
    CHECK(s.keys[4] == "fido_vault/sig_counter#4");
}

TEST(pack_rejects_bad_fields) {
    static uint8_t b[256];
    size_t len;
    uint8_t v[8] = {0};
    CHECK(packBegin(b, sizeof(b), &len));
    CHECK(!pack(b, sizeof(b), &len, "", "k", REC_U8, v, 1));                   // empty namespace
    CHECK(!pack(b, sizeof(b), &len, "0123456789abcdef", "k", REC_U8, v, 1));   // 16 chars: too long
    CHECK(!pack(b, sizeof(b), &len, "ns", "k", REC_U8, v, 2));                 // u8 with 2 bytes
    CHECK(!pack(b, sizeof(b), &len, "ns", "k", REC_I64, v, 4));                // i64 with 4 bytes
    CHECK(!pack(b, sizeof(b), &len, "ns", "k", 0x99, v, 1));                   // unknown type
    uint8_t big[300] = {0};
    CHECK(!pack(b, sizeof(b), &len, "ns", "k", REC_BLOB, big, 300));          // doesn't fit
    CHECK(len == 7);                                                           // nothing half-written
}

TEST(unpack_rejects_every_truncation_and_trailing_byte) {
    static uint8_t b[4096];
    size_t len = sampleBundle(b, sizeof(b));
    for (size_t n = 0; n < len; n++) CHECK(!unpack(b, n, nullptr, nullptr));
    b[len] = 0;
    CHECK(!unpack(b, len + 1, nullptr, nullptr));
    CHECK(unpack(b, len, nullptr, nullptr));
}

TEST(unpack_survives_random_corruption) {
    static uint8_t b[4096], m[4096];
    size_t len = sampleBundle(b, sizeof(b));
    std::mt19937 r(99);
    for (int i = 0; i < 20000; i++) {
        memcpy(m, b, len);
        for (int e = 0; e < 1 + (int)(r() % 3); e++) m[r() % len] = (uint8_t)r();
        Seen s;
        unpack(m, len, collect, &s);   // must never crash (ASan) or read out of bounds
    }
    CHECK(true);
}

TEST(seal_open_round_trip) {
    static uint8_t b[4096], sealed[5000], plain[4096];
    size_t len = sampleBundle(b, sizeof(b)), sl, pl;
    CHECK(seal(b, len, "correct horse battery", CHIP, IT, sealed, sizeof(sealed), &sl));
    CHECK(sl == sealedSize(len));
    CHECK(memmem(sealed, sl, "setup_pw2", 9) == nullptr);   // nothing readable on the card
    CHECK(open(sealed, sl, "correct horse battery", CHIP, plain, sizeof(plain), &pl));
    CHECK(pl == len && memcmp(plain, b, len) == 0);
}

TEST(wrong_password_or_other_chip_fails) {
    static uint8_t b[4096], sealed[5000], plain[4096];
    size_t len = sampleBundle(b, sizeof(b)), sl, pl = 1;
    CHECK(seal(b, len, "correct horse battery", CHIP, IT, sealed, sizeof(sealed), &sl));
    CHECK(!open(sealed, sl, "correct horse batterY", CHIP, plain, sizeof(plain), &pl) && pl == 0);
    CHECK(!open(sealed, sl, "correct horse battery", OTHER_CHIP, plain, sizeof(plain), &pl));   // another T-Key
    for (size_t i = 0; i < sizeof(plain); i++) if (plain[i]) { CHECK(false); break; }             // no plaintext left
}

TEST(every_header_and_body_byte_is_authenticated) {
    static uint8_t b[512], sealed[800], plain[512];
    size_t len, sl, pl;
    uint8_t v = 1;
    CHECK(packBegin(b, sizeof(b), &len));
    CHECK(pack(b, sizeof(b), &len, "vault_sec", "k", REC_U8, &v, 1));
    CHECK(seal(b, len, "pw", CHIP, IT, sealed, sizeof(sealed), &sl));
    for (size_t i = 0; i < sl; i++) {
        sealed[i] ^= 0x01;
        CHECK(!open(sealed, sl, "pw", CHIP, plain, sizeof(plain), &pl));
        sealed[i] ^= 0x01;
    }
    CHECK(open(sealed, sl, "pw", CHIP, plain, sizeof(plain), &pl));
}

TEST(hostile_headers_rejected_before_work) {
    static uint8_t b[512], sealed[800], plain[512];
    size_t len, sl, pl;
    uint8_t v = 1;
    CHECK(packBegin(b, sizeof(b), &len));
    CHECK(pack(b, sizeof(b), &len, "vault_sec", "k", REC_U8, &v, 1));
    CHECK(seal(b, len, "pw", CHIP, IT, sealed, sizeof(sealed), &sl));
    CHECK(!open(sealed, sl - 1, "pw", CHIP, plain, sizeof(plain), &pl));           // truncated
    CHECK(!open(sealed, sl, "pw", CHIP, plain, 3, &pl));                           // too small a buffer
    CHECK(!seal(b, len, "pw", CHIP, MAX_ITERS + 1, sealed, sizeof(sealed), &sl));  // absurd iterations
    CHECK(!seal(b, len, "", CHIP, IT, sealed, sizeof(sealed), &sl));               // empty password
}

int main() {
    RUN(bundle_round_trip);
    RUN(pack_rejects_bad_fields);
    RUN(unpack_rejects_every_truncation_and_trailing_byte);
    RUN(unpack_survives_random_corruption);
    RUN(seal_open_round_trip);
    RUN(wrong_password_or_other_chip_fails);
    RUN(every_header_and_body_byte_is_authenticated);
    RUN(hostile_headers_rejected_before_work);
    DONE();
}
