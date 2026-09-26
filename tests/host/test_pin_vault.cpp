// PinVault unit tests: hashing, lockout persistence, duress, and the master/duress clash (M2).
#include "test.h"
#include "pin_vault.h"
#include <Preferences.h>
#include <mbedtls/pkcs5.h>
#include <string.h>

// Link-time wrap of the PIN hash: records what NVS holds while the hash runs, i.e. what a
// power cut at that moment would leave behind.
extern "C" int __real_mbedtls_pkcs5_pbkdf2_hmac_ext(mbedtls_md_type_t, const unsigned char*, size_t,
                                                    const unsigned char*, size_t, unsigned int,
                                                    uint32_t, unsigned char*);
static int s_failsDuringHash = -1;
extern "C" int __wrap_mbedtls_pkcs5_pbkdf2_hmac_ext(mbedtls_md_type_t md, const unsigned char* pw, size_t pwLen,
                                                    const unsigned char* salt, size_t saltLen, unsigned int iter,
                                                    uint32_t keyLen, unsigned char* out) {
    Preferences p;
    p.begin("vault_sec", true);
    s_failsDuringHash = p.isKey("pin_fails") ? p.getUChar("pin_fails", 0) : -1;
    p.end();
    return __real_mbedtls_pkcs5_pbkdf2_hmac_ext(md, pw, pwLen, salt, saltLen, iter, keyLen, out);
}

static void fresh() {
    hoststub::nvs.clear();
    hoststub::nvs_fail_writes = false;
    PinVault::begin();
}

TEST(factory_pin_then_set_and_check) {
    fresh();
    CHECK(PinVault::check("1234") == PinVault::OK);   // factory default until setup sets one
    CHECK(PinVault::setPin("482913"));
    CHECK(PinVault::length() == 6);
    CHECK(PinVault::check("482913") == PinVault::OK);
    CHECK(PinVault::check("1234") == PinVault::WRONG);
    // Only hashes are stored, never the digits
    for (auto& kv : hoststub::nvs["vault_sec"]) {
        std::string v(kv.second.begin(), kv.second.end());
        CHECK(v.find("482913") == std::string::npos);
    }
}

TEST(set_pin_rejects_bad_input) {
    fresh();
    CHECK(!PinVault::setPin("123"));
    CHECK(!PinVault::setPin("123456789"));
    CHECK(!PinVault::setPin("12a4"));
    CHECK(!PinVault::setPin(nullptr));
    CHECK(PinVault::check("1234") == PinVault::OK);   // unchanged
}

TEST(wrong_pins_lock_out_and_survive_reboot) {
    fresh();
    CHECK(PinVault::setPin("5555"));
    for (int i = 0; i < PIN_MAX_ATTEMPTS - 1; i++) CHECK(PinVault::check("0000") == PinVault::WRONG);
    CHECK(PinVault::attemptsLeft() == 1);
    PinVault::begin();                                  // power cycle: the count is persistent
    CHECK(PinVault::attemptsLeft() == 1);
    CHECK(PinVault::check("0000") == PinVault::LOCKED_OUT);
    CHECK(PinVault::check("5555") == PinVault::LOCKED_OUT);   // even the right PIN
}

TEST(correct_pin_resets_the_counter) {
    fresh();
    CHECK(PinVault::setPin("5555"));
    CHECK(PinVault::check("0000") == PinVault::WRONG);
    CHECK(PinVault::check("5555") == PinVault::OK);
    CHECK(PinVault::attemptsLeft() == PIN_MAX_ATTEMPTS);
}

TEST(duress_pin) {
    fresh();
    CHECK(PinVault::setPin("1111"));
    CHECK(!PinVault::setDuressPin("1111"));             // may not equal the master PIN
    CHECK(!PinVault::hasDuress());
    CHECK(PinVault::setDuressPin("9090"));
    CHECK(PinVault::hasDuress());
    CHECK(PinVault::check("9090") == PinVault::DURESS);
    PinVault::begin();
    CHECK(PinVault::check("9090") == PinVault::DURESS);   // persisted
    CHECK(PinVault::setDuressPin(""));                    // removed
    CHECK(!PinVault::hasDuress());
    CHECK(PinVault::check("9090") == PinVault::WRONG);
}

// M2: a master PIN equal to the duress PIN would disable the duress wipe
TEST(master_pin_cannot_equal_duress_pin) {
    fresh();
    CHECK(PinVault::setPin("1111"));
    CHECK(PinVault::setDuressPin("2222"));
    CHECK(!PinVault::setPin("2222"));
    CHECK(PinVault::check("1111") == PinVault::OK);       // old master still works
    CHECK(PinVault::check("2222") == PinVault::DURESS);   // duress still armed
    PinVault::begin();
    CHECK(PinVault::check("1111") == PinVault::OK);       // nothing half-written to NVS
    CHECK(PinVault::setDuressPin(""));
    CHECK(PinVault::setPin("2222"));                      // allowed once duress is cleared
    CHECK(PinVault::check("2222") == PinVault::OK);
}

// S7: the attempt is already counted in NVS while the hash runs, then settled by the result
TEST(attempt_counted_before_hashing) {
    fresh();
    CHECK(PinVault::setPin("7777"));
    CHECK(PinVault::setDuressPin("8888"));
    CHECK(PinVault::check("0000") == PinVault::WRONG);
    CHECK(s_failsDuringHash == 1);                 // a power cut here keeps the failure
    CHECK(PinVault::check("0001") == PinVault::WRONG);
    CHECK(s_failsDuringHash == 2);
    CHECK(PinVault::check("8888") == PinVault::DURESS);
    CHECK(s_failsDuringHash == 3);                 // counted while unknown...
    PinVault::begin();
    CHECK(PinVault::attemptsLeft() == PIN_MAX_ATTEMPTS - 2);   // ...then restored: duress isn't a guess
    CHECK(PinVault::check("7777") == PinVault::OK);
    PinVault::begin();
    CHECK(PinVault::attemptsLeft() == PIN_MAX_ATTEMPTS);
}

// A duress PIN must be exactly as long as the master PIN (the entry screen has one slot per digit)
TEST(duress_length_must_match_pin) {
    fresh();
    CHECK(PinVault::setPin("123456"));
    CHECK(!PinVault::setDuressPin("9090"));         // 4 digits for a 6-digit PIN: untypeable
    CHECK(!PinVault::hasDuress());
    CHECK(!PinVault::setDuressPin("90909090"));
    CHECK(PinVault::setDuressPin("909090"));
    CHECK(PinVault::check("909090") == PinVault::DURESS);
    // changing the PIN length alone would orphan the duress PIN
    CHECK(!PinVault::setPin("1234"));
    CHECK(strstr(PinVault::lastError(), "digits") != nullptr);
    CHECK(PinVault::check("123456") == PinVault::OK);
    PinVault::begin();                               // the recorded length survives a reboot
    CHECK(!PinVault::setPin("1234"));
}

TEST(set_pins_atomically) {
    fresh();
    CHECK(PinVault::setPins("123456", "654321"));
    CHECK(PinVault::check("123456") == PinVault::OK && PinVault::check("654321") == PinVault::DURESS);
    // both change length together
    CHECK(PinVault::setPins("24681357", "13572468"));
    CHECK(PinVault::length() == 8);
    CHECK(PinVault::check("24681357") == PinVault::OK && PinVault::check("13572468") == PinVault::DURESS);
    // invalid combinations change nothing
    CHECK(!PinVault::setPins("1111", "2222222"));    // length mismatch
    CHECK(!PinVault::setPins("5555", "5555"));        // identical
    CHECK(!PinVault::setPins("12a4", "1234"));        // not digits
    CHECK(!PinVault::setPins("4444", nullptr));       // new length, old 8-digit duress kept
    CHECK(PinVault::check("24681357") == PinVault::OK && PinVault::check("13572468") == PinVault::DURESS);
    // new length and remove the duress PIN
    CHECK(PinVault::setPins("4444", ""));
    CHECK(!PinVault::hasDuress() && PinVault::check("4444") == PinVault::OK);
    // keep the PIN, add a duress PIN of its length
    CHECK(PinVault::setPins(nullptr, "4321"));
    CHECK(PinVault::check("4321") == PinVault::DURESS);
}

int main() {
    RUN(factory_pin_then_set_and_check);
    RUN(set_pin_rejects_bad_input);
    RUN(wrong_pins_lock_out_and_survive_reboot);
    RUN(correct_pin_resets_the_counter);
    RUN(duress_pin);
    RUN(master_pin_cannot_equal_duress_pin);
    RUN(attempt_counted_before_hashing);
    RUN(duress_length_must_match_pin);
    RUN(set_pins_atomically);
    DONE();
}
