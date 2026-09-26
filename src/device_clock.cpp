#include "device_clock.h"
#include <Preferences.h>
#include <sys/time.h>

static const uint32_t EPOCH_MIN = 1700000000UL;   // Nov 2023: anything earlier is "unset"
static const uint32_t EPOCH_MAX = 4102444800UL;   // 2100-01-01
static int16_t s_offsetMin = 0;

namespace DeviceClock {

void begin() {
    Preferences p;
    p.begin("ui_theme", true);
    s_offsetMin = (int16_t)p.getUInt("tz_off", 0);
    p.end();
}

bool set(uint32_t epoch, int16_t offsetMin, bool offsetGiven) {
    if (epoch < EPOCH_MIN || epoch > EPOCH_MAX) return false;
    if (offsetGiven) {
        if (offsetMin < -14 * 60 || offsetMin > 14 * 60) return false;   // real UTC offsets only
        if (offsetMin != s_offsetMin) {
            s_offsetMin = offsetMin;
            Preferences p;
            p.begin("ui_theme", false);
            p.putUInt("tz_off", (uint32_t)(int32_t)offsetMin);   // written only when it changes
            p.end();
        }
    }
    struct timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    return true;
}

bool isValid() {
    return time(nullptr) >= (time_t)EPOCH_MIN;
}

bool localTime(struct tm* out) {
    if (!out || !isValid()) return false;
    time_t t = time(nullptr) + (time_t)s_offsetMin * 60;
    gmtime_r(&t, out);
    return true;
}

int16_t offsetMinutes() {
    return s_offsetMin;
}

}  // namespace DeviceClock
