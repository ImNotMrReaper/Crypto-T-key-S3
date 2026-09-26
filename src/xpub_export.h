#pragma once

#include <stddef.h>
#include <stdint.h>

namespace XpubExport {

constexpr size_t EXTENDED_KEY_TEXT_LEN = 112;
constexpr size_t KEY_ORIGIN_TEXT_LEN = 32;

// Export the BIP-84 account node m/84'/0'/0' as standard xpub and SLIP-132 zpub.
// origin receives the descriptor origin, for example [73c5da0a/84h/0h/0h].
// Every output buffer must include room for the terminating NUL.
bool exportBip84Account(const uint8_t seed[64], char* xpub, size_t xpubCap,
                        char* zpub, size_t zpubCap, char* origin, size_t originCap);

}  // namespace XpubExport
