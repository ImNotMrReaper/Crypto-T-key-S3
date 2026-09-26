// PSBT core tests. Vectors: BIP-143 "Native P2WPKH" (verbatim from
// https://github.com/bitcoin/bips/blob/master/bip-0143.mediawiki#native-p2wpkh, first added
// by Codex in Phase C1). End-to-end fixtures come from embit (tests/host/psbt_fixtures.h).
#include "test.h"
#include "psbt_core.h"
#include "bip32_engine.h"
#include "psbt_fixtures.h"
#include <mbedtls/ecdsa.h>
#include <mbedtls/sha256.h>
#include <random>
#include <string.h>

static size_t fromHex(const char* h, uint8_t* out) {
    size_t n = 0;
    while (h[0] && h[1]) {
        unsigned x = 0;
        sscanf(h, "%2x", &x);
        out[n++] = (uint8_t)x;
        h += 2;
    }
    return n;
}

// RFC 6979 makes k deterministic; the blinding RNG must not change the signature
static int blind(void*, unsigned char* p, size_t n) {
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(i * 7 + 1);
    return 0;
}

TEST(bip143_native_p2wpkh_vector) {
    static const char txHex[] =
        "0100000002fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f0000000000eeffffff"
        "ef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a0100000000ffffffff02202cb206"
        "000000001976a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac9093510d000000001976a9143bde42db"
        "ee7e4dbe6a21b2d50ce2f0167faa815988ac11000000";
    uint8_t tx[256], pub[33], key[32], digest[32], want[32], der[80], wantDer[80];
    size_t n = fromHex(txHex, tx), dl = 0;
    fromHex("025476c2e83188368da1ff3e292e7acafcdb3566bb0ad253f62fc70f07aeee6357", pub);
    fromHex("619c335025c7f4012e556c2a58b2506e30b8511b53ade95ea316fd8c3286feb9", key);
    fromHex("c37af31116d1b27caf68aae9e3ac82f1477929014d5b917657d0eb49478cb670", want);
    CHECK(PsbtCore::bip143Sighash(tx, n, 1, 600000000ULL, pub, digest));
    CHECK(memcmp(digest, want, 32) == 0);
    CHECK(PsbtCore::signMbedTlsSecp256k1(key, digest, blind, nullptr, der, sizeof(der), &dl));
    size_t wl = fromHex("304402203609e17b84f6a7d30c80bfa610b5b4542f32a8a0d5447a12fb1366d7f01cc44a"
                        "0220573a954c4518331561406f90300e8f3358f51928d43c212a8caed02de67eebee", wantDer);
    CHECK(dl == wl && memcmp(der, wantDer, wl) == 0);
    // a different input index gives a different digest
    uint8_t d0[32];
    CHECK(PsbtCore::bip143Sighash(tx, n, 0, 600000000ULL, pub, d0) && memcmp(d0, digest, 32) != 0);
}

TEST(sighash_rejects_bad_input) {
    uint8_t tx[256], digest[32], pub[33] = {2};
    size_t n = fromHex("0100000001", tx);
    CHECK(!PsbtCore::bip143Sighash(tx, n, 0, 1, pub, digest));
    CHECK(!PsbtCore::bip143Sighash(nullptr, 0, 0, 1, pub, digest));
}

// ─── End to end against embit fixtures (independent implementation) ─────────
// Keys come from the firmware's own BIP-32 code for the BIP-39 test mnemonic, exactly as the
// device derives them; embit produced the PSBTs, sighashes and the reference signature.
static uint8_t g_seed[64];

static bool testDerive(const uint32_t* path, size_t depth, uint8_t pub33[33], void*) {
    Bip32Node m, n;
    bool ok = Bip32Engine::initMasterNode(g_seed, &m) && Bip32Engine::derivePath(&m, path, depth, &n);
    if (ok) memcpy(pub33, n.pubKeyCompressed, 33);
    return ok;
}

static bool testSign(const uint32_t* path, size_t depth, const uint8_t d[32], uint8_t* der,
                     size_t cap, size_t* len, void*) {
    Bip32Node m, n;
    return Bip32Engine::initMasterNode(g_seed, &m) && Bip32Engine::derivePath(&m, path, depth, &n) &&
           PsbtCore::signMbedTlsSecp256k1(n.privKey, d, blind, nullptr, der, cap, len);
}

static PsbtCore::Options testOptions() {
    PsbtCore::Options o;
    memset(&o, 0, sizeof(o));
    memcpy(o.masterFingerprint, f0_master_fingerprint, 4);
    o.derive = testDerive;
    o.sign = testSign;
    return o;
}

// Every PARTIAL_SIG added by sign(): verified with mbedTLS against an independent sighash
static int verifyPartialSigs(const uint8_t* psbt, size_t len, const uint8_t* expectSighash) {
    int verified = 0;
    for (size_t i = 0; i + 36 < len; i++) {
        // 0x22 0x02 <33-byte pubkey> <len> <DER ... 0x01>
        if (psbt[i] != 0x22 || psbt[i + 1] != 0x02 || (psbt[i + 2] != 2 && psbt[i + 2] != 3)) continue;
        const uint8_t* pub = psbt + i + 2;
        size_t sl = psbt[i + 35];
        if (sl < 9 || sl > 73 || i + 36 + sl > len || psbt[i + 36] != 0x30 || psbt[i + 35 + sl] != 0x01) continue;
        mbedtls_ecp_keypair kp;
        mbedtls_ecp_keypair_init(&kp);
        bool ok = mbedtls_ecp_group_load(&kp.grp, MBEDTLS_ECP_DP_SECP256K1) == 0 &&
                  mbedtls_ecp_point_read_binary(&kp.grp, &kp.Q, pub, 33) == 0 &&
                  mbedtls_ecdsa_read_signature(&kp, expectSighash, 32, psbt + i + 36, sl - 1) == 0;
        mbedtls_ecp_keypair_free(&kp);
        if (ok) verified++;
    }
    return verified;
}

TEST(f0_keys_match_embit) {
    CHECK(Bip32Engine::mnemonicToSeed("abandon abandon abandon abandon abandon abandon abandon abandon "
                                      "abandon abandon abandon about", "", g_seed));
    uint8_t pub[33];
    const uint32_t H = 0x80000000u, recv[5] = {84 | H, 0 | H, 0 | H, 0, 0}, chg[5] = {84 | H, 0 | H, 0 | H, 1, 0};
    CHECK(testDerive(recv, 5, pub, nullptr) && memcmp(pub, f0_receive_pubkey, 33) == 0);
    CHECK(testDerive(chg, 5, pub, nullptr) && memcmp(pub, f0_change_pubkey, 33) == 0);
}

TEST(f1_single_payment_with_change) {
    PsbtCore::Result r;
    PsbtCore::Options o = testOptions();
    CHECK(PsbtCore::analyze(f1_psbt, f1_psbt_len, o, &r) == PsbtCore::OK);
    CHECK(r.outputCount == f1_external_count);
    CHECK(strcmp(r.outputs[0].address, f1_external_address) == 0);
    CHECK(r.outputs[0].amountSats == f1_external_sat && r.changeSats == f1_change_sat && r.feeSats == f1_fee_sat);
    CHECK(r.oursInputs == 1 && r.foreignInputs == 0);
    static uint8_t out[PsbtCore::MAX_PSBT_BYTES];
    size_t outLen = 0;
    CHECK(PsbtCore::sign(f1_psbt, f1_psbt_len, o, out, sizeof(out), &outLen, nullptr) == PsbtCore::OK);
    CHECK(verifyPartialSigs(out, outLen, f1_sighash) == 1);
    // RFC 6979: the same key and digest give embit's exact signature bytes
    bool found = false;
    for (size_t i = 0; i + f6_signature_with_sighash_len <= outLen; i++) {
        if (memcmp(out + i, f6_signature_with_sighash, f6_signature_with_sighash_len) == 0) found = true;
    }
    CHECK(found);
    // Same size as embit's signed PSBT, and both carry the identical PARTIAL_SIG record
    // (BIP-174 map order is free: embit sorts by key type, we append before the separator)
    uint8_t rec[2 + 33 + 1 + 80];
    rec[0] = 0x22;
    rec[1] = 0x02;
    memcpy(rec + 2, f0_receive_pubkey, 33);
    rec[35] = (uint8_t)f6_signature_with_sighash_len;
    memcpy(rec + 36, f6_signature_with_sighash, f6_signature_with_sighash_len);
    size_t recLen = 36 + f6_signature_with_sighash_len;
    auto contains = [&](const uint8_t* buf, size_t len) {
        for (size_t i = 0; i + recLen <= len; i++) if (memcmp(buf + i, rec, recLen) == 0) return true;
        return false;
    };
    CHECK(outLen == f6_signed_psbt_len);
    CHECK(contains(out, outLen) && contains(f6_signed_psbt, f6_signed_psbt_len));
    // signing again doesn't add a second signature for the same key
    static uint8_t again[PsbtCore::MAX_PSBT_BYTES];
    size_t againLen = 0;
    CHECK(PsbtCore::sign(out, outLen, o, again, sizeof(again), &againLen, nullptr) == PsbtCore::OK);
    CHECK(againLen == outLen && memcmp(again, out, outLen) == 0);
}

TEST(f2_every_external_output_listed) {
    PsbtCore::Result r;
    CHECK(PsbtCore::analyze(f2_psbt, f2_psbt_len, testOptions(), &r) == PsbtCore::OK);
    CHECK(r.outputCount == f2_external_count);
    bool p2tr = false, p2pkh = false;
    for (size_t i = 0; i < r.outputCount; i++) {
        if (strcmp(r.outputs[i].address, f2_external_p2tr) == 0 && r.outputs[i].amountSats == f2_external_p2tr_sat) p2tr = true;
        if (strcmp(r.outputs[i].address, f2_external_p2pkh) == 0 && r.outputs[i].amountSats == f2_external_p2pkh_sat) p2pkh = true;
    }
    CHECK(p2tr && p2pkh);
    CHECK(r.changeSats == f2_change_sat && r.oursInputs == 2);
}

TEST(f3_foreign_input_stays_unsigned) {
    PsbtCore::Result r;
    PsbtCore::Options o = testOptions();
    CHECK(PsbtCore::analyze(f3_psbt, f3_psbt_len, o, &r) == PsbtCore::OK);
    CHECK(r.oursInputs == f3_ours && r.foreignInputs == f3_foreign);
    static uint8_t out[PsbtCore::MAX_PSBT_BYTES];
    size_t outLen = 0;
    CHECK(PsbtCore::sign(f3_psbt, f3_psbt_len, o, out, sizeof(out), &outLen, nullptr) == PsbtCore::OK);
    CHECK(verifyPartialSigs(out, outLen, f3_ours_sighash) == (int)f3_expected_signed_inputs);
    // exactly one record added: keylen(1) + key(34) + valuelen(1) + DER(70..72) + sighash(1)
    CHECK(outLen >= f3_psbt_len + 1 + 34 + 1 + 71 && outLen <= f3_psbt_len + 1 + 34 + 1 + 73);
}

TEST(f4_fake_change_is_shown_as_external) {
    PsbtCore::Result r;
    CHECK(PsbtCore::analyze(f4_psbt, f4_psbt_len, testOptions(), &r) == PsbtCore::OK);
    CHECK(r.changeSats == f4_change_sat);
    CHECK(r.outputCount == 1 && strcmp(r.outputs[0].address, f4_actual_address) == 0 && r.outputs[0].amountSats == f4_output_sat);
}

TEST(f5_receive_branch_is_not_change) {
    PsbtCore::Result r;
    CHECK(PsbtCore::analyze(f5_psbt, f5_psbt_len, testOptions(), &r) == PsbtCore::OK);
    CHECK(r.changeSats == f5_change_sat);
    CHECK(r.outputCount == 1 && strcmp(r.outputs[0].address, f5_expected_external_address) == 0);
}

TEST(bip174_upstream_vectors) {
    // Invalid PSBTs must be refused. Valid ones belong to other wallets / script types outside
    // our scope: never OK, and (apart from the 0-input edge cases) not refused as malformed.
    int invalidRejected = 0, validParsed = 0, validCount = 0;
    for (size_t i = 0; i < bip174_vector_count; i++) {
        const BIP174Fixture& v = bip174_vectors[i];
        PsbtCore::Result r;
        PsbtCore::Error e = PsbtCore::analyze(v.bytes, v.len, testOptions(), &r);
        CHECK(e != PsbtCore::OK);
        if (!v.valid) {
            invalidRejected += e != PsbtCore::OK;
        } else {
            validCount++;
            if (e != PsbtCore::ERR_FORMAT) validParsed++;
            else printf("    valid vector refused as malformed: %s\n", v.note);
        }
    }
    printf("    %d invalid rejected; %d of %d valid parsed then refused as not ours / out of scope\n",
           invalidRejected, validParsed, validCount);
}

TEST(fuzz_never_signs_garbage) {
    std::mt19937 rng(42);
    static uint8_t m[4096], out[PsbtCore::MAX_PSBT_BYTES];
    PsbtCore::Options o = testOptions();
    int okCount = 0;
    for (int i = 0; i < 20000; i++) {
        memcpy(m, f2_psbt, f2_psbt_len);
        int edits = 1 + rng() % 3;
        for (int e = 0; e < edits; e++) m[rng() % f2_psbt_len] = (uint8_t)rng();
        PsbtCore::Result r;
        size_t outLen = 0;
        PsbtCore::Error e = PsbtCore::sign(m, f2_psbt_len, o, out, sizeof(out), &outLen, &r);
        if (e == PsbtCore::OK) {
            okCount++;
            // a mutated PSBT that still passes must still be fully consistent
            CHECK(r.oursInputs >= 1 && r.feeSats <= PsbtCore::MAX_FEE_SATS && outLen > f2_psbt_len);
        } else {
            CHECK(outLen == 0);
        }
    }
    printf("    %d of 20000 mutations were still valid wallet PSBTs (e.g. an amount byte changed)\n", okCount);
}

int main() {
    RUN(bip143_native_p2wpkh_vector);
    RUN(sighash_rejects_bad_input);
    RUN(f0_keys_match_embit);
    RUN(f1_single_payment_with_change);
    RUN(f2_every_external_output_listed);
    RUN(f3_foreign_input_stays_unsigned);
    RUN(f4_fake_change_is_shown_as_external);
    RUN(f5_receive_branch_is_not_change);
    RUN(bip174_upstream_vectors);
    RUN(fuzz_never_signs_garbage);
    DONE();
}
