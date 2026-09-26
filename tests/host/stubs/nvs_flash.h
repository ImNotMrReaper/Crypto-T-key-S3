#pragma once
#include <stdint.h>

#define ESP_OK 0

inline int nvs_flash_erase() {
    return ESP_OK;
}

inline int nvs_flash_init() {
    return ESP_OK;
}
