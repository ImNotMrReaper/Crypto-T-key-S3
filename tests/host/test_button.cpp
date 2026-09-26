// ButtonCadence unit tests: timings, double-tap window, hold stages, and pending queue.
#include "test.h"
#include "button_cadence.h"
#include "Arduino.h"
#include <vector>

static void pressBtn() {
    hoststub::pin_level[0] = 0;
}

static void releaseBtn() {
    hoststub::pin_level[0] = 1;
}

static std::vector<ButtonEvent> stepTime(ButtonCadence& btn, uint32_t ms, uint32_t step_ms = 2) {
    std::vector<ButtonEvent> events;
    uint32_t target = hoststub::now_ms + ms;
    while (hoststub::now_ms < target) {
        uint32_t adv = (target - hoststub::now_ms < step_ms) ? (target - hoststub::now_ms) : step_ms;
        hoststub::now_ms += adv;
        ButtonEvent ev = btn.update();
        if (ev != BTN_NONE) {
            events.push_back(ev);
        }
    }
    return events;
}

// Out-of-range reads give BTN_NONE, so a regression fails a CHECK instead of crashing the suite
static ButtonEvent at(const std::vector<ButtonEvent>& v, size_t i) {
    return i < v.size() ? v[i] : BTN_NONE;
}

static void resetEnv(ButtonCadence& btn) {
    hoststub::now_ms += 1000;
    releaseBtn();
    btn.begin(0);
}

// 1. Single tap (<600 ms, idle >320 ms after release -> BTN_SHORT_PRESS)
TEST(single_tap) {
    ButtonCadence btn;
    resetEnv(btn);

    pressBtn();
    auto ev1 = stepTime(btn, 60);
    CHECK(ev1.empty());

    releaseBtn();
    auto ev2 = stepTime(btn, 100);
    CHECK(ev2.empty()); // Still waiting for potential second tap

    auto ev3 = stepTime(btn, 250);
    CHECK(ev3.size() == 1);
    CHECK(at(ev3, 0) == BTN_SHORT_PRESS);
}

// 2. Fast double click (gap between tap 1 release and tap 2 press <= 320 ms)
TEST(fast_double) {
    ButtonCadence btn;
    resetEnv(btn);

    pressBtn();
    stepTime(btn, 50);
    releaseBtn();
    auto ev1 = stepTime(btn, 60); // 60 ms gap
    CHECK(ev1.empty());

    pressBtn();
    stepTime(btn, 50);
    releaseBtn();
    auto ev2 = stepTime(btn, 350);
    CHECK(ev2.size() == 1);
    CHECK(at(ev2, 0) == BTN_DOUBLE_CLICK);
}

// 3. Slow-hold double: press #2 starts within 320 ms, but release #2 is >320 ms after release #1
TEST(slow_hold_double) {
    ButtonCadence btn;
    resetEnv(btn);

    // Tap 1: held 60 ms
    pressBtn();
    stepTime(btn, 60);
    releaseBtn();
    stepTime(btn, 150); // Gap = 150 ms (<= 320 ms)

    // Tap 2: pressed 150 ms after release #1, held 180 ms (<600 ms quick tap)
    // Release #2 occurs at 150 + 180 = 330 ms after release #1 (>320 ms).
    pressBtn();
    stepTime(btn, 180);
    releaseBtn();

    auto ev = stepTime(btn, 400);
    CHECK(ev.size() == 1);
    CHECK(at(ev, 0) == BTN_DOUBLE_CLICK);
}

// 4. Tap then long press: tap 1 emitted first, then long press on next update() call
TEST(tap_then_long) {
    ButtonCadence btn;
    resetEnv(btn);

    // Tap 1
    pressBtn();
    stepTime(btn, 60);
    releaseBtn();
    stepTime(btn, 150);

    // Press #2 held 800 ms (>= BTN_LONG_PRESS_MS 600 ms, < BTN_VERY_LONG_MS 2200 ms)
    pressBtn();
    stepTime(btn, 800);
    releaseBtn();

    auto ev = stepTime(btn, 400);
    CHECK(ev.size() == 2);
    CHECK(at(ev, 0) == BTN_SHORT_PRESS);
    CHECK(at(ev, 1) == BTN_LONG_PRESS);
}

// 5. Tap then very long press: tap 1 emitted first, then very long press
TEST(tap_then_very_long) {
    ButtonCadence btn;
    resetEnv(btn);

    // Tap 1
    pressBtn();
    stepTime(btn, 60);
    releaseBtn();
    stepTime(btn, 150);

    // Press #2 held 2400 ms (>= BTN_VERY_LONG_MS 2200 ms)
    pressBtn();
    stepTime(btn, 2400);
    releaseBtn();

    auto ev = stepTime(btn, 400);
    CHECK(ev.size() == 2);
    CHECK(at(ev, 0) == BTN_SHORT_PRESS);
    CHECK(at(ev, 1) == BTN_VERY_LONG_PRESS);
}

// 6. Tap then panic hold: tap 1 emitted first, then panic hold
TEST(tap_then_panic) {
    ButtonCadence btn;
    resetEnv(btn);

    // Tap 1
    pressBtn();
    stepTime(btn, 60);
    releaseBtn();
    stepTime(btn, 150);

    // Press #2 held >= 6000 ms
    pressBtn();
    auto evHeld = stepTime(btn, 6200);
    CHECK(evHeld.size() == 2);
    CHECK(at(evHeld, 0) == BTN_SHORT_PRESS);
    CHECK(at(evHeld, 1) == BTN_PANIC_HOLD);

    // Continue holding: no duplicate panic events
    auto evMore = stepTime(btn, 500);
    CHECK(evMore.empty());

    // Release after panic: emits nothing
    releaseBtn();
    auto evRel = stepTime(btn, 200);
    CHECK(evRel.empty());
}

// 7. Long press (held 800 ms without prior tap)
TEST(long_press) {
    ButtonCadence btn;
    resetEnv(btn);

    pressBtn();
    auto ev1 = stepTime(btn, 800);
    CHECK(ev1.empty()); // Evaluated at release

    releaseBtn();
    auto ev2 = stepTime(btn, 400);
    CHECK(ev2.size() == 1);
    CHECK(at(ev2, 0) == BTN_LONG_PRESS);
}

// 8. Very long press (held 2400 ms without prior tap)
TEST(very_long_press) {
    ButtonCadence btn;
    resetEnv(btn);

    pressBtn();
    auto ev1 = stepTime(btn, 2400);
    CHECK(ev1.empty());

    releaseBtn();
    auto ev2 = stepTime(btn, 400);
    CHECK(ev2.size() == 1);
    CHECK(at(ev2, 0) == BTN_VERY_LONG_PRESS);
}

// 9. Panic firing exactly once at 6000 ms
TEST(panic_firing_exactly_once_at_6000ms) {
    ButtonCadence btn;
    resetEnv(btn);

    uint32_t t0 = hoststub::now_ms;
    pressBtn();
    btn.update(); // Sample at t0 so _lastDebounceTime records t0

    uint32_t panicFiredTime = 0;
    int panicCount = 0;

    // Advance 1 ms at a time
    for (uint32_t i = 1; i <= 8000; i++) {
        hoststub::now_ms = t0 + i;
        ButtonEvent ev = btn.update();
        if (ev == BTN_PANIC_HOLD) {
            panicCount++;
            if (panicFiredTime == 0) panicFiredTime = hoststub::now_ms;
        } else if (ev != BTN_NONE) {
            CHECK(false); // No other event should fire
        }
    }

    CHECK(panicCount == 1);
    CHECK(panicFiredTime == t0 + 6000);

    // Release emits nothing
    releaseBtn();
    auto evRel = stepTime(btn, 400);
    CHECK(evRel.empty());
}

// 10. Contact bounce (20 ms LOW glitch rejected by 35 ms debouncer)
TEST(contact_bounce_rejected) {
    ButtonCadence btn;
    resetEnv(btn);

    // Glitch LOW for 20 ms
    pressBtn();
    stepTime(btn, 20);
    releaseBtn();

    auto ev = stepTime(btn, 500);
    CHECK(ev.empty());
    CHECK(!btn.isPressedNow());
    CHECK(btn.currentHoldDuration() == 0);
    CHECK(btn.getHoldStage() == 0);
}

// 11. Triple tap: double click followed by a single tap
TEST(triple_tap) {
    ButtonCadence btn;
    resetEnv(btn);

    // Tap 1
    pressBtn();
    stepTime(btn, 50);
    releaseBtn();
    stepTime(btn, 80);

    // Tap 2 (completes double click)
    pressBtn();
    stepTime(btn, 50);
    releaseBtn();
    auto ev1 = stepTime(btn, 80);
    CHECK(ev1.size() == 1);
    CHECK(at(ev1, 0) == BTN_DOUBLE_CLICK);

    // Tap 3 (independent single tap)
    pressBtn();
    stepTime(btn, 50);
    releaseBtn();
    auto ev2 = stepTime(btn, 400);
    CHECK(ev2.size() == 1);
    CHECK(at(ev2, 0) == BTN_SHORT_PRESS);
}

// 12. getHoldStage and currentHoldDuration verification
TEST(hold_stage_and_current_hold_duration) {
    ButtonCadence btn;
    resetEnv(btn);

    CHECK(!btn.isPressedNow());
    CHECK(btn.currentHoldDuration() == 0);
    CHECK(btn.getHoldStage() == 0);

    pressBtn();
    btn.update(); // Sample at t0 so _lastDebounceTime records t0

    // Step 200 ms: hold stage 0
    stepTime(btn, 200);
    CHECK(btn.isPressedNow());
    CHECK(btn.currentHoldDuration() == 200);
    CHECK(btn.getHoldStage() == 0);

    // Step to 800 ms: hold stage 1 (Confirm, >=600 ms)
    stepTime(btn, 600);
    CHECK(btn.isPressedNow());
    CHECK(btn.currentHoldDuration() == 800);
    CHECK(btn.getHoldStage() == 1);

    // Step to 2400 ms: hold stage 2 (Reset / Back, >=2200 ms)
    stepTime(btn, 1600);
    CHECK(btn.isPressedNow());
    CHECK(btn.currentHoldDuration() == 2400);
    CHECK(btn.getHoldStage() == 2);

    // Step to 6200 ms: hold stage 3 (Panic, >=6000 ms)
    stepTime(btn, 3800);
    CHECK(btn.isPressedNow());
    CHECK(btn.currentHoldDuration() == 6200);
    CHECK(btn.getHoldStage() == 3);

    // Release: immediately drops to 0
    releaseBtn();
    stepTime(btn, 50);
    CHECK(!btn.isPressedNow());
    CHECK(btn.currentHoldDuration() == 0);
    CHECK(btn.getHoldStage() == 0);
}

// 13. Late second press at +300 ms (inside 320 ms window) gives DOUBLE_CLICK despite debounce latency
TEST(late_second_press_gives_double_click) {
    ButtonCadence btn;
    resetEnv(btn);

    // Tap 1: hold 100 ms, release
    pressBtn();
    stepTime(btn, 100);
    releaseBtn();

    // Gap 300 ms (inside 320 ms window)
    auto evGap = stepTime(btn, 300);
    CHECK(evGap.empty());

    // Tap 2: pressed at +300 ms, held 100 ms, release
    pressBtn();
    stepTime(btn, 100);
    releaseBtn();

    // Settle 800 ms
    auto evSettle = stepTime(btn, 800);
    CHECK(evSettle.size() == 1);
    CHECK(at(evSettle, 0) == BTN_DOUBLE_CLICK);
}

// 14. Second press at +330 ms (outside 320 ms window) gives SHORT then SHORT
TEST(second_press_outside_window_gives_two_shorts) {
    ButtonCadence btn;
    resetEnv(btn);

    // Tap 1: hold 100 ms, release
    pressBtn();
    stepTime(btn, 100);
    releaseBtn();

    // Gap 330 ms (exceeds 320 ms window): tap 1 confirms SHORT
    auto evGap = stepTime(btn, 330);
    CHECK(evGap.size() == 1);
    CHECK(at(evGap, 0) == BTN_SHORT_PRESS);

    // Tap 2: hold 100 ms, release
    pressBtn();
    stepTime(btn, 100);
    releaseBtn();

    // Settle 400 ms: tap 2 confirms SHORT
    auto evSettle = stepTime(btn, 400);
    CHECK(evSettle.size() == 1);
    CHECK(at(evSettle, 0) == BTN_SHORT_PRESS);
}

// 15. 10 ms bounce at +300 ms followed by nothing gives exactly one SHORT
TEST(bounce_at_300ms_gives_one_short) {
    ButtonCadence btn;
    resetEnv(btn);

    // Tap 1: hold 100 ms, release
    pressBtn();
    stepTime(btn, 100);
    releaseBtn();

    // Gap 300 ms
    auto evGap = stepTime(btn, 300);
    CHECK(evGap.empty());

    // 10 ms bounce glitch at +300 ms
    pressBtn();
    auto evBounce = stepTime(btn, 10);
    CHECK(evBounce.empty());
    releaseBtn();

    // Idle settles: debounce rejects the 10 ms glitch; timeout fires for Tap 1
    auto evSettle = stepTime(btn, 500);
    CHECK(evSettle.size() == 1);
    CHECK(at(evSettle, 0) == BTN_SHORT_PRESS);
}

int main() {
    RUN(single_tap);
    RUN(fast_double);
    RUN(slow_hold_double);
    RUN(tap_then_long);
    RUN(tap_then_very_long);
    RUN(tap_then_panic);
    RUN(long_press);
    RUN(very_long_press);
    RUN(panic_firing_exactly_once_at_6000ms);
    RUN(contact_bounce_rejected);
    RUN(triple_tap);
    RUN(hold_stage_and_current_hold_duration);
    RUN(late_second_press_gives_double_click);
    RUN(second_press_outside_window_gives_two_shorts);
    RUN(bounce_at_300ms_gives_one_short);
    DONE();
}
