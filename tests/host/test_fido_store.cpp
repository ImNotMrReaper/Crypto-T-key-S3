// Resident credential store unit tests.
#include "test.h"
#include "fido_store.h"
#include <Preferences.h>
#include <string.h>

static ResidentCred makeCred(uint8_t rp, uint8_t user, uint8_t id, uint32_t created) {
    ResidentCred cred = {};
    memset(cred.rpIdHash, rp, sizeof(cred.rpIdHash));
    strcpy(cred.rpId, "example.test");
    cred.userIdLen = 1;
    cred.userId[0] = user;
    cred.credId[0] = id;
    strcpy(cred.userName, "user");
    strcpy(cred.displayName, "Example User");
    cred.created = created;
    return cred;
}

TEST(save_find_count_and_reload) {
    hoststub::nvs.clear();
    hoststub::nvs_fail_writes = false;
    FidoStore store;
    store.begin();
    ResidentCred a = makeCred(1, 10, 21, 5);
    ResidentCred b = makeCred(2, 20, 22, 8);
    CHECK(store.save(a));
    CHECK(store.save(b));
    CHECK(store.count() == 2);
    CHECK(store.findByCredId(a.credId) == 0);
    CHECK(store.findByCredId(b.credId) == 1);
    uint8_t found[4] = {};
    CHECK(store.findByRp(a.rpIdHash, found, sizeof(found)) == 1);
    CHECK(found[0] == 0);

    FidoStore reloaded;
    reloaded.begin();
    CHECK(reloaded.count() == 2);
    CHECK(reloaded.findByCredId(a.credId) == 0);
    CHECK(reloaded.findByCredId(b.credId) == 1);
    CHECK(memcmp(&reloaded.slot(0), &store.slot(0), sizeof(ResidentCred)) == 0);
}

TEST(overwrite_same_rp_and_user) {
    hoststub::nvs.clear();
    FidoStore store;
    store.begin();
    ResidentCred original = makeCred(3, 30, 31, 1);
    ResidentCred replacement = makeCred(3, 30, 32, 9);
    CHECK(store.save(original));
    CHECK(store.save(replacement));
    CHECK(store.count() == 1);
    CHECK(store.findByCredId(original.credId) == -1);
    CHECK(store.findByCredId(replacement.credId) == 0);
    CHECK(store.slot(0).created == 9);
}

TEST(find_by_rp_is_newest_first_and_respects_capacity) {
    hoststub::nvs.clear();
    FidoStore store;
    store.begin();
    CHECK(store.save(makeCred(4, 1, 41, 10)));
    CHECK(store.save(makeCred(4, 2, 42, 30)));
    CHECK(store.save(makeCred(4, 3, 43, 20)));
    CHECK(store.save(makeCred(5, 4, 44, 40)));
    uint8_t found[4] = {};
    CHECK(store.findByRp(makeCred(4, 0, 0, 0).rpIdHash, found, 4) == 3);
    CHECK(found[0] == 1);
    CHECK(found[1] == 2);
    CHECK(found[2] == 0);
    CHECK(store.findByRp(makeCred(4, 0, 0, 0).rpIdHash, found, 2) == 2);
    CHECK(found[0] == 1 && found[1] == 2);
}

TEST(erase_all_clears_ram_and_nvs) {
    hoststub::nvs.clear();
    FidoStore store;
    store.begin();
    CHECK(store.save(makeCred(6, 1, 61, 1)));
    CHECK(store.save(makeCred(7, 2, 62, 2)));
    store.eraseAll();
    CHECK(store.count() == 0);
    CHECK(store.findByCredId(makeCred(6, 1, 61, 1).credId) == -1);
    FidoStore reloaded;
    reloaded.begin();
    CHECK(reloaded.count() == 0);
}

TEST(failed_new_save_preserves_ram_and_count) {
    hoststub::nvs.clear();
    hoststub::nvs_fail_writes = false;
    FidoStore store;
    store.begin();
    CHECK(store.save(makeCred(8, 1, 81, 1)));
    ResidentCred before = store.slot(0);
    uint8_t count = store.count();
    hoststub::nvs_fail_writes = true;
    CHECK(!store.save(makeCred(9, 2, 82, 2)));
    CHECK(store.count() == count);
    CHECK(memcmp(&store.slot(0), &before, sizeof(before)) == 0);
    CHECK(store.findByCredId(makeCred(9, 2, 82, 2).credId) == -1);
    hoststub::nvs_fail_writes = false;
}

TEST(failed_overwrite_preserves_ram_and_count) {
    hoststub::nvs.clear();
    hoststub::nvs_fail_writes = false;
    FidoStore store;
    store.begin();
    ResidentCred original = makeCred(10, 3, 101, 3);
    CHECK(store.save(original));
    ResidentCred before = store.slot(0);
    uint8_t count = store.count();
    ResidentCred replacement = makeCred(10, 3, 102, 99);
    hoststub::nvs_fail_writes = true;
    CHECK(!store.save(replacement));
    CHECK(store.count() == count);
    CHECK(memcmp(&store.slot(0), &before, sizeof(before)) == 0);
    CHECK(store.findByCredId(original.credId) == 0);
    CHECK(store.findByCredId(replacement.credId) == -1);
    hoststub::nvs_fail_writes = false;
}

int main() {
    RUN(save_find_count_and_reload);
    RUN(overwrite_same_rp_and_user);
    RUN(find_by_rp_is_newest_first_and_respects_capacity);
    RUN(erase_all_clears_ram_and_nvs);
    RUN(failed_new_save_preserves_ram_and_count);
    RUN(failed_overwrite_preserves_ram_and_count);
    DONE();
}
