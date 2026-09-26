// CBOR codec unit tests.
#include "test.h"
#include "cbor_codec.h"
#include <string.h>
#include <vector>

static const uint64_t BOUNDARIES[] = {
    23, 24, 255, 256, 65535, 65536, UINT64_C(0x100000000)
};

TEST(unsigned_and_negative_boundaries_round_trip) {
    for (uint64_t n : BOUNDARIES) {
        uint8_t buf[16];
        CborEncoder enc(buf, sizeof(buf));
        CHECK(enc.encodeUnsigned(n));
        CborDecoder dec(buf, enc.getLength());
        uint64_t got = 0;
        CHECK(dec.readUnsigned(&got));
        CHECK(got == n);

        enc = CborEncoder(buf, sizeof(buf));
        CHECK(enc.encodeNegative(-1 - (int64_t)n));
        CborDecoder neg(buf, enc.getLength());
        int64_t signedGot = 0;
        CHECK(neg.readNegative(&signedGot));
        CHECK(signedGot == -1 - (int64_t)n);
    }
}

TEST(bytes_and_text_length_boundaries_round_trip) {
    const uint64_t lengths[] = {0, 23, 24, 255, 256, 65535, 65536};
    for (uint64_t n : lengths) {
        std::vector<uint8_t> data((size_t)n, 0x5A);
        std::vector<uint8_t> buf((size_t)n + 16);
        CborEncoder enc(buf.data(), buf.size());
        CHECK(enc.encodeBytes(data.data(), data.size()));
        CborDecoder dec(buf.data(), enc.getLength());
        const uint8_t* got = nullptr;
        size_t gotLen = 0;
        CHECK(dec.readBytes(&got, &gotLen));
        CHECK(gotLen == data.size());
        CHECK(gotLen == 0 || memcmp(got, data.data(), gotLen) == 0);

        std::string str((size_t)n, 'x');
        enc = CborEncoder(buf.data(), buf.size());
        CHECK(enc.encodeText(str.c_str()));
        CborDecoder textDec(buf.data(), enc.getLength());
        std::vector<char> out((size_t)n + 1);
        CHECK(textDec.readText(out.data(), out.size()));
        CHECK(strlen(out.data()) == (size_t)n);
        CHECK(str == out.data());
    }
}

TEST(bool_round_trip) {
    for (bool value : {false, true}) {
        uint8_t buf[2];
        CborEncoder enc(buf, sizeof(buf));
        CHECK(enc.encodeBool(value));
        CborDecoder dec(buf, enc.getLength());
        bool got = !value;
        CHECK(dec.readBool(&got));
        CHECK(got == value);
    }
}

TEST(map_and_array_headers_round_trip) {
    for (uint64_t n : BOUNDARIES) {
        uint8_t buf[16];
        CborEncoder enc(buf, sizeof(buf));
        CHECK(enc.encodeMapHeader((size_t)n));
        CborDecoder map(buf, enc.getLength());
        size_t mapCount = 0;
        CHECK(map.readMapHeader(&mapCount));
        CHECK(mapCount == (size_t)n);

        enc = CborEncoder(buf, sizeof(buf));
        CHECK(enc.encodeArrayHeader((size_t)n));
        CborDecoder array(buf, enc.getLength());
        size_t arrayCount = 0;
        CHECK(array.readArrayHeader(&arrayCount));
        CHECK(arrayCount == (size_t)n);
    }
}

TEST(wrap_attempts_rejected_for_bytes_text_and_skip) {
    const uint8_t bytes[] = {0x5B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7};
    CborDecoder bytesDec(bytes, sizeof(bytes));
    const uint8_t* p = nullptr;
    size_t len = 0;
    CHECK(!bytesDec.readBytes(&p, &len));

    const uint8_t text[] = {0x7B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7};
    CborDecoder textDec(text, sizeof(text));
    char out[8];
    CHECK(!textDec.readText(out, sizeof(out)));

    CborDecoder skipBytes(bytes, sizeof(bytes));
    CHECK(!skipBytes.skipValue());
    CborDecoder skipText(text, sizeof(text));
    CHECK(!skipText.skipValue());

    const uint8_t bytes4G[] = {0x5A, 0x00, 0x00, 0x00, 0x01};
    CborDecoder bytes4GDec(bytes4G, sizeof(bytes4G));
    CHECK(!bytes4GDec.readBytes(&p, &len));
    const uint8_t text4G[] = {0x7A, 0x00, 0x00, 0x00, 0x01};
    CborDecoder text4GDec(text4G, sizeof(text4G));
    CHECK(!text4GDec.readText(out, sizeof(out)));
}

TEST(truncated_ctap_like_map_rejected_at_every_byte) {
    // Map with rp/user text, client data hash bytes, and nested options/extension values.
    const uint8_t sample[] = {
        0xA4,
        0x01, 0x02,
        0x02, 0x43, 0xAA, 0xBB, 0xCC,
        0x03, 0x63, 'r', 'p', '1',
        0x04, 0x82, 0xF4, 0xA1, 0x61, 'x', 0x01
    };
    CborDecoder complete(sample, sizeof(sample));
    CHECK(complete.skipValue());
    CHECK(!complete.hasMore());
    for (size_t n = 0; n < sizeof(sample); n++) {
        CborDecoder truncated(sample, n);
        CHECK(!truncated.skipValue());
    }
}

TEST(depth_limit_and_indefinite_rejection) {
    uint8_t nested[32];
    for (size_t depth = 0; depth <= 10; depth++) {
        memset(nested, 0x81, depth);
        nested[depth] = 0x00;
        CborDecoder dec(nested, depth + 1);
        bool ok = dec.skipValue();
        CHECK(ok == (depth <= 8));
    }
    const uint8_t indefinite[] = {0x9F, 0xFF}; // indefinite array / break
    CborDecoder dec(indefinite, sizeof(indefinite));
    size_t count = 0;
    CHECK(!dec.readArrayHeader(&count));
    CborDecoder skip(indefinite, sizeof(indefinite));
    CHECK(!skip.skipValue());
}

TEST(read_text_truncates_and_rejects_zero_capacity) {
    const uint8_t text[] = {0x65, 'h', 'e', 'l', 'l', 'o'};
    char out[4] = {};
    CborDecoder dec(text, sizeof(text));
    CHECK(dec.readText(out, sizeof(out)));
    CHECK(strcmp(out, "hel") == 0);
    CHECK(!dec.hasMore());

    CborDecoder zeroCap(text, sizeof(text));
    CHECK(!zeroCap.readText(out, 0));
}

int main() {
    RUN(unsigned_and_negative_boundaries_round_trip);
    RUN(bytes_and_text_length_boundaries_round_trip);
    RUN(bool_round_trip);
    RUN(map_and_array_headers_round_trip);
    RUN(wrap_attempts_rejected_for_bytes_text_and_skip);
    RUN(truncated_ctap_like_map_rejected_at_every_byte);
    RUN(depth_limit_and_indefinite_rejection);
    RUN(read_text_truncates_and_rejects_zero_capacity);
    DONE();
}
