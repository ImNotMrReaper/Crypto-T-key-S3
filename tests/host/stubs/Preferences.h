// Host-test Preferences: an in-memory NVS with failure injection.
#pragma once
#include <map>
#include <string>
#include <vector>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "Arduino.h"

namespace hoststub {
// namespace -> key -> bytes
extern std::map<std::string, std::map<std::string, std::vector<uint8_t>>> nvs;
extern bool nvs_fail_writes;     // when true every put* returns 0 (a full or broken NVS)
}

class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false) { _ns = ns; _ro = readOnly; return true; }
    void end() {}
    bool clear() { if (_ro) return false; hoststub::nvs[_ns].clear(); return true; }
    bool isKey(const char* k) { return hoststub::nvs[_ns].count(k) > 0; }
    bool remove(const char* k) { if (_ro) return false; return hoststub::nvs[_ns].erase(k) > 0; }
    size_t putBytes(const char* k, const void* v, size_t n) {
        if (_ro || hoststub::nvs_fail_writes) return 0;
        auto p = (const uint8_t*)v;
        hoststub::nvs[_ns][k] = std::vector<uint8_t>(p, p + n);
        return n;
    }
    size_t getBytesLength(const char* k) { auto& m = hoststub::nvs[_ns]; return m.count(k) ? m[k].size() : 0; }
    size_t getBytes(const char* k, void* out, size_t n) {
        auto& m = hoststub::nvs[_ns];
        if (!m.count(k) || m[k].size() > n) return 0;
        memcpy(out, m[k].data(), m[k].size());
        return m[k].size();
    }
    size_t putUChar(const char* k, uint8_t v) { return putBytes(k, &v, 1); }
    uint8_t getUChar(const char* k, uint8_t d = 0) { uint8_t v = d; getBytes(k, &v, 1); return v; }
    size_t putInt(const char* k, int32_t v) { return putBytes(k, &v, sizeof(v)); }
    int32_t getInt(const char* k, int32_t d = 0) { int32_t v = d; getBytes(k, &v, sizeof(v)); return v; }
    size_t putUInt(const char* k, uint32_t v) { return putBytes(k, &v, 4); }
    uint32_t getUInt(const char* k, uint32_t d = 0) { uint32_t v = d; getBytes(k, &v, 4); return v; }
    size_t putString(const char* k, const char* v) { return putBytes(k, v, strlen(v)); }
    String getString(const char* k, const char* d = "") {
        auto& m = hoststub::nvs[_ns];
        return m.count(k) ? String(std::string(m[k].begin(), m[k].end())) : String(d);
    }
    size_t getString(const char* k, char* out, size_t maxLen) {
        auto& m = hoststub::nvs[_ns];
        if (!m.count(k) || maxLen == 0) return 0;
        size_t len = std::min(m[k].size(), maxLen - 1);
        memcpy(out, m[k].data(), len);
        out[len] = '\0';
        return len;
    }
    size_t putBool(const char* k, bool v) { return putUChar(k, v); }
    bool getBool(const char* k, bool d = false) { return getUChar(k, d); }
private:
    std::string _ns;
    bool _ro = false;
};
