#include "test.h"
#include "wifi_manager.h"

namespace WallTicker {
    void start(bool) {}
    bool running() { return false; }
    void applyResults() {}
}


TEST(wifi_credentials_and_orphaned_keys_cleanup) {
    hoststub::nvs.clear();

    WifiManager wm;
    wm.begin();

    CHECK(wm.getSavedCount() == 0);

    // Add 3 networks
    CHECK(wm.addNetwork("HomeWiFi", "SecretPass123"));
    CHECK(wm.addNetwork("OfficeGuest", "OfficePassword!"));
    CHECK(wm.addNetwork("Hotspot", "PhoneData456"));
    CHECK(wm.getSavedCount() == 3);

    // Check NVS entries
    auto& nvs = hoststub::nvs["wifi_cfg"];
    CHECK(nvs.count("count") == 1);
    CHECK(nvs.count("s_0") == 1);
    CHECK(nvs.count("p_0") == 1);
    CHECK(nvs.count("s_1") == 1);
    CHECK(nvs.count("p_1") == 1);
    CHECK(nvs.count("s_2") == 1);
    CHECK(nvs.count("p_2") == 1);

    // Remove middle network "OfficeGuest"
    CHECK(wm.removeNetwork("OfficeGuest"));
    CHECK(wm.getSavedCount() == 2);

    // Verify memory array
    CHECK(strcmp(wm.getNetwork(0)->ssid, "HomeWiFi") == 0);
    CHECK(strcmp(wm.getNetwork(1)->ssid, "Hotspot") == 0);
    CHECK(wm.getNetwork(2) == nullptr);

    // Verify NVS keys: count == 2, s_0 and s_1 exist, and ORPHANED keys s_2 and p_2 are completely removed!
    CHECK(nvs.count("s_0") == 1);
    CHECK(nvs.count("p_0") == 1);
    CHECK(nvs.count("s_1") == 1);
    CHECK(nvs.count("p_1") == 1);
    CHECK(nvs.count("s_2") == 0); // Orphaned key MUST be erased!
    CHECK(nvs.count("p_2") == 0); // Orphaned key MUST be erased!

    // Remove first network "HomeWiFi"
    CHECK(wm.removeNetwork("HomeWiFi"));
    CHECK(wm.getSavedCount() == 1);
    CHECK(strcmp(wm.getNetwork(0)->ssid, "Hotspot") == 0);

    // Now s_1 and p_1 must also be erased from NVS
    CHECK(nvs.count("s_0") == 1);
    CHECK(nvs.count("p_0") == 1);
    CHECK(nvs.count("s_1") == 0);
    CHECK(nvs.count("p_1") == 0);
    CHECK(nvs.count("s_2") == 0);
    CHECK(nvs.count("p_2") == 0);

    // Reload in fresh manager from NVS
    WifiManager wm2;
    wm2.begin();
    CHECK(wm2.getSavedCount() == 1);
    CHECK(strcmp(wm2.getNetwork(0)->ssid, "Hotspot") == 0);
    CHECK(strcmp(wm2.getNetwork(0)->pass, "PhoneData456") == 0);

    // Remove last network
    CHECK(wm2.removeNetwork("Hotspot"));
    CHECK(wm2.getSavedCount() == 0);
    CHECK(nvs.count("s_0") == 0);
    CHECK(nvs.count("p_0") == 0);
}

TEST(wifi_manager_bounds_and_updates) {
    hoststub::nvs.clear();

    WifiManager wm;
    wm.begin();

    // Rejection of invalid inputs
    CHECK(!wm.addNetwork(nullptr, "pass"));
    CHECK(!wm.addNetwork("", "pass"));
    CHECK(!wm.addNetwork("123456789012345678901234567890123", "pass")); // 33 chars > 32
    char longPass[70];
    memset(longPass, 'A', sizeof(longPass));
    longPass[65] = '\0';
    CHECK(!wm.addNetwork("ValidSSID", longPass)); // 65 chars > 64

    // Valid update of existing SSID
    CHECK(wm.addNetwork("MyRouter", "InitialPass"));
    CHECK(wm.getSavedCount() == 1);
    CHECK(strcmp(wm.getNetwork(0)->pass, "InitialPass") == 0);

    CHECK(wm.addNetwork("MyRouter", "UpdatedPass"));
    CHECK(wm.getSavedCount() == 1); // count didn't increase
    CHECK(strcmp(wm.getNetwork(0)->pass, "UpdatedPass") == 0);

    // Fill to capacity MAX_WIFI_NETWORKS (10)
    for (int i = 1; i < MAX_WIFI_NETWORKS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Net_%d", i);
        CHECK(wm.addNetwork(name, "pass"));
    }
    CHECK(wm.getSavedCount() == 10);

    // 11th network must be rejected
    CHECK(!wm.addNetwork("OverflowNet", "pass"));
    CHECK(wm.getSavedCount() == 10);
}

int main() {
    RUN(wifi_credentials_and_orphaned_keys_cleanup);
    RUN(wifi_manager_bounds_and_updates);
    DONE();
}
