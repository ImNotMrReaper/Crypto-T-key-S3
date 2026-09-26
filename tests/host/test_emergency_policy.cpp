// Emergency policy storage and validation tests.
#include "test.h"
#include "emergency_policy.h"
#include <Preferences.h>
#include <string.h>
#include <vector>

static bool samePolicy(const EmergencyPolicy& a, const EmergencyPolicy& b) {
    for (uint8_t i = 0; i < TRIG_COUNT; i++) if (a.action[i] != b.action[i]) return false;
    return a.panicCountdownS == b.panicCountdownS;
}

static uint8_t testCrc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static void resetNvs() {
    hoststub::nvs.clear();
    hoststub::nvs_fail_writes = false;
}

TEST(defaults_match_contract) {
    resetNvs();
    EmergencyPolicy expected = {};
    expected.action[TRIG_DURESS_PIN] = ACT_WIPE_CHIP;
    expected.action[TRIG_PANIC_HOLD] = ACT_WIPE_CHIP;
    expected.action[TRIG_PIN_LOCKOUT] = ACT_WIPE_CHIP;
    expected.panicCountdownS = 3;
    CHECK(samePolicy(EmergencyPolicyStore::defaults(), expected));
    CHECK(samePolicy(EmergencyPolicyStore::load(), expected));
}

TEST(full_trigger_action_allow_matrix) {
    const bool expected[TRIG_COUNT][ACT_COUNT] = {
        {true, true, true, true, true},
        {true, false, false, true, true},
        {true, false, false, true, true}
    };
    for (uint8_t t = 0; t < TRIG_COUNT; t++) {
        for (uint8_t a = 0; a < ACT_COUNT; a++) {
            CHECK(EmergencyPolicyStore::isAllowed((EmergencyTrigger)t, (EmergencyAction)a) == expected[t][a]);
        }
    }
    CHECK(!EmergencyPolicyStore::isAllowed((EmergencyTrigger)TRIG_COUNT, ACT_NOTHING));
    CHECK(!EmergencyPolicyStore::isAllowed(TRIG_DURESS_PIN, (EmergencyAction)ACT_COUNT));
}

TEST(validation_rejects_bad_enums_and_countdown) {
    EmergencyPolicy policy = EmergencyPolicyStore::defaults();
    CHECK(EmergencyPolicyStore::isValid(policy));
    policy.action[TRIG_DURESS_PIN] = (EmergencyAction)ACT_COUNT;
    CHECK(!EmergencyPolicyStore::isValid(policy));
    policy = EmergencyPolicyStore::defaults();
    policy.action[TRIG_PANIC_HOLD] = ACT_DECOY;
    CHECK(!EmergencyPolicyStore::isValid(policy));
    policy = EmergencyPolicyStore::defaults();
    policy.panicCountdownS = PANIC_COUNTDOWN_MAX_S + 1;
    CHECK(!EmergencyPolicyStore::isValid(policy));
    policy.panicCountdownS = 0;
    CHECK(EmergencyPolicyStore::isValid(policy));
}

TEST(save_load_round_trips_every_valid_policy_combination) {
    resetNvs();
    EmergencyPolicy policy = EmergencyPolicyStore::defaults();
    for (uint8_t duress = 0; duress < ACT_COUNT; duress++) {
        for (uint8_t panic = 0; panic < ACT_COUNT; panic++) {
            for (uint8_t lockout = 0; lockout < ACT_COUNT; lockout++) {
                policy.action[TRIG_DURESS_PIN] = (EmergencyAction)duress;
                policy.action[TRIG_PANIC_HOLD] = (EmergencyAction)panic;
                policy.action[TRIG_PIN_LOCKOUT] = (EmergencyAction)lockout;
                for (uint8_t countdown = 0; countdown <= PANIC_COUNTDOWN_MAX_S; countdown++) {
                    policy.panicCountdownS = countdown;
                    bool valid = EmergencyPolicyStore::isValid(policy);
                    CHECK(EmergencyPolicyStore::save(policy) == valid);
                    if (valid) CHECK(samePolicy(EmergencyPolicyStore::load(), policy));
                }
            }
        }
    }
}

TEST(missing_short_long_wrong_version_and_invalid_fields_fall_back) {
    resetNvs();
    EmergencyPolicy fallback = EmergencyPolicyStore::defaults();
    CHECK(samePolicy(EmergencyPolicyStore::load(), fallback));

    hoststub::nvs["emerg_pol"]["pol"] = {1, 3, 3};
    CHECK(samePolicy(EmergencyPolicyStore::load(), fallback));
    hoststub::nvs["emerg_pol"]["pol"] = {1, 3, 3, 3, 3, 0, 0};
    CHECK(samePolicy(EmergencyPolicyStore::load(), fallback));

    CHECK(EmergencyPolicyStore::save(fallback));
    auto& blob = hoststub::nvs["emerg_pol"]["pol"];
    blob[0] = 2;
    blob[blob.size() - 1] = testCrc8(blob.data(), blob.size() - 1);
    CHECK(samePolicy(EmergencyPolicyStore::load(), fallback));
    blob[0] = 1;
    blob[1 + TRIG_PANIC_HOLD] = ACT_DECOY_WIPE_CHIP;
    blob[blob.size() - 1] = testCrc8(blob.data(), blob.size() - 1);
    CHECK(samePolicy(EmergencyPolicyStore::load(), fallback));
    blob[1 + TRIG_PANIC_HOLD] = ACT_WIPE_CHIP;
    blob[1 + TRIG_COUNT] = PANIC_COUNTDOWN_MAX_S + 1;
    blob[blob.size() - 1] = testCrc8(blob.data(), blob.size() - 1);
    CHECK(samePolicy(EmergencyPolicyStore::load(), fallback));
}

TEST(each_single_byte_mutation_never_loads_an_invalid_policy) {
    resetNvs();
    EmergencyPolicy policy = EmergencyPolicyStore::defaults();
    policy.action[TRIG_DURESS_PIN] = ACT_DECOY;
    policy.action[TRIG_PANIC_HOLD] = ACT_NOTHING;
    policy.action[TRIG_PIN_LOCKOUT] = ACT_WIPE_CHIP_SHRED_SD;
    policy.panicCountdownS = 7;
    CHECK(EmergencyPolicyStore::save(policy));
    const std::vector<uint8_t> original = hoststub::nvs["emerg_pol"]["pol"];
    EmergencyPolicy fallback = EmergencyPolicyStore::defaults();

    for (size_t byte = 0; byte < original.size(); byte++) {
        for (uint16_t value = 0; value <= 0xFF; value++) {
            if (value == original[byte]) continue;
            std::vector<uint8_t> changed = original;
            changed[byte] = (uint8_t)value;
            hoststub::nvs["emerg_pol"]["pol"] = changed;
            EmergencyPolicy loaded = EmergencyPolicyStore::load();
            bool crcMatches = testCrc8(changed.data(), changed.size() - 1) == changed.back();
            CHECK(EmergencyPolicyStore::isValid(loaded));
            if (!crcMatches || changed[0] != 1) CHECK(samePolicy(loaded, fallback));
        }
    }
}

TEST(failed_write_returns_false_and_keeps_persisted_policy) {
    resetNvs();
    EmergencyPolicy original = EmergencyPolicyStore::defaults();
    CHECK(EmergencyPolicyStore::save(original));
    EmergencyPolicy updated = original;
    updated.action[TRIG_DURESS_PIN] = ACT_DECOY_WIPE_CHIP;
    hoststub::nvs_fail_writes = true;
    CHECK(!EmergencyPolicyStore::save(updated));
    CHECK(samePolicy(EmergencyPolicyStore::load(), original));
    CHECK(!EmergencyPolicyStore::save(EmergencyPolicy{}));
    hoststub::nvs_fail_writes = false;
}

TEST(names_parse_round_trip_and_reject_unknown_strings) {
    const char* triggerNames[TRIG_COUNT] = {"duress", "panic", "lockout"};
    const char* actionNames[ACT_COUNT] = {"nothing", "decoy", "decoy_wipe", "wipe", "wipe_shred"};
    for (uint8_t i = 0; i < TRIG_COUNT; i++) {
        EmergencyTrigger parsed = (EmergencyTrigger)TRIG_COUNT;
        CHECK(strcmp(EmergencyPolicyStore::triggerName((EmergencyTrigger)i), triggerNames[i]) == 0);
        CHECK(EmergencyPolicyStore::parseTrigger(triggerNames[i], &parsed));
        CHECK(parsed == (EmergencyTrigger)i);
    }
    for (uint8_t i = 0; i < ACT_COUNT; i++) {
        EmergencyAction parsed = (EmergencyAction)ACT_COUNT;
        CHECK(strcmp(EmergencyPolicyStore::actionName((EmergencyAction)i), actionNames[i]) == 0);
        CHECK(EmergencyPolicyStore::parseAction(actionNames[i], &parsed));
        CHECK(parsed == (EmergencyAction)i);
    }
    CHECK(EmergencyPolicyStore::triggerName((EmergencyTrigger)TRIG_COUNT) == nullptr);
    CHECK(EmergencyPolicyStore::actionName((EmergencyAction)ACT_COUNT) == nullptr);
    EmergencyTrigger trigger = TRIG_PANIC_HOLD;
    EmergencyAction action = ACT_WIPE_CHIP;
    CHECK(!EmergencyPolicyStore::parseTrigger("Duress", &trigger));
    CHECK(trigger == TRIG_PANIC_HOLD);
    CHECK(!EmergencyPolicyStore::parseTrigger("unknown", &trigger));
    CHECK(!EmergencyPolicyStore::parseAction("wipe-shred", &action));
    CHECK(action == ACT_WIPE_CHIP);
    CHECK(!EmergencyPolicyStore::parseAction("unknown", &action));
    CHECK(!EmergencyPolicyStore::parseAction(nullptr, &action));
    CHECK(!EmergencyPolicyStore::parseAction("wipe", nullptr));
}

int main() {
    RUN(defaults_match_contract);
    RUN(full_trigger_action_allow_matrix);
    RUN(validation_rejects_bad_enums_and_countdown);
    RUN(save_load_round_trips_every_valid_policy_combination);
    RUN(missing_short_long_wrong_version_and_invalid_fields_fall_back);
    RUN(each_single_byte_mutation_never_loads_an_invalid_policy);
    RUN(failed_write_returns_false_and_keeps_persisted_policy);
    RUN(names_parse_round_trip_and_reject_unknown_strings);
    DONE();
}
