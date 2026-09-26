#pragma once
#include <stdint.h>
#include <string.h>

typedef enum {
    ESP_MAC_WIFI_STA,
    ESP_MAC_WIFI_SOFTAP,
    ESP_MAC_BT,
    ESP_MAC_ETH,
} esp_mac_type_t;

inline int esp_read_mac(uint8_t* mac, esp_mac_type_t type) {
    (void)type;
    static const uint8_t mockMac[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    memcpy(mac, mockMac, 6);
    return 0;
}
