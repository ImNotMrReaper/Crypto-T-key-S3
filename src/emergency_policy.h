/**
 * emergency_policy.h — What each emergency trigger does, chosen by the user in the setup portal.
 *
 * Triggers: the duress PIN, the >6 s panic hold, and the wrong-PIN lockout. Each maps to one
 * action. Not every action makes sense for every trigger (a decoy can only be opened by a PIN;
 * the lockout can't "do nothing" silently, it then just stays locked), so the allowed set is
 * enforced here, and anything invalid or unreadable in NVS falls back to the defaults.
 *
 * The decoy actions need a decoy wallet (Phase B3): the portal only offers them once one exists,
 * and the firmware treats them as ACT_WIPE_CHIP if it's missing (protect the assets, never
 * reveal the real wallet).
 *
 * Stored in NVS "emerg_pol" as one versioned blob. A wipe erases it with everything else, so a
 * wiped key comes back with the defaults.
 */

#pragma once
#include <Arduino.h>

enum EmergencyTrigger : uint8_t {
    TRIG_DURESS_PIN = 0,
    TRIG_PANIC_HOLD,
    TRIG_PIN_LOCKOUT,
    TRIG_COUNT
};

enum EmergencyAction : uint8_t {
    ACT_NOTHING = 0,           // duress PIN: treated as a wrong PIN · panic: disabled · lockout: stay locked
    ACT_DECOY,                 // open the decoy wallet, keep the real one (duress PIN only)
    ACT_DECOY_WIPE_CHIP,       // open the decoy wallet after silently wiping the real one (duress PIN only)
    ACT_WIPE_CHIP,             // erase NVS: real seed, passkeys, PINs, Wi-Fi (SD backup kept)
    ACT_WIPE_CHIP_SHRED_SD,    // erase NVS and overwrite + delete the SD vault files
    ACT_COUNT
};

#define PANIC_COUNTDOWN_MAX_S 10

struct EmergencyPolicy {
    EmergencyAction action[TRIG_COUNT];
    uint8_t panicCountdownS;   // 0..PANIC_COUNTDOWN_MAX_S; a tap during the countdown cancels
};

namespace EmergencyPolicyStore {
    // Tiered defaults: duress PIN -> wipe chip (-> decoy + wipe once a decoy wallet exists),
    // panic hold -> wipe chip after a 3 s countdown, lockout -> wipe chip. SD backup is kept.
    EmergencyPolicy defaults();
    bool isAllowed(EmergencyTrigger t, EmergencyAction a);
    // Validates every field; returns false (and changes nothing) if any is out of range.
    bool isValid(const EmergencyPolicy& p);
    // Reads NVS; a missing, short, wrong-version or invalid blob yields defaults().
    EmergencyPolicy load();
    // Rejects an invalid policy without touching NVS. True only if the write succeeded.
    bool save(const EmergencyPolicy& p);
    const char* triggerName(EmergencyTrigger t);   // stable ids for the portal: "duress", "panic", "lockout"
    const char* actionName(EmergencyAction a);     // "nothing", "decoy", "decoy_wipe", "wipe", "wipe_shred"
    // Parses the names above; false for anything unknown.
    bool parseTrigger(const char* s, EmergencyTrigger* out);
    bool parseAction(const char* s, EmergencyAction* out);
}
