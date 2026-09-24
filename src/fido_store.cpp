#include "fido_store.h"
#include <Preferences.h>
#include <string.h>

#define FIDO_RK_MAGIC 0xA5

FidoStore fidoStore;

static void slotKey(uint8_t i, char* out) {
    snprintf(out, 8, "rk%02u", i);
}

void FidoStore::begin() {
    memset(_slots, 0, sizeof(_slots));
    Preferences prefs;
    prefs.begin("fido_rk", true);
    for (uint8_t i = 0; i < FIDO_RK_SLOTS; i++) {
        char key[8];
        slotKey(i, key);
        if (prefs.getBytesLength(key) == sizeof(ResidentCred)) {
            prefs.getBytes(key, &_slots[i], sizeof(ResidentCred));
            if (_slots[i].magic != FIDO_RK_MAGIC) memset(&_slots[i], 0, sizeof(ResidentCred));
        }
    }
    prefs.end();
}

bool FidoStore::persist(uint8_t i) {
    char key[8];
    slotKey(i, key);
    Preferences prefs;
    prefs.begin("fido_rk", false);
    bool ok;
    if (_slots[i].magic == FIDO_RK_MAGIC) {
        ok = prefs.putBytes(key, &_slots[i], sizeof(ResidentCred)) == sizeof(ResidentCred);
    } else {
        ok = !prefs.isKey(key) || prefs.remove(key);
    }
    prefs.end();
    return ok;
}

bool FidoStore::save(const ResidentCred& cred) {
    int target = -1;
    for (uint8_t i = 0; i < FIDO_RK_SLOTS; i++) {
        const ResidentCred& s = _slots[i];
        if (s.magic == FIDO_RK_MAGIC && memcmp(s.rpIdHash, cred.rpIdHash, 32) == 0 &&
            s.userIdLen == cred.userIdLen && memcmp(s.userId, cred.userId, cred.userIdLen) == 0) {
            target = i;  // same account on the same site: overwrite
            break;
        }
    }
    for (uint8_t i = 0; target < 0 && i < FIDO_RK_SLOTS; i++) {
        if (_slots[i].magic != FIDO_RK_MAGIC) target = i;
    }
    if (target < 0) return false;
    _slots[target] = cred;
    _slots[target].magic = FIDO_RK_MAGIC;
    return persist(target);
}

uint8_t FidoStore::findByRp(const uint8_t* rpIdHash, uint8_t* slotsOut, uint8_t maxOut) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < FIDO_RK_SLOTS && n < maxOut; i++) {
        if (_slots[i].magic == FIDO_RK_MAGIC && memcmp(_slots[i].rpIdHash, rpIdHash, 32) == 0) {
            // insertion sort, newest (highest counter) first
            uint8_t j = n++;
            while (j > 0 && _slots[slotsOut[j - 1]].created < _slots[i].created) {
                slotsOut[j] = slotsOut[j - 1];
                j--;
            }
            slotsOut[j] = i;
        }
    }
    return n;
}

int FidoStore::findByCredId(const uint8_t* credId) const {
    for (uint8_t i = 0; i < FIDO_RK_SLOTS; i++) {
        if (_slots[i].magic == FIDO_RK_MAGIC && memcmp(_slots[i].credId, credId, 32) == 0) return i;
    }
    return -1;
}

uint8_t FidoStore::count() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < FIDO_RK_SLOTS; i++) n += (_slots[i].magic == FIDO_RK_MAGIC);
    return n;
}

void FidoStore::eraseAll() {
    memset(_slots, 0, sizeof(_slots));
    Preferences prefs;
    prefs.begin("fido_rk", false);
    prefs.clear();
    prefs.end();
}
