// test_sd_vault.cpp — Host unit tests for SdVaultEngine and DuressWipe hooks
#include "test.h"
#include "sd_vault.h"
#include "duress_wipe.h"
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

// Device-side NVS functions sd_vault's full backup calls (the host has no NVS or eFuse MAC)
#include "nvs_backup.h"
namespace NvsBackup {
void hostRandom(uint8_t* out, size_t len) { for (size_t i = 0; i < len; i++) out[i] = (uint8_t)(i * 31 + 7); }
bool readNvs(const char* const*, uint8_t* buf, size_t cap, size_t* len) {
    uint8_t v = 42;
    return packBegin(buf, cap, len) && pack(buf, cap, len, "vault_sec", "pin_len", REC_U8, &v, 1);
}
bool writeNvs(const uint8_t* bundle, size_t len) { return unpack(bundle, len, nullptr, nullptr); }
bool chipId(uint8_t out[CHIP_ID_LEN]) { for (size_t i = 0; i < CHIP_ID_LEN; i++) out[i] = (uint8_t)i; return true; }
}

// Stub definitions for RgbStatus referenced by DuressWipe::execute
void RgbStatus::setMode(LedMode) {}
void RgbStatus::update() {}

// 1. v2 backupSeedV2 / restoreSeed round trip
TEST(v2_backup_restore_round_trip) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pass = "correct-horse-battery-staple";
    const char* seedPhrase = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";

    CHECK(sdVault.backupSeedV2(seedPhrase, pass));
    CHECK(sdVault.hasSeedBackup());

    char restored[256] = {0};
    CHECK(sdVault.restoreSeed(restored, sizeof(restored), pass));
    CHECK(strcmp(restored, seedPhrase) == 0);
}

// 2. Wrong passphrase fails
TEST(v2_wrong_passphrase_fails) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pass = "correct-horse-battery-staple";
    const char* wrong = "wrong-horse-battery-staple-bad";
    const char* seedPhrase = "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong";

    CHECK(sdVault.backupSeedV2(seedPhrase, pass));

    char restored[256] = {0};
    CHECK(!sdVault.restoreSeed(restored, sizeof(restored), wrong));
    CHECK(strlen(restored) == 0);
}

// 3. EVERY single header byte flipped fails GCM authentication (Finding M2 fix)
TEST(v2_every_single_header_byte_flipped_fails_m2) {
    const char* pass = "passphrase-for-m2-test-123";
    const char* seedPhrase = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu";

    for (size_t byteIdx = 0; byteIdx < sizeof(SdVaultHeaderV2); byteIdx++) {
        hoststub::sd_reset();
        sdVault.end();

        CHECK(sdVault.backupSeedV2(seedPhrase, pass));

        auto it = hoststub::sd_files.find(SD_VAULT_SEED_FILE);
        CHECK(it != hoststub::sd_files.end());
        CHECK(it->second->data.size() >= sizeof(SdVaultHeaderV2));

        // Flip bit in header byte byteIdx
        it->second->data[byteIdx] ^= 0x55;

        char restored[256] = {0};
        CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass));
        CHECK(strlen(restored) == 0);
    }
}

// 4. A payload bit flip fails GCM authentication
TEST(v2_payload_bit_flip_fails) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pass = "strong-passphrase-payload-test";
    const char* seedPhrase = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu";

    CHECK(sdVault.backupSeedV2(seedPhrase, pass));

    auto it = hoststub::sd_files.find(SD_VAULT_SEED_FILE);
    CHECK(it != hoststub::sd_files.end());
    CHECK(it->second->data.size() > sizeof(SdVaultHeaderV2) + SD_VAULT_TAG_LEN);

    // Flip a bit in the ciphertext payload
    it->second->data[sizeof(SdVaultHeaderV2) + 2] ^= 0x01;

    char restored[256] = {0};
    CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass));
    CHECK(strlen(restored) == 0);
}

// 5. Truncated and trailing-data files fail
TEST(v2_truncated_and_trailing_data_files_fail) {
    const char* pass = "strong-passphrase-trunc-test";
    const char* seedPhrase = "seed for truncation and trailing data validation test phrase";

    // 5a. Truncated file (1 byte short)
    {
        hoststub::sd_reset();
        sdVault.end();
        CHECK(sdVault.backupSeedV2(seedPhrase, pass));

        auto it = hoststub::sd_files.find(SD_VAULT_SEED_FILE);
        CHECK(it != hoststub::sd_files.end());
        it->second->data.pop_back();

        char restored[256] = {0};
        CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass));
        CHECK(strlen(restored) == 0);
    }

    // 5b. Trailing data appended (1 byte extra)
    {
        hoststub::sd_reset();
        sdVault.end();
        CHECK(sdVault.backupSeedV2(seedPhrase, pass));

        auto it = hoststub::sd_files.find(SD_VAULT_SEED_FILE);
        CHECK(it != hoststub::sd_files.end());
        it->second->data.push_back(0xAA);

        char restored[256] = {0};
        CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass));
        CHECK(strlen(restored) == 0);
    }
}

// 6. Iteration-bound rejection (< 1000 or > 2000000)
TEST(v2_iteration_bound_rejection) {
    const char* pass = "strong-passphrase-iters-test";
    const char* seedPhrase = "testing iteration bounds minimum and maximum validation";

    // 6a. Iterations < 1000
    {
        hoststub::sd_reset();
        sdVault.end();
        CHECK(sdVault.backupSeedV2(seedPhrase, pass));

        auto it = hoststub::sd_files.find(SD_VAULT_SEED_FILE);
        CHECK(it != hoststub::sd_files.end());
        SdVaultHeaderV2* h = (SdVaultHeaderV2*)it->second->data.data();
        h->iterations = 500;

        char restored[256] = {0};
        CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass));
        CHECK(strlen(restored) == 0);
    }

    // 6b. Iterations > 400000 (R5 cap at 400000)
    {
        hoststub::sd_reset();
        sdVault.end();
        CHECK(sdVault.backupSeedV2(seedPhrase, pass));

        auto it = hoststub::sd_files.find(SD_VAULT_SEED_FILE);
        CHECK(it != hoststub::sd_files.end());
        SdVaultHeaderV2* h = (SdVaultHeaderV2*)it->second->data.data();
        h->iterations = 400001;

        char restored[256] = {0};
        CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass));
        CHECK(strlen(restored) == 0);
    }
}

// 7. Short passphrase & non-printable ASCII rejection
TEST(v2_short_and_invalid_passphrase_rejection) {
    hoststub::sd_reset();
    sdVault.end();

    const char* seedPhrase = "seed phrase testing passphrase validation policy";

    // Less than 12 chars
    CHECK(!sdVault.backupSeedV2(seedPhrase, ""));
    CHECK(!sdVault.backupSeedV2(seedPhrase, "short"));
    CHECK(!sdVault.backupSeedV2(seedPhrase, "12345678901")); // 11 chars

    // Non-printable ASCII
    char nonPrintable[20] = "validlength\x1f\x20pass";
    CHECK(!sdVault.backupSeedV2(seedPhrase, nonPrintable));
}

// 8. Legacy v1 container still restores
TEST(v1_legacy_file_still_restores) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pin = "1234";
    const char* seedPhrase = "legacy v1 backup recovery phrase round trip";

    // Write a genuine v1 container using writeEncryptedFile
    CHECK(sdVault.writeEncryptedFile(SD_VAULT_SEED_FILE, (const uint8_t*)seedPhrase, strlen(seedPhrase), pin));
    CHECK(sdVault.hasSeedBackup());

    // Restore using PIN
    char restored[256] = {0};
    CHECK(sdVault.restoreSeed(restored, sizeof(restored), pin));
    CHECK(strcmp(restored, seedPhrase) == 0);

    // Wrong PIN fails on v1
    CHECK(!sdVault.restoreSeed(restored, sizeof(restored), "9999"));
}

// 9. No plaintext left in the write record
TEST(v2_no_plaintext_left_in_write_record) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pass = "passphrase-for-write-record-check";
    const char* seedPhrase = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";

    CHECK(sdVault.backupSeedV2(seedPhrase, pass));

    const auto& rec = hoststub::final_write_record[SD_VAULT_SEED_FILE];
    CHECK(rec.size() > 0);

    std::string recStr(rec.begin(), rec.end());
    CHECK(recStr.find("abandon") == std::string::npos);
    CHECK(recStr.find("about") == std::string::npos);
}

// 10. wipeVault overwriting every original byte of a multi-KB passkeys file plus seed file
TEST(wipe_vault_overwrites_every_byte_and_removes_files) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pass = "passphrase-wipe-test-1234";

    // 10a. Create seed backup v2
    const char* seedPhrase = "bacon bacon bacon bacon bacon bacon bacon bacon bacon bacon bacon bacon";
    CHECK(sdVault.backupSeedV2(seedPhrase, pass));

    // 10b. Populate a multi-KB passkeys file with 12 resident passkeys (~2.7 KB)
    for (int i = 0; i < 12; i++) {
        uint8_t credId[32];
        memset(credId, 0x10 + i, sizeof(credId));
        char rp[64];
        snprintf(rp, sizeof(rp), "service-%d.example.com", i);
        char user[64];
        snprintf(user, sizeof(user), "user-%d", i);
        uint8_t priv[32];
        memset(priv, 0x50 + i, sizeof(priv));

        CHECK(sdVault.saveResidentPasskey(credId, sizeof(credId), rp, user, priv, "1234"));
    }

    auto seedIt = hoststub::sd_files.find(SD_VAULT_SEED_FILE);
    CHECK(seedIt != hoststub::sd_files.end());
    std::vector<uint8_t> oldSeedBytes = seedIt->second->data;
    size_t seedLen = oldSeedBytes.size();
    CHECK(seedLen > 0);

    auto passIt = hoststub::sd_files.find(SD_VAULT_PASSKEY_FILE);
    CHECK(passIt != hoststub::sd_files.end());
    std::vector<uint8_t> oldPassBytes = passIt->second->data;
    size_t passLen = oldPassBytes.size();
    CHECK(passLen >= 2000);

    // 10c. Execute wipeVault()
    CHECK(sdVault.wipeVault() == WIPE_OK);

    // 10d. Assert files are gone
    CHECK(!SD_MMC.exists(SD_VAULT_SEED_FILE));
    CHECK(!SD_MMC.exists(SD_VAULT_PASSKEY_FILE));
    CHECK(!sdVault.hasSeedBackup());

    // 10e. Assert the old bytes appear nowhere in the final write record for each file span
    const auto& seedRecord = hoststub::final_write_record[SD_VAULT_SEED_FILE];
    CHECK(seedRecord.size() == seedLen);
    for (size_t i = 0; i < seedLen; i++) {
        CHECK(seedRecord[i] == 0);
    }
    CHECK(memcmp(seedRecord.data(), oldSeedBytes.data(), seedLen) != 0);

    const auto& passRecord = hoststub::final_write_record[SD_VAULT_PASSKEY_FILE];
    CHECK(passRecord.size() == passLen);
    for (size_t i = 0; i < passLen; i++) {
        CHECK(passRecord[i] == 0);
    }
    CHECK(memcmp(passRecord.data(), oldPassBytes.data(), passLen) != 0);

    for (size_t i = 0; i + 16 <= seedLen; i++) {
        CHECK(memcmp(&oldSeedBytes[i], &seedRecord[i], 16) != 0);
    }
    for (size_t i = 0; i + 16 <= passLen; i++) {
        CHECK(memcmp(&oldPassBytes[i], &passRecord[i], 16) != 0);
    }
}

// 11. DuressWipe hook configuration API
static bool s_ramScrubberInvoked = false;
static void testRamScrubber() {
    s_ramScrubberInvoked = true;
}

TEST(duress_wipe_hook_api) {
    s_ramScrubberInvoked = false;
    DuressWipe::setRamScrubber(testRamScrubber);
    DuressWipe::setSdWipe(true);
    DuressWipe::setSdWipe(false);
}

// 12. wipeVault reports failure if overwrite comes up short
TEST(wipe_vault_fails_if_overwrite_short) {
    hoststub::sd_reset();
    sdVault.end();

    CHECK(sdVault.backupSeedV2("test backup for write failure", "strong-passphrase-write-fail"));
    CHECK(sdVault.hasSeedBackup());

    hoststub::sd_fail_writes = true;
    WipeResult wipeRes = sdVault.wipeVault();
    CHECK(wipeRes == WIPE_FAILED);

    hoststub::sd_fail_writes = false;
}

// 13. R2: Seed overwrite fails, passkey still wiped and removed, overall result WIPE_FAILED
TEST(wipe_vault_best_effort_seed_fails_passkey_still_wiped) {
    hoststub::sd_reset();
    sdVault.end();

    CHECK(sdVault.backupSeedV2("seed phrase to fail overwrite", "strong-passphrase-best-effort"));

    uint8_t credId[32] = {1, 2, 3};
    uint8_t priv[32] = {4, 5, 6};
    CHECK(sdVault.saveResidentPasskey(credId, sizeof(credId), "test.com", "user", priv, "1234"));

    CHECK(SD_MMC.exists(SD_VAULT_SEED_FILE));
    CHECK(SD_MMC.exists(SD_VAULT_PASSKEY_FILE));

    hoststub::sd_fail_write_path = SD_VAULT_SEED_FILE;

    WipeResult res = sdVault.wipeVault();
    CHECK(res == WIPE_FAILED);

    // Both files must still be removed by best-effort removal!
    CHECK(!SD_MMC.exists(SD_VAULT_SEED_FILE));
    CHECK(!SD_MMC.exists(SD_VAULT_PASSKEY_FILE));

    const auto& passRecord = hoststub::final_write_record[SD_VAULT_PASSKEY_FILE];
    CHECK(passRecord.size() > 0);
    for (size_t i = 0; i < passRecord.size(); i++) {
        CHECK(passRecord[i] == 0);
    }

    hoststub::sd_fail_write_path.clear();
}

// 14. R3: No files gives WIPE_NOTHING
TEST(wipe_vault_no_files_gives_wipe_nothing) {
    hoststub::sd_reset();
    sdVault.end();

    CHECK(!SD_MMC.exists(SD_VAULT_SEED_FILE));
    CHECK(!SD_MMC.exists(SD_VAULT_PASSKEY_FILE));

    WipeResult res = sdVault.wipeVault();
    CHECK(res == WIPE_NOTHING);
}

// 15. Open fails on file: still attempts remove and returns WIPE_FAILED
TEST(wipe_vault_open_fails_still_removes) {
    hoststub::sd_reset();
    sdVault.end();

    CHECK(sdVault.backupSeedV2("seed for open fail test", "strong-passphrase-open-fail"));
    CHECK(SD_MMC.exists(SD_VAULT_SEED_FILE));

    hoststub::sd_fail_open_path = SD_VAULT_SEED_FILE;

    WipeResult res = sdVault.wipeVault();
    CHECK(res == WIPE_FAILED);
    CHECK(!SD_MMC.exists(SD_VAULT_SEED_FILE));

    hoststub::sd_fail_open_path.clear();
}

// 16. R3: Atomic backup — failed/short write leaves previous backup restorable and no tmp file; successful replaces it
TEST(v2_atomic_backup_write_failure_preserves_old_backup) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pass = "atomic-backup-passphrase-123";
    const char* oldPhrase = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    const char* newPhrase = "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong";

    // 16a. Create initial backup
    CHECK(sdVault.backupSeedV2(oldPhrase, pass));
    CHECK(sdVault.hasSeedBackup());
    CHECK(!SD_MMC.exists(SD_VAULT_TMP_FILE));

    char restored[256] = {0};
    CHECK(sdVault.restoreSeed(restored, sizeof(restored), pass));
    CHECK(strcmp(restored, oldPhrase) == 0);

    // 16b. Inject write failure on tmp file
    hoststub::sd_fail_write_path = SD_VAULT_TMP_FILE;
    CHECK(!sdVault.backupSeedV2(newPhrase, pass));

    // Assert tmp file was cleaned up and live backup was NOT corrupted
    CHECK(!SD_MMC.exists(SD_VAULT_TMP_FILE));
    CHECK(SD_MMC.exists(SD_VAULT_SEED_FILE));

    // Assert old backup is still 100% restorable
    memset(restored, 0, sizeof(restored));
    CHECK(sdVault.restoreSeed(restored, sizeof(restored), pass));
    CHECK(strcmp(restored, oldPhrase) == 0);

    hoststub::sd_fail_write_path.clear();

    // 16c. Now perform successful replacement backup
    CHECK(sdVault.backupSeedV2(newPhrase, pass));
    CHECK(!SD_MMC.exists(SD_VAULT_TMP_FILE));
    CHECK(SD_MMC.exists(SD_VAULT_SEED_FILE));

    // Assert new backup replaces it
    memset(restored, 0, sizeof(restored));
    CHECK(sdVault.restoreSeed(restored, sizeof(restored), pass));
    CHECK(strcmp(restored, newPhrase) == 0);
}

// 17. R4: Large payload length near 2^32 or > 512 cap is rejected without 32-bit wrap
TEST(v2_large_payload_near_uint32_max_rejected) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pass = "strong-passphrase-overflow-test";
    const char* seedPhrase = "seed phrase testing 32-bit integer overflow protection";

    CHECK(sdVault.backupSeedV2(seedPhrase, pass));

    auto it = hoststub::sd_files.find(SD_VAULT_SEED_FILE);
    CHECK(it != hoststub::sd_files.end());

    // 17a. Near 2^32 payloadLen
    SdVaultHeaderV2* h = (SdVaultHeaderV2*)it->second->data.data();
    h->payloadLen = 0xFFFFFFF0;

    char restored[256] = {0};
    CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass));
    CHECK(strlen(restored) == 0);

    // 17b. payloadLen > 512 cap
    h->payloadLen = 513;
    CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass));
    CHECK(strlen(restored) == 0);
}

// 18. A v2 file written with a different passphrase cannot be restored
TEST(v2_different_passphrase_cannot_restore) {
    hoststub::sd_reset();
    sdVault.end();

    const char* pass1 = "passphrase-one-unique-alpha";
    const char* pass2 = "passphrase-two-unique-bravo";
    const char* seedPhrase = "word word word word word word word word word word word word";

    CHECK(sdVault.backupSeedV2(seedPhrase, pass1));

    char restored[256] = {0};
    CHECK(!sdVault.restoreSeed(restored, sizeof(restored), pass2));
    CHECK(strlen(restored) == 0);

    // With the correct passphrase, it restores cleanly
    CHECK(sdVault.restoreSeed(restored, sizeof(restored), pass1));
    CHECK(strcmp(restored, seedPhrase) == 0);
}

// Lead addition (R3 follow-up): a failed rename during the swap never loses the old backup
TEST(v2_rename_failure_keeps_old_backup) {
    hoststub::sd_reset();
    const char* oldSeed = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    const char* newSeed = "legal winner thank year wave sausage worth useful legal winner thank yellow";
    const char* pass = "correct horse battery staple";
    CHECK(sdVault.backupSeedV2(oldSeed, pass));
    hoststub::sd_fail_rename_to = SD_VAULT_SEED_FILE;   // the tmp -> live step fails
    CHECK(!sdVault.backupSeedV2(newSeed, pass));
    hoststub::sd_fail_rename_to.clear();
    char out[256] = {0};
    CHECK(sdVault.restoreSeed(out, sizeof(out), pass));
    CHECK(strcmp(out, oldSeed) == 0);                     // old backup back in place
    CHECK(!SD_MMC.exists(SD_VAULT_TMP_FILE));
    CHECK(!SD_MMC.exists(SD_VAULT_OLD_FILE));
    CHECK(sdVault.backupSeedV2(newSeed, pass));            // and a later backup still works
    CHECK(sdVault.restoreSeed(out, sizeof(out), pass));
    CHECK(strcmp(out, newSeed) == 0);
    CHECK(!SD_MMC.exists(SD_VAULT_OLD_FILE));
}

// Full device-bound backup through the SD layer: round trip, wrong password, atomic replace
TEST(full_backup_round_trip) {
    hoststub::sd_reset();
    CHECK(sdVault.backupFull("setup-password-1"));
    CHECK(sdVault.hasFullBackup());
    CHECK(sdVault.restoreFull("setup-password-1"));
    CHECK(!sdVault.restoreFull("setup-password-2"));
    CHECK(!SD_MMC.exists(SD_VAULT_FULL_FILE ".tmp") && !SD_MMC.exists(SD_VAULT_FULL_FILE ".old"));
    CHECK(sdVault.backupFull("setup-password-2"));        // replacing keeps exactly one copy
    CHECK(sdVault.restoreFull("setup-password-2") && !sdVault.restoreFull("setup-password-1"));
    CHECK(!sdVault.backupFull(""));
}

int main() {
    RUN(v2_backup_restore_round_trip);
    RUN(v2_wrong_passphrase_fails);
    RUN(v2_every_single_header_byte_flipped_fails_m2);
    RUN(v2_payload_bit_flip_fails);
    RUN(v2_truncated_and_trailing_data_files_fail);
    RUN(v2_iteration_bound_rejection);
    RUN(v2_short_and_invalid_passphrase_rejection);
    RUN(v1_legacy_file_still_restores);
    RUN(v2_no_plaintext_left_in_write_record);
    RUN(wipe_vault_overwrites_every_byte_and_removes_files);
    RUN(duress_wipe_hook_api);
    RUN(wipe_vault_fails_if_overwrite_short);
    RUN(wipe_vault_best_effort_seed_fails_passkey_still_wiped);
    RUN(wipe_vault_no_files_gives_wipe_nothing);
    RUN(wipe_vault_open_fails_still_removes);
    RUN(v2_atomic_backup_write_failure_preserves_old_backup);
    RUN(v2_large_payload_near_uint32_max_rejected);
    RUN(v2_different_passphrase_cannot_restore);
    RUN(v2_rename_failure_keeps_old_backup);
    RUN(full_backup_round_trip);
    DONE();
}
