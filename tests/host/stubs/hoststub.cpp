#include "Arduino.h"
#include "Preferences.h"
#include "WiFi.h"
namespace hoststub {
uint32_t now_ms = 0;
int pin_level[64] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
std::map<std::string, std::map<std::string, std::vector<uint8_t>>> nvs;
bool nvs_fail_writes = false;
}
HostSerial Serial;
WiFiClass WiFi;
