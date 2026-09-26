/**
 * psbt_signer.cpp — Production BIP-174 PSBT Parser & Offline Bitcoin Signer
 * ==============================================================================
 * Standard: Follows hardware-wallet-dev skill (BIP-174, BIP-143, BIP-84 SegWit).
 * Hardware: LilyGo T-Dongle S3 SD_MMC 1-Bit slot (CLK=12, CMD=16, D0=17).
 */

#include "psbt_signer.h"

bool PsbtSigner::_mounted = false;

// ─── 1. SDIO 1-Bit Initialization & Power Management ─────────────────────────
bool PsbtSigner::initSD() {
    if (_mounted) return true;

    // Official LilyGO T-Dongle S3 SDIO pins
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    if (!SD_MMC.begin("/sdcard", false)) {
        SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
        if (!SD_MMC.begin("/sdcard", true)) {
            _mounted = (SD_MMC.cardType() != CARD_NONE);
            return _mounted;
        }
    }

    _mounted = (SD_MMC.cardType() != CARD_NONE);
    return _mounted;
}

void PsbtSigner::closeSD() {
    if (_mounted) {
        SD_MMC.end();
        _mounted = false;
    }
}

bool PsbtSigner::isCardMounted() {
    return _mounted;
}

// ─── 2. Scan for Pending Unsigned PSBT Files ─────────────────────────────────
bool PsbtSigner::findPendingPsbt(char* outPath, size_t maxLen) {
    if (!initSD()) return false;

    // First scan root /
    File root = SD_MMC.open("/");
    if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                const char* name = file.name();
                size_t nLen = strlen(name);
                if (nLen > 5 && strcasecmp(name + nLen - 5, ".psbt") == 0) {
                    if (strstr(name, "-signed") == nullptr && strstr(name, "_signed") == nullptr) {
                        snprintf(outPath, maxLen, "/%s", name);
                        file.close();
                        root.close();
                        return true;
                    }
                }
            }
            file = root.openNextFile();
        }
        root.close();
    }

    // Next check /psbt subfolder
    File dir = SD_MMC.open("/psbt");
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                const char* name = file.name();
                size_t nLen = strlen(name);
                if (nLen > 5 && strcasecmp(name + nLen - 5, ".psbt") == 0) {
                    if (strstr(name, "-signed") == nullptr && strstr(name, "_signed") == nullptr) {
                        snprintf(outPath, maxLen, "/psbt/%s", name);
                        file.close();
                        dir.close();
                        return true;
                    }
                }
            }
            file = dir.openNextFile();
        }
        dir.close();
    }

    return false;
}

// ─── 5. Base64 Decoder / Encoder ─────────────────────────────────────────────
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool PsbtSigner::decodeBase64(const char* in, size_t inLen, uint8_t* out, size_t& outLen, size_t maxOut) {
    outLen = 0;
    uint32_t buf = 0;
    int bits = 0;

    for (size_t i = 0; i < inLen; i++) {
        char c = in[i];
        if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
        const char* p = strchr(b64_table, c);
        if (!p) return false;
        buf = (buf << 6) | (uint32_t)(p - b64_table);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (outLen >= maxOut) return false;
            out[outLen++] = (uint8_t)((buf >> bits) & 0xFF);
        }
    }
    return true;
}

bool PsbtSigner::encodeBase64(const uint8_t* in, size_t inLen, char* out, size_t maxOut) {
    size_t o = 0;
    for (size_t i = 0; i < inLen; i += 3) {
        uint32_t b = ((uint32_t)in[i]) << 16;
        if (i + 1 < inLen) b |= ((uint32_t)in[i + 1]) << 8;
        if (i + 2 < inLen) b |= (uint32_t)in[i + 2];

        if (o + 4 >= maxOut) return false;
        out[o++] = b64_table[(b >> 18) & 0x3F];
        out[o++] = b64_table[(b >> 12) & 0x3F];
        out[o++] = (i + 1 < inLen) ? b64_table[(b >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < inLen) ? b64_table[b & 0x3F] : '=';
    }
    out[o] = '\0';
    return true;
}

void PsbtSigner::formatSatoshis(uint64_t sats, char* outBuf, size_t maxLen) {
    uint64_t btc = sats / 100000000ULL;
    uint64_t rem = sats % 100000000ULL;
    snprintf(outBuf, maxLen, "%llu.%08llu BTC", btc, rem);
}

// ─── 6. PSBT Load / Analyze / Sign (the Bitcoin logic lives in psbt_core) ────
uint8_t* PsbtSigner::loadPsbt(const char* filePath, size_t* len, bool* wasBase64) {
    *len = 0;
    *wasBase64 = false;
    if (!initSD()) return nullptr;
    File f = SD_MMC.open(filePath, FILE_READ);
    if (!f) return nullptr;
    size_t fSize = f.size();
    if (fSize < 5 || fSize > PSBT_MAX_FILE_BYTES) {
        f.close();
        return nullptr;
    }
    uint8_t* raw = (uint8_t*)malloc(fSize);
    if (!raw || f.read(raw, fSize) != fSize) {
        f.close();
        free(raw);
        return nullptr;
    }
    f.close();
    if (memcmp(raw, "psbt\xff", 5) == 0) {   // binary PSBT
        *len = fSize;
        return raw;
    }
    // Base64 ("cHNidP8..." is "psbt\xff"), as Sparrow and Bitcoin Core export it
    uint8_t* bin = (uint8_t*)malloc(fSize);
    size_t binLen = 0;
    bool ok = bin && decodeBase64((const char*)raw, fSize, bin, binLen, fSize) &&
              binLen >= 5 && memcmp(bin, "psbt\xff", 5) == 0;
    free(raw);
    if (!ok) {
        free(bin);
        return nullptr;
    }
    *len = binLen;
    *wasBase64 = true;
    return bin;
}

static bool psbtDerive(const uint32_t* path, size_t depth, uint8_t pub33[33], void* ctx) {
    return static_cast<const CryptoWallet*>(ctx)->btcDerivePubkey(path, depth, pub33);
}

static bool psbtSign(const uint32_t* path, size_t depth, const uint8_t digest[32],
                     uint8_t* der, size_t cap, size_t* len, void* ctx) {
    return static_cast<const CryptoWallet*>(ctx)->btcSignDigest(path, depth, digest, der, cap, len);
}

bool PsbtSigner::fillOptions(const CryptoWallet& wallet, PsbtCore::Options* opt) {
    memset(opt, 0, sizeof(*opt));
    if (!wallet.btcMasterFingerprint(opt->masterFingerprint)) return false;   // vault locked
    opt->testnet = false;
    opt->derive = psbtDerive;
    opt->deriveCtx = (void*)&wallet;
    opt->sign = psbtSign;
    opt->signCtx = (void*)&wallet;
    return true;
}

bool PsbtSigner::parsePsbtFile(const char* filePath, PsbtTxDetails& d, const CryptoWallet& wallet) {
    memset(&d, 0, sizeof(d));
    strncpy(d.fileName, filePath, sizeof(d.fileName) - 1);
    d.error = PsbtCore::ERR_FORMAT;
    size_t len;
    bool b64;
    uint8_t* psbt = loadPsbt(filePath, &len, &b64);
    if (!psbt) return false;
    PsbtCore::Options opt;
    if (!fillOptions(wallet, &opt)) {
        d.error = PsbtCore::ERR_ARGS;
        free(psbt);
        return false;
    }
    d.error = PsbtCore::analyze(psbt, len, opt, &d.r);
    free(psbt);
    d.isValid = d.error == PsbtCore::OK;
    if (d.isValid) {
        if (d.r.outputCount) {
            strncpy(d.recipientAddr, d.r.outputs[0].address, sizeof(d.recipientAddr) - 1);
            d.sendSatoshis = d.r.externalSats;
        }
        d.feeSatoshis = d.r.feeSats;
        d.changeSatoshis = d.r.changeSats;
        d.numInputs = (uint32_t)(d.r.oursInputs + d.r.foreignInputs);
        d.numOutputs = (uint32_t)d.r.outputCount;
    }
    return d.isValid;
}

bool PsbtSigner::signPsbtFile(const char* filePath, const CryptoWallet& wallet, char* outSignedPath, size_t maxLen) {
    size_t len;
    bool b64;
    uint8_t* psbt = loadPsbt(filePath, &len, &b64);
    if (!psbt) return false;
    PsbtCore::Options opt;
    size_t cap = len + PsbtCore::MAX_INPUTS * 110;   // room for one partial signature per input
    uint8_t* out = (uint8_t*)malloc(cap);
    size_t outLen = 0;
    PsbtCore::Error e = PsbtCore::ERR_ARGS;
    if (out && fillOptions(wallet, &opt)) e = PsbtCore::sign(psbt, len, opt, out, cap, &outLen, nullptr);
    free(psbt);
    if (e != PsbtCore::OK) {
        Serial.printf("[PSBT] Not signed: %s\n", PsbtCore::errorName(e));
        free(out);
        return false;
    }

    // "<name>-signed.psbt" next to the original, in the same encoding
    char path[96];
    const char* dot = strrchr(filePath, '.');
    size_t stem = dot ? (size_t)(dot - filePath) : strlen(filePath);
    snprintf(path, sizeof(path), "%.*s-signed.psbt", (int)stem, filePath);
    bool ok = false;
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) {
        if (b64) {
            size_t encCap = (outLen + 2) / 3 * 4 + 1;
            char* enc = (char*)malloc(encCap);
            ok = enc && encodeBase64(out, outLen, enc, encCap) && f.write((const uint8_t*)enc, strlen(enc)) == strlen(enc);
            free(enc);
        } else {
            ok = f.write(out, outLen) == outLen;
        }
        f.close();
    }
    free(out);
    if (!ok) {
        SD_MMC.remove(path);
        return false;
    }
    strncpy(outSignedPath, path, maxLen - 1);
    outSignedPath[maxLen - 1] = '\0';
    return true;
}
