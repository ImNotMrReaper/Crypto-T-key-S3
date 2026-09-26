#pragma once
typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;
#define ESP_OK 0
inline int esp_wifi_get_mode(wifi_mode_t* m) { *m = WIFI_MODE_NULL; return ESP_OK; }
inline int esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }
