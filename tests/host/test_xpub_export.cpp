// BIP-84 account xpub/zpub export cross-checked against embit.
#include "test.h"
#include "xpub_export.h"
#include "bip32_engine.h"
#include <string.h>

static const char MNEMONIC[] =
    "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
static const char EXPECTED_XPUB[] =
    "xpub6CatWdiZiodmUeTDp8LT5or8nmbKNcuyvz7WyksVFkKB4RHwCD3XyuvPEbvqAQY3rAPshWcMLoP2fMFMKHPJ4ZeZXYVUhLv1VMrjPC7PW6V";
static const char EXPECTED_ZPUB[] =
    "zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG1m4wAcvPhXNfE3EfH1r1ADqtfSdVCToUG868RvUUkgDKf31mGDtKsAYz2oz2AGutZYs";
static const char EXPECTED_ORIGIN[] = "[73c5da0a/84h/0h/0h]";

TEST(bip84_account_xpub_zpub_match_embit) {
    uint8_t seed[64] = {};
    char xpub[XpubExport::EXTENDED_KEY_TEXT_LEN] = {};
    char zpub[XpubExport::EXTENDED_KEY_TEXT_LEN] = {};
    char origin[XpubExport::KEY_ORIGIN_TEXT_LEN] = {};
    CHECK(Bip32Engine::mnemonicToSeed(MNEMONIC, "", seed));
    CHECK(XpubExport::exportBip84Account(seed, xpub, sizeof(xpub), zpub, sizeof(zpub), origin, sizeof(origin)));
    CHECK(strcmp(xpub, EXPECTED_XPUB) == 0);
    CHECK(strcmp(zpub, EXPECTED_ZPUB) == 0);
    CHECK(strcmp(origin, EXPECTED_ORIGIN) == 0);
    Bip32Engine::secureZero(seed, sizeof(seed));
}

TEST(export_fails_closed_for_short_buffers) {
    uint8_t seed[64] = {};
    char xpub[XpubExport::EXTENDED_KEY_TEXT_LEN] = "not empty";
    char zpub[XpubExport::EXTENDED_KEY_TEXT_LEN] = "not empty";
    char origin[8] = "filled";
    CHECK(Bip32Engine::mnemonicToSeed(MNEMONIC, "", seed));
    CHECK(!XpubExport::exportBip84Account(seed, xpub, sizeof(xpub), zpub, sizeof(zpub), origin, sizeof(origin)));
    CHECK(xpub[0] == '\0' && zpub[0] == '\0' && origin[0] == '\0');
    Bip32Engine::secureZero(seed, sizeof(seed));
}

int main() {
    RUN(bip84_account_xpub_zpub_match_embit);
    RUN(export_fails_closed_for_short_buffers);
    DONE();
}
