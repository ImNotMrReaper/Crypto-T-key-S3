// Wallet family receive addresses cross-checked with independent Python libraries and wallet paths.
#include "test.h"
#include "wallet_families.h"
#include "family_fixtures.h"
#include <string.h>

TEST(all_family_addresses_match_independent_fixtures) {
    CHECK(family_address_fixture_count == 16 * 3);
    CHECK(memcmp(family_seed_bip39_12, family_seed_bip39_12_trezor, 64) != 0);
    char actual[FAMILY_ADDR_LEN];
    for (size_t i = 0; i < family_address_fixture_count; i++) {
        const FamilyAddressFixture& fixture = family_address_fixtures[i];
        memset(actual, 0, sizeof(actual));
        bool ok = WalletFamilies::deriveAddress(fixture.family, fixture.seed, actual);
        bool sameReferences = strcmp(fixture.reference_address, fixture.mainstream_wallet_address) == 0;
        CHECK(sameReferences);
        if (!sameReferences) {
            printf("  reference mismatch %s %s (%s, %s): reference %s, wallet %s\n",
                   fixture.family_name, fixture.mnemonic_case, fixture.reference_wallet,
                   fixture.derivation_path, fixture.reference_address, fixture.mainstream_wallet_address);
        }
        if (!ok || strcmp(actual, fixture.reference_address) != 0 ||
            strcmp(actual, fixture.mainstream_wallet_address) != 0) {
            printf("  firmware mismatch %s %s (%s, %s): expected %s, got %s\n",
                   fixture.family_name, fixture.mnemonic_case, fixture.reference_wallet,
                   fixture.derivation_path, fixture.reference_address, ok ? actual : "<derive failed>");
            CHECK(false);
        } else {
            CHECK(true);
        }
    }
}

int main() {
    RUN(all_family_addresses_match_independent_fixtures);
    DONE();
}
