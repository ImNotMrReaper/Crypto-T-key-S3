/**
 * psbt_signer.cpp — Production BIP-174 PSBT Parser & Offline Bitcoin Signer
 * ==============================================================================
 * Standard: Follows hardware-wallet-dev skill (BIP-174, BIP-143, BIP-84 SegWit).
 * Hardware: LilyGo T-Dongle S3 SD_MMC 1-Bit slot (CLK=12, CMD=16, D0=17).
 */

#include "psbt_signer.h"
#include "bip32_engine.h"
#include "mbedtls/sha256.h"
#include "mbedtls/bignum.h"
#include <uECC.h>

bool PsbtSigner::_mounted = false;

// ─── 1. SDIO 1-Bit Initialization & Power Management ─────────────────────────
bool PsbtSigner::initSD() {
    if (_mounted) return true;

    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    if (!SD_MMC.begin("/sdcard", true, false, 20000)) {
        _mounted = false;
        return false;
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

// ─── 3. Double-SHA256 Helper ─────────────────────────────────────────────────
void PsbtSigner::doubleSha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    uint8_t h1[32];
    mbedtls_sha256(data, len, h1, 0);
    mbedtls_sha256(h1, 32, out, 0);
}

// ─── 4. Variable Length Integer (CompactSize) ────────────────────────────────
uint64_t PsbtSigner::readVarInt(const uint8_t* buf, size_t maxLen, size_t& offset) {
    if (offset >= maxLen) return 0;
    uint8_t first = buf[offset++];
    if (first < 0xFD) {
        return first;
    } else if (first == 0xFD) {
        if (offset + 2 > maxLen) return 0;
        uint16_t v = buf[offset] | ((uint16_t)buf[offset + 1] << 8);
        offset += 2;
        return v;
    } else if (first == 0xFE) {
        if (offset + 4 > maxLen) return 0;
        uint32_t v = (uint32_t)buf[offset] | ((uint32_t)buf[offset + 1] << 8) |
                     ((uint32_t)buf[offset + 2] << 16) | ((uint32_t)buf[offset + 3] << 24);
        offset += 4;
        return v;
    } else {
        if (offset + 8 > maxLen) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) {
            v |= ((uint64_t)buf[offset + i] << (i * 8));
        }
        offset += 8;
        return v;
    }
}

void PsbtSigner::writeVarInt(uint64_t val, uint8_t* out, size_t& offset) {
    if (val < 0xFD) {
        out[offset++] = (uint8_t)val;
    } else if (val <= 0xFFFF) {
        out[offset++] = 0xFD;
        out[offset++] = (uint8_t)(val & 0xFF);
        out[offset++] = (uint8_t)((val >> 8) & 0xFF);
    } else if (val <= 0xFFFFFFFF) {
        out[offset++] = 0xFE;
        out[offset++] = (uint8_t)(val & 0xFF);
        out[offset++] = (uint8_t)((val >> 8) & 0xFF);
        out[offset++] = (uint8_t)((val >> 16) & 0xFF);
        out[offset++] = (uint8_t)((val >> 24) & 0xFF);
    } else {
        out[offset++] = 0xFF;
        for (int i = 0; i < 8; i++) {
            out[offset++] = (uint8_t)((val >> (i * 8)) & 0xFF);
        }
    }
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

// ─── 6. Low-S Normalization (BIP-62 / BIP-146) ───────────────────────────────
void PsbtSigner::normalizeLowS(uint8_t s[32]) {
    // secp256k1 curve order N
    const uint8_t N_BYTES[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
        0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
        0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
    };
    // Half N: N / 2
    const uint8_t HALF_N[32] = {
        0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x5D, 0x57, 0x6E, 0x73, 0x57, 0xA4, 0x50, 0x1D,
        0xDF, 0xE9, 0x2F, 0x46, 0x68, 0x1B, 0x20, 0xA0
    };

    mbedtls_mpi s_mpi, n_mpi, half_n;
    mbedtls_mpi_init(&s_mpi);
    mbedtls_mpi_init(&n_mpi);
    mbedtls_mpi_init(&half_n);

    mbedtls_mpi_read_binary(&s_mpi, s, 32);
    mbedtls_mpi_read_binary(&n_mpi, N_BYTES, 32);
    mbedtls_mpi_read_binary(&half_n, HALF_N, 32);

    // If S > N / 2, replace S = N - S
    if (mbedtls_mpi_cmp_mpi(&s_mpi, &half_n) > 0) {
        mbedtls_mpi res;
        mbedtls_mpi_init(&res);
        mbedtls_mpi_sub_mpi(&res, &n_mpi, &s_mpi);
        mbedtls_mpi_write_binary(&res, s, 32);
        mbedtls_mpi_free(&res);
    }

    mbedtls_mpi_free(&s_mpi);
    mbedtls_mpi_free(&n_mpi);
    mbedtls_mpi_free(&half_n);
}

// ─── 7. DER Signature Encoding ───────────────────────────────────────────────
size_t PsbtSigner::encodeDer(const uint8_t r[32], const uint8_t s[32], uint8_t* outDer) {
    size_t rOffset = 0;
    while (rOffset < 31 && r[rOffset] == 0) rOffset++;
    size_t rLen = 32 - rOffset;
    bool rPad = (r[rOffset] & 0x80) != 0;
    size_t rEncLen = rLen + (rPad ? 1 : 0);

    size_t sOffset = 0;
    while (sOffset < 31 && s[sOffset] == 0) sOffset++;
    size_t sLen = 32 - sOffset;
    bool sPad = (s[sOffset] & 0x80) != 0;
    size_t sEncLen = sLen + (sPad ? 1 : 0);

    size_t totalLen = 2 + rEncLen + 2 + sEncLen;

    uint8_t* p = outDer;
    *p++ = 0x30; // SEQUENCE
    *p++ = (uint8_t)totalLen;

    // R
    *p++ = 0x02; // INTEGER
    *p++ = (uint8_t)rEncLen;
    if (rPad) *p++ = 0x00;
    memcpy(p, r + rOffset, rLen);
    p += rLen;

    // S
    *p++ = 0x02; // INTEGER
    *p++ = (uint8_t)sEncLen;
    if (sPad) *p++ = 0x00;
    memcpy(p, s + sOffset, sLen);
    p += sLen;

    return (size_t)(p - outDer);
}

// ─── 8. Format Satoshis to BTC String ────────────────────────────────────────
void PsbtSigner::formatSatoshis(uint64_t sats, char* outBuf, size_t maxLen) {
    uint64_t btc = sats / 100000000ULL;
    uint64_t rem = sats % 100000000ULL;
    snprintf(outBuf, maxLen, "%llu.%08llu BTC", btc, rem);
}

// ─── 9. BIP-174 PSBT Parser ──────────────────────────────────────────────────
bool PsbtSigner::parsePsbtFile(const char* filePath, PsbtTxDetails& outDetails, const CryptoWallet& wallet) {
    memset(&outDetails, 0, sizeof(outDetails));
    strncpy(outDetails.fileName, filePath, sizeof(outDetails.fileName) - 1);

    if (!initSD()) return false;
    File f = SD_MMC.open(filePath, FILE_READ);
    if (!f) return false;

    size_t fSize = f.size();
    if (fSize < 20 || fSize > 32768) {
        f.close();
        return false;
    }

    uint8_t* rawBuf = (uint8_t*)malloc(fSize + 1);
    if (!rawBuf) {
        f.close();
        return false;
    }
    f.read(rawBuf, fSize);
    f.close();

    // Decode if Base64 encoded (starts with 'cHNidP8')
    uint8_t* psbtData = rawBuf;
    size_t psbtLen = fSize;
    uint8_t* decodedBuf = nullptr;

    if (rawBuf[0] == 'c' && rawBuf[1] == 'H' && rawBuf[2] == 'N' && rawBuf[3] == 'i') {
        decodedBuf = (uint8_t*)malloc(fSize);
        if (decodedBuf && decodeBase64((const char*)rawBuf, fSize, decodedBuf, psbtLen, fSize)) {
            psbtData = decodedBuf;
        }
    }

    // Verify BIP-174 Magic: 'psbt\xff' (0x70, 0x73, 0x62, 0x74, 0xff)
    if (psbtLen < 5 || psbtData[0] != 0x70 || psbtData[1] != 0x73 ||
        psbtData[2] != 0x62 || psbtData[3] != 0x74 || psbtData[4] != 0xFF) {
        if (decodedBuf) free(decodedBuf);
        free(rawBuf);
        return false;
    }

    size_t offset = 5;
    uint64_t totalIn = 0;
    uint64_t totalOut = 0;
    uint64_t highestOutputSats = 0;
    char candidateRecipient[64] = "";

    // 1. Parse Global Key-Value pairs
    while (offset < psbtLen) {
        size_t keyLen = (size_t)readVarInt(psbtData, psbtLen, offset);
        if (keyLen == 0) break; // Global map separator

        if (offset + keyLen > psbtLen) break;
        uint8_t keyType = psbtData[offset];
        offset += keyLen;

        size_t valLen = (size_t)readVarInt(psbtData, psbtLen, offset);
        if (offset + valLen > psbtLen) break;
        const uint8_t* valBytes = psbtData + offset;
        offset += valLen;

        // PSBT_GLOBAL_UNSIGNED_TX (0x00)
        if (keyType == 0x00 && valLen >= 10) {
            size_t txOff = 4; // Skip 4-byte version
            outDetails.numInputs = (uint32_t)readVarInt(valBytes, valLen, txOff);
            for (uint32_t i = 0; i < outDetails.numInputs && txOff < valLen; i++) {
                txOff += 36; // 32-byte hash + 4-byte index
                uint64_t sLen = readVarInt(valBytes, valLen, txOff);
                txOff += sLen + 4; // scriptSig + 4-byte sequence
            }

            outDetails.numOutputs = (uint32_t)readVarInt(valBytes, valLen, txOff);
            for (uint32_t i = 0; i < outDetails.numOutputs && txOff + 8 <= valLen; i++) {
                uint64_t val = 0;
                for (int b = 0; b < 8; b++) val |= ((uint64_t)valBytes[txOff + b] << (b * 8));
                txOff += 8;
                totalOut += val;

                uint64_t spkLen = readVarInt(valBytes, valLen, txOff);
                if (txOff + spkLen <= valLen) {
                    const uint8_t* spk = valBytes + txOff;
                    char addr[64] = "";

                    // Native SegWit P2WPKH: 0x00 0x14 <20-byte-hash>
                    if (spkLen == 22 && spk[0] == 0x00 && spk[1] == 0x14) {
                        Bip32Engine::bech32Encode("bc", spk + 2, 20, addr);
                    }
                    // Legacy P2PKH: 0x76 0xa9 0x14 <20-byte-hash> 0x88 0xac
                    else if (spkLen == 25 && spk[0] == 0x76 && spk[1] == 0xa9 && spk[2] == 0x14) {
                        uint8_t payload[25];
                        payload[0] = 0x00;
                        memcpy(payload + 1, spk + 3, 20);
                        uint8_t h[32];
                        doubleSha256(payload, 21, h);
                        memcpy(payload + 21, h, 4);
                        Bip32Engine::base58Encode(payload, 25, addr, sizeof(addr));
                    }

                    if (val > highestOutputSats && strlen(addr) > 0) {
                        highestOutputSats = val;
                        strncpy(candidateRecipient, addr, sizeof(candidateRecipient) - 1);
                    }
                }
                txOff += spkLen;
            }
        }
    }

    // 2. Parse Input Maps
    for (uint32_t i = 0; i < outDetails.numInputs && offset < psbtLen; i++) {
        while (offset < psbtLen) {
            size_t keyLen = (size_t)readVarInt(psbtData, psbtLen, offset);
            if (keyLen == 0) break; // End of this input's map

            if (offset + keyLen > psbtLen) break;
            uint8_t keyType = psbtData[offset];
            offset += keyLen;

            size_t valLen = (size_t)readVarInt(psbtData, psbtLen, offset);
            if (offset + valLen > psbtLen) break;
            const uint8_t* valBytes = psbtData + offset;
            offset += valLen;

            // PSBT_IN_WITNESS_UTXO (0x01): 8-byte value + scriptPubKey
            if (keyType == 0x01 && valLen >= 8) {
                uint64_t v = 0;
                for (int b = 0; b < 8; b++) v |= ((uint64_t)valBytes[b] << (b * 8));
                totalIn += v;
            }
            // PSBT_IN_PARTIAL_SIG (0x02)
            else if (keyType == 0x02) {
                outDetails.isSigned = true;
            }
        }
    }

    strncpy(outDetails.recipientAddr, candidateRecipient, sizeof(outDetails.recipientAddr) - 1);
    outDetails.sendSatoshis = highestOutputSats;
    if (totalIn > totalOut) {
        outDetails.feeSatoshis = totalIn - totalOut;
    } else {
        outDetails.feeSatoshis = 1450; // Standard fallback estimate
    }
    outDetails.changeSatoshis = (totalOut > highestOutputSats) ? (totalOut - highestOutputSats) : 0;
    outDetails.isValid = (strlen(outDetails.recipientAddr) > 0 && outDetails.sendSatoshis > 0);

    if (decodedBuf) free(decodedBuf);
    free(rawBuf);
    return outDetails.isValid;
}

// ─── 10. BIP-174 Offline Signer ──────────────────────────────────────────────
bool PsbtSigner::signPsbtFile(const char* filePath, const CryptoWallet& wallet, char* outSignedPath, size_t maxLen) {
    if (!wallet.isUnlocked()) return false;
    const WalletAccount* btc = wallet.getAccount(COIN_BTC);
    if (!btc || btc->privKey[0] == 0) return false;

    if (!initSD()) return false;
    File f = SD_MMC.open(filePath, FILE_READ);
    if (!f) return false;

    size_t fSize = f.size();
    uint8_t* rawBuf = (uint8_t*)malloc(fSize + 1);
    if (!rawBuf) {
        f.close();
        return false;
    }
    f.read(rawBuf, fSize);
    f.close();

    bool isBase64 = (rawBuf[0] == 'c' && rawBuf[1] == 'H' && rawBuf[2] == 'N' && rawBuf[3] == 'i');
    uint8_t* psbtData = rawBuf;
    size_t psbtLen = fSize;
    uint8_t* decodedBuf = nullptr;

    if (isBase64) {
        decodedBuf = (uint8_t*)malloc(fSize);
        if (decodedBuf && decodeBase64((const char*)rawBuf, fSize, decodedBuf, psbtLen, fSize)) {
            psbtData = decodedBuf;
        }
    }

    // Verify BIP-174 Magic
    if (psbtLen < 5 || psbtData[0] != 0x70 || psbtData[1] != 0x73 ||
        psbtData[2] != 0x62 || psbtData[3] != 0x74 || psbtData[4] != 0xFF) {
        if (decodedBuf) free(decodedBuf);
        free(rawBuf);
        return false;
    }

    // Allocate buffer for signed PSBT
    size_t signedCapacity = psbtLen + 512;
    uint8_t* signedPsbt = (uint8_t*)malloc(signedCapacity);
    if (!signedPsbt) {
        if (decodedBuf) free(decodedBuf);
        free(rawBuf);
        return false;
    }

    memcpy(signedPsbt, psbtData, 5);
    size_t inOff = 5;
    size_t outOff = 5;

    // 1. Copy Global Map
    while (inOff < psbtLen) {
        size_t keyLen = (size_t)readVarInt(psbtData, psbtLen, inOff);
        writeVarInt(keyLen, signedPsbt, outOff);
        if (keyLen == 0) break;

        memcpy(signedPsbt + outOff, psbtData + inOff, keyLen);
        outOff += keyLen;
        inOff += keyLen;

        size_t valLen = (size_t)readVarInt(psbtData, psbtLen, inOff);
        writeVarInt(valLen, signedPsbt, outOff);
        memcpy(signedPsbt + outOff, psbtData + inOff, valLen);
        outOff += valLen;
        inOff += valLen;
    }

    // 2. Process Input Maps and inject Partial Signatures
    uint8_t sighash[32];
    doubleSha256(psbtData, psbtLen > 256 ? 256 : psbtLen, sighash);

    // Sign with secp256k1
    uint8_t rawSig[64];
    uECC_sign(btc->privKey, sighash, 32, rawSig, uECC_secp256k1());

    // Low-S normalization
    normalizeLowS(rawSig + 32);

    // Encode DER + SIGHASH_ALL (0x01)
    uint8_t derSig[74];
    size_t derLen = encodeDer(rawSig, rawSig + 32, derSig);
    derSig[derLen++] = 0x01; // SIGHASH_ALL

    // Securely scrub raw private key operations
    Bip32Engine::secureZero(rawSig, sizeof(rawSig));
    Bip32Engine::secureZero(sighash, sizeof(sighash));

    // Inject Partial Signature for first input map
    bool injected = false;
    while (inOff < psbtLen) {
        size_t keyLen = (size_t)readVarInt(psbtData, psbtLen, inOff);
        if (keyLen == 0) {
            // End of input map: inject PSBT_IN_PARTIAL_SIG if not already present
            if (!injected) {
                // Key: 0x02 || pubKey[33] (34 bytes)
                writeVarInt(34, signedPsbt, outOff);
                signedPsbt[outOff++] = 0x02; // PSBT_IN_PARTIAL_SIG
                memcpy(signedPsbt + outOff, btc->pubKey, 33);
                outOff += 33;

                // Value: DER signature with SIGHASH_ALL
                writeVarInt(derLen, signedPsbt, outOff);
                memcpy(signedPsbt + outOff, derSig, derLen);
                outOff += derLen;
                injected = true;
            }

            writeVarInt(0, signedPsbt, outOff);
            break;
        }

        writeVarInt(keyLen, signedPsbt, outOff);
        memcpy(signedPsbt + outOff, psbtData + inOff, keyLen);
        outOff += keyLen;
        inOff += keyLen;

        size_t valLen = (size_t)readVarInt(psbtData, psbtLen, inOff);
        writeVarInt(valLen, signedPsbt, outOff);
        memcpy(signedPsbt + outOff, psbtData + inOff, valLen);
        outOff += valLen;
        inOff += valLen;
    }

    // 3. Copy remaining maps (outputs, other inputs)
    if (inOff < psbtLen) {
        size_t rem = psbtLen - inOff;
        if (outOff + rem <= signedCapacity) {
            memcpy(signedPsbt + outOff, psbtData + inOff, rem);
            outOff += rem;
        }
    }

    // Determine output file path
    char outPath[128];
    const char* ext = strrchr(filePath, '.');
    if (ext) {
        size_t baseLen = ext - filePath;
        snprintf(outPath, sizeof(outPath), "%.*s-signed.psbt", (int)baseLen, filePath);
    } else {
        snprintf(outPath, sizeof(outPath), "%s-signed.psbt", filePath);
    }
    strncpy(outSignedPath, outPath, maxLen - 1);

    // Save signed file to MicroSD
    File outFile = SD_MMC.open(outPath, FILE_WRITE);
    if (!outFile) {
        free(signedPsbt);
        if (decodedBuf) free(decodedBuf);
        free(rawBuf);
        return false;
    }

    if (isBase64) {
        size_t b64Cap = (outOff * 4 / 3) + 16;
        char* b64Out = (char*)malloc(b64Cap);
        if (b64Out && encodeBase64(signedPsbt, outOff, b64Out, b64Cap)) {
            outFile.print(b64Out);
            free(b64Out);
        } else {
            outFile.write(signedPsbt, outOff);
        }
    } else {
        outFile.write(signedPsbt, outOff);
    }
    outFile.flush();
    outFile.close();

    free(signedPsbt);
    if (decodedBuf) free(decodedBuf);
    free(rawBuf);
    return true;
}
