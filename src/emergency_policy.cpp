#include "emergency_policy.h"
#include <Preferences.h>
#include <string.h>

namespace {

static const char* const NVS_NAMESPACE = "emerg_pol";
static const char* const NVS_KEY = "pol";
static const uint8_t POLICY_VERSION = 1;
static const size_t POLICY_BLOB_SIZE = 1 + TRIG_COUNT + 1 + 1;

static uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static bool validTrigger(EmergencyTrigger trigger) {
    return (uint8_t)trigger < TRIG_COUNT;
}

static bool validAction(EmergencyAction action) {
    return (uint8_t)action < ACT_COUNT;
}

} // namespace

namespace EmergencyPolicyStore {

EmergencyPolicy defaults() {
    EmergencyPolicy policy = {};
    policy.action[TRIG_DURESS_PIN] = ACT_WIPE_CHIP;
    policy.action[TRIG_PANIC_HOLD] = ACT_WIPE_CHIP;
    policy.action[TRIG_PIN_LOCKOUT] = ACT_WIPE_CHIP;
    policy.panicCountdownS = 3;
    return policy;
}

bool isAllowed(EmergencyTrigger trigger, EmergencyAction action) {
    if (!validTrigger(trigger) || !validAction(action)) return false;
    switch (trigger) {
        case TRIG_DURESS_PIN:
            return true;
        case TRIG_PANIC_HOLD:
        case TRIG_PIN_LOCKOUT:
            return action == ACT_NOTHING || action == ACT_WIPE_CHIP || action == ACT_WIPE_CHIP_SHRED_SD;
        default:
            return false;
    }
}

bool isValid(const EmergencyPolicy& policy) {
    if (policy.panicCountdownS > PANIC_COUNTDOWN_MAX_S) return false;
    for (uint8_t i = 0; i < TRIG_COUNT; i++) {
        EmergencyTrigger trigger = (EmergencyTrigger)i;
        if (!isAllowed(trigger, policy.action[i])) return false;
    }
    return true;
}

EmergencyPolicy load() {
    EmergencyPolicy fallback = defaults();
    uint8_t blob[POLICY_BLOB_SIZE] = {};
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return fallback;
    size_t size = prefs.getBytesLength(NVS_KEY);
    bool readOk = size == sizeof(blob) && prefs.getBytes(NVS_KEY, blob, sizeof(blob)) == sizeof(blob);
    prefs.end();
    if (!readOk || blob[0] != POLICY_VERSION || crc8(blob, sizeof(blob) - 1) != blob[sizeof(blob) - 1]) {
        return fallback;
    }

    EmergencyPolicy policy = {};
    for (uint8_t i = 0; i < TRIG_COUNT; i++) policy.action[i] = (EmergencyAction)blob[1 + i];
    policy.panicCountdownS = blob[1 + TRIG_COUNT];
    return isValid(policy) ? policy : fallback;
}

bool save(const EmergencyPolicy& policy) {
    if (!isValid(policy)) return false;

    uint8_t blob[POLICY_BLOB_SIZE] = {};
    blob[0] = POLICY_VERSION;
    for (uint8_t i = 0; i < TRIG_COUNT; i++) blob[1 + i] = (uint8_t)policy.action[i];
    blob[1 + TRIG_COUNT] = policy.panicCountdownS;
    blob[sizeof(blob) - 1] = crc8(blob, sizeof(blob) - 1);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    bool ok = prefs.putBytes(NVS_KEY, blob, sizeof(blob)) == sizeof(blob);
    prefs.end();
    return ok;
}

const char* triggerName(EmergencyTrigger trigger) {
    static const char* const names[TRIG_COUNT] = {"duress", "panic", "lockout"};
    return validTrigger(trigger) ? names[(uint8_t)trigger] : nullptr;
}

const char* actionName(EmergencyAction action) {
    static const char* const names[ACT_COUNT] = {"nothing", "decoy", "decoy_wipe", "wipe", "wipe_shred"};
    return validAction(action) ? names[(uint8_t)action] : nullptr;
}

bool parseTrigger(const char* name, EmergencyTrigger* out) {
    if (!name || !out) return false;
    for (uint8_t i = 0; i < TRIG_COUNT; i++) {
        if (strcmp(name, triggerName((EmergencyTrigger)i)) == 0) {
            *out = (EmergencyTrigger)i;
            return true;
        }
    }
    return false;
}

bool parseAction(const char* name, EmergencyAction* out) {
    if (!name || !out) return false;
    for (uint8_t i = 0; i < ACT_COUNT; i++) {
        if (strcmp(name, actionName((EmergencyAction)i)) == 0) {
            *out = (EmergencyAction)i;
            return true;
        }
    }
    return false;
}

} // namespace EmergencyPolicyStore
