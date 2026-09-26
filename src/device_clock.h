/**
 * device_clock.h — Wall-clock time for the home screen.
 *
 * The ESP32 has no battery-backed clock, so the time comes from the laptop companion over USB
 * (serial `time <unix-epoch> [utc-offset-minutes]`, sent about once a minute) or from NTP during
 * a wall-power Wi-Fi burst. Both set the system clock; the UTC offset is remembered in NVS.
 * Display only: no security decision ever depends on this time.
 */
#pragma once
#include <Arduino.h>
#include <time.h>

namespace DeviceClock {
    void begin();                               // loads the saved UTC offset
    bool set(uint32_t epoch, int16_t offsetMin, bool offsetGiven);
    bool isValid();                             // true once a plausible time has been set
    bool localTime(struct tm* out);             // local time (UTC + offset); false if unset
    int16_t offsetMinutes();
}
