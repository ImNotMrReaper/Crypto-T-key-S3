// Host-test in-memory SD_MMC stub
#pragma once
#include "FS.h"
#include <stdint.h>

typedef enum {
    CARD_NONE,
    CARD_MMC,
    CARD_SD,
    CARD_SDHC,
    CARD_UNKNOWN
} sdcard_type_t;

namespace hoststub {
inline sdcard_type_t sd_card_type = CARD_SD;
inline uint64_t sd_card_size = 16ULL * 1024 * 1024 * 1024;
}

class SDMMCFS : public FS {
public:
    bool setPins(int clk, int cmd, int d0, int d1 = -1, int d2 = -1, int d3 = -1) {
        (void)clk; (void)cmd; (void)d0; (void)d1; (void)d2; (void)d3;
        return true;
    }

    bool begin(const char* mountpoint = "/sdcard", bool mode1bit = false) {
        (void)mountpoint; (void)mode1bit;
        return hoststub::sd_card_type != CARD_NONE;
    }

    void end() {}

    sdcard_type_t cardType() const {
        return hoststub::sd_card_type;
    }

    uint64_t cardSize() const {
        return hoststub::sd_card_size;
    }

    uint64_t usedBytes() const {
        uint64_t total = 0;
        for (const auto& kv : hoststub::sd_files) {
            if (kv.second && kv.second->exists) {
                total += kv.second->data.size();
            }
        }
        return total;
    }
};

inline SDMMCFS SD_MMC;
