// Host-test RNG: /dev/urandom stands in for the ESP32-S3 TRNG.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
inline void esp_fill_random(void* buf, size_t len) {
    FILE* f = fopen("/dev/urandom", "rb");
    size_t n = f ? fread(buf, 1, len, f) : 0;
    if (f) fclose(f);
    (void)n;
}
inline uint32_t esp_random() { uint32_t v; esp_fill_random(&v, sizeof(v)); return v; }
