/**
 * sd_vault.cpp — Hardware-Bound Encrypted MicroSD Vault Engine (AES-256-GCM)
 * ==============================================================================
 * Security Model:
 *  - PBKDF2-HMAC-SHA256 (10,000 iterations) binds Master PIN + ESP32-S3 MAC root.
 *  - AES-256-GCM authenticated encryption with 96-bit random hardware IV per file.
 *  - Additional Authenticated Data (AAD) binds container magic "TKEY_ENC".
 *  - 128-bit GCM authentication tag verifies 100% cryptographic integrity.
 *  - Instant volatile SRAM zeroization on lock/completion.
 */

#include "sd_vault.h"
#include "nvs_backup.h"
#include "crypto_p256.h"
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>
#include <esp_mac.h>

SdVaultEngine sdVault;

SdVaultEngine::SdVaultEngine() : _mounted(false) {}

void SdVaultEngine::secureZero(void* ptr, size_t len) {
    if (!ptr) return;
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    while (len--) *p++ = 0;
}

bool SdVaultEngine::begin() {
    if (_mounted) return true;

    // Check if SD_MMC is already active
    if (SD_MMC.cardType() != CARD_NONE) {
        _mounted = true;
        if (!SD_MMC.exists(SD_VAULT_DIR)) {
            SD_MMC.mkdir(SD_VAULT_DIR);
        }
        return true;
    }

    // Configure official LilyGO T-Dongle S3 SDIO pins (CLK=12, CMD=16, D0=14, D1=17, D2=21, D3=18)
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    // Try 4-bit bus mode first (official LilyGO standard)
    if (!SD_MMC.begin("/sdcard", false)) {
        // Fallback to 1-bit bus mode
        SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
        if (!SD_MMC.begin("/sdcard", true)) {
            if (SD_MMC.cardType() != CARD_NONE) {
                _mounted = true;
            } else {
                _mounted = false;
                return false;
            }
        }
    }

    if (SD_MMC.cardType() == CARD_NONE) {
        _mounted = false;
        return false;
    }

    _mounted = true;

    // Ensure /vault directory exists
    if (!SD_MMC.exists(SD_VAULT_DIR)) {
        SD_MMC.mkdir(SD_VAULT_DIR);
    }

    return true;
}

void SdVaultEngine::end() {
    if (_mounted) {
        SD_MMC.end();
        _mounted = false;
    }
}

bool SdVaultEngine::isMounted() const {
    return _mounted;
}

uint64_t SdVaultEngine::getCardSizeMB() const {
    if (!_mounted) return 0;
    return SD_MMC.cardSize() / (1024 * 1024);
}

uint64_t SdVaultEngine::getUsedBytes() const {
    if (!_mounted) return 0;
    return SD_MMC.usedBytes();
}

bool SdVaultEngine::deriveVaultKey(const char* pin, uint8_t* keyOut32) {
    if (!pin || strlen(pin) == 0 || !keyOut32) return false;

    // Salt: Device-Unique MAC Address (6 bytes) + AAGUID (16 bytes) = 22 bytes
    uint8_t salt[32] = {0};
    esp_read_mac(salt, ESP_MAC_WIFI_STA);

    static const uint8_t TKEY_SALT_PREFIX[16] = {
        0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x40, 0x00,
        0x80, 0x00, 0x74, 0x6b, 0x65, 0x79, 0x73, 0x33
    };
    memcpy(salt + 6, TKEY_SALT_PREFIX, 16);

    // PBKDF2-HMAC-SHA256 with 10,000 iterations
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        (const unsigned char*)pin,
        strlen(pin),
        salt,
        22,
        10000,
        32,
        keyOut32
    );

    secureZero(salt, sizeof(salt));
    return (ret == 0);
}

bool SdVaultEngine::writeEncryptedFile(const char* path, const uint8_t* plaintext, size_t len, const char* pin) {
    if (!begin() || !path || !plaintext || len == 0 || !pin) return false;

    // 1. Derive 256-bit AES vault key
    uint8_t vaultKey[32];
    if (!deriveVaultKey(pin, vaultKey)) return false;

    // 2. Generate cryptographically random 12-byte IV
    SdVaultHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, SD_VAULT_MAGIC, SD_VAULT_MAGIC_LEN);
    hdr.version = SD_VAULT_VERSION;
    hdr.flags = 0x0000;
    hdr.payloadLen = (uint32_t)len;
    CryptoP256::secureRandom(hdr.iv, SD_VAULT_IV_LEN);

    // 3. Encrypt payload with AES-256-GCM
    uint8_t* ciphertext = (uint8_t*)malloc(len);
    if (!ciphertext) {
        secureZero(vaultKey, sizeof(vaultKey));
        return false;
    }

    uint8_t tag[SD_VAULT_TAG_LEN];
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, vaultKey, 256);

    // Bind magic header as Additional Authenticated Data (AAD)
    int ret = mbedtls_gcm_crypt_and_tag(
        &gcm,
        MBEDTLS_GCM_ENCRYPT,
        len,
        hdr.iv,
        SD_VAULT_IV_LEN,
        (const unsigned char*)SD_VAULT_MAGIC,
        SD_VAULT_MAGIC_LEN,
        plaintext,
        ciphertext,
        SD_VAULT_TAG_LEN,
        tag
    );
    mbedtls_gcm_free(&gcm);
    secureZero(vaultKey, sizeof(vaultKey));

    if (ret != 0) {
        free(ciphertext);
        return false;
    }

    // 4. Write binary container: [Header 28B][Ciphertext N][Tag 16B]
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) {
        free(ciphertext);
        return false;
    }

    f.write((const uint8_t*)&hdr, sizeof(hdr));
    f.write(ciphertext, len);
    f.write(tag, SD_VAULT_TAG_LEN);
    f.flush();
    f.close();

    free(ciphertext);
    return true;
}

bool SdVaultEngine::readDecryptedFile(const char* path, uint8_t* outBuf, size_t maxLen, size_t* outLen, const char* pin) {
    if (!begin() || !path || !outBuf || !outLen || !pin) return false;

    File f = SD_MMC.open(path, FILE_READ);
    if (!f) return false;

    // 1. Read and validate container header
    SdVaultHeader hdr;
    if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        return false;
    }

    if (memcmp(hdr.magic, SD_VAULT_MAGIC, SD_VAULT_MAGIC_LEN) != 0) {
        f.close();
        return false; // Not a valid TKEY encrypted vault file
    }

    if (hdr.payloadLen > maxLen) {
        f.close();
        return false; // Buffer too small
    }

    // 2. Read ciphertext
    uint8_t* ciphertext = (uint8_t*)malloc(hdr.payloadLen);
    if (!ciphertext) {
        f.close();
        return false;
    }

    if (f.read(ciphertext, hdr.payloadLen) != hdr.payloadLen) {
        free(ciphertext);
        f.close();
        return false;
    }

    // 3. Read authentication tag
    uint8_t tag[SD_VAULT_TAG_LEN];
    if (f.read(tag, SD_VAULT_TAG_LEN) != SD_VAULT_TAG_LEN) {
        free(ciphertext);
        f.close();
        return false;
    }
    f.close();

    // 4. Derive key and authenticate/decrypt
    uint8_t vaultKey[32];
    if (!deriveVaultKey(pin, vaultKey)) {
        free(ciphertext);
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, vaultKey, 256);

    int ret = mbedtls_gcm_auth_decrypt(
        &gcm,
        hdr.payloadLen,
        hdr.iv,
        SD_VAULT_IV_LEN,
        (const unsigned char*)SD_VAULT_MAGIC,
        SD_VAULT_MAGIC_LEN,
        tag,
        SD_VAULT_TAG_LEN,
        ciphertext,
        outBuf
    );
    mbedtls_gcm_free(&gcm);
    secureZero(vaultKey, sizeof(vaultKey));
    free(ciphertext);

    if (ret != 0) {
        // Tag mismatch! Either wrong PIN, wrong physical device, or file was tampered with
        secureZero(outBuf, hdr.payloadLen);
        *outLen = 0;
        return false;
    }

    *outLen = hdr.payloadLen;
    return true;
}

bool SdVaultEngine::backupSeedV2(const char* mnemonic, const char* passphrase) {
    if (!mnemonic || strlen(mnemonic) == 0) return false;
    if (!passphrase || strlen(passphrase) < SD_VAULT_MIN_PASSPHRASE_LEN) return false;

    // Validate printable ASCII (32..126)
    for (const char* p = passphrase; *p; p++) {
        if ((unsigned char)*p < 32 || (unsigned char)*p > 126) return false;
    }

    if (!begin()) return false;

    SdVaultHeaderV2 hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, SD_VAULT_MAGIC, SD_VAULT_MAGIC_LEN);
    hdr.version = SD_VAULT_VERSION_V2;
    hdr.iterations = SD_VAULT_DEFAULT_ITERS;
    CryptoP256::secureRandom(hdr.salt, SD_VAULT_SALT_LEN);
    CryptoP256::secureRandom(hdr.iv, SD_VAULT_IV_LEN);
    size_t plainLen = strlen(mnemonic);
    hdr.payloadLen = (uint32_t)plainLen;

    // Derive 256-bit AES key from passphrase + random salt (device-independent)
    uint8_t key[32];
    int kdfRet = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        (const unsigned char*)passphrase,
        strlen(passphrase),
        hdr.salt,
        SD_VAULT_SALT_LEN,
        hdr.iterations,
        32,
        key
    );
    if (kdfRet != 0) {
        secureZero(key, sizeof(key));
        return false;
    }

    uint8_t* ciphertext = (uint8_t*)malloc(plainLen);
    if (!ciphertext) {
        secureZero(key, sizeof(key));
        return false;
    }

    uint8_t tag[SD_VAULT_TAG_LEN];
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);

    // Entire header is authenticated Additional Authenticated Data (AAD)
    int ret = mbedtls_gcm_crypt_and_tag(
        &gcm,
        MBEDTLS_GCM_ENCRYPT,
        plainLen,
        hdr.iv,
        SD_VAULT_IV_LEN,
        (const unsigned char*)&hdr,
        sizeof(hdr),
        (const unsigned char*)mnemonic,
        ciphertext,
        SD_VAULT_TAG_LEN,
        tag
    );
    mbedtls_gcm_free(&gcm);
    secureZero(key, sizeof(key));

    if (ret != 0) {
        free(ciphertext);
        return false;
    }

    File f = SD_MMC.open(SD_VAULT_TMP_FILE, FILE_WRITE);
    if (!f) {
        free(ciphertext);
        return false;
    }

    size_t w1 = f.write((const uint8_t*)&hdr, sizeof(hdr));
    size_t w2 = f.write(ciphertext, plainLen);
    size_t w3 = f.write(tag, SD_VAULT_TAG_LEN);
    f.flush();
    f.close();
    free(ciphertext);

    if (w1 != sizeof(hdr) || w2 != plainLen || w3 != SD_VAULT_TAG_LEN) {
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }

    // Re-open and decrypt-verify tmp file with the same passphrase before replacing live backup
    File vf = SD_MMC.open(SD_VAULT_TMP_FILE, FILE_READ);
    if (!vf) {
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }
    size_t vfSize = vf.size();
    const size_t expectedV2Size = sizeof(SdVaultHeaderV2) + plainLen + SD_VAULT_TAG_LEN;
    if (vfSize != expectedV2Size) {
        vf.close();
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }

    SdVaultHeaderV2 checkHdr;
    if (vf.read((uint8_t*)&checkHdr, sizeof(checkHdr)) != sizeof(checkHdr) ||
        memcmp(&checkHdr, &hdr, sizeof(hdr)) != 0) {
        vf.close();
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }

    uint8_t* checkCt = (uint8_t*)malloc(plainLen);
    if (!checkCt) {
        vf.close();
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }
    if (vf.read(checkCt, plainLen) != plainLen) {
        free(checkCt);
        vf.close();
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }

    uint8_t checkTag[SD_VAULT_TAG_LEN];
    if (vf.read(checkTag, SD_VAULT_TAG_LEN) != SD_VAULT_TAG_LEN) {
        free(checkCt);
        vf.close();
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }
    vf.close();

    // Verify decryption
    uint8_t verifyKey[32];
    int vKdf = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256,
        (const unsigned char*)passphrase,
        strlen(passphrase),
        checkHdr.salt,
        SD_VAULT_SALT_LEN,
        checkHdr.iterations,
        32,
        verifyKey
    );
    if (vKdf != 0) {
        secureZero(verifyKey, sizeof(verifyKey));
        free(checkCt);
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }

    mbedtls_gcm_context vGcm;
    mbedtls_gcm_init(&vGcm);
    mbedtls_gcm_setkey(&vGcm, MBEDTLS_CIPHER_ID_AES, verifyKey, 256);
    char* checkPlain = (char*)malloc(plainLen + 1);
    if (!checkPlain) {
        mbedtls_gcm_free(&vGcm);
        secureZero(verifyKey, sizeof(verifyKey));
        free(checkCt);
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }

    int vDecrypt = mbedtls_gcm_auth_decrypt(
        &vGcm,
        plainLen,
        checkHdr.iv,
        SD_VAULT_IV_LEN,
        (const unsigned char*)&checkHdr,
        sizeof(checkHdr),
        checkTag,
        SD_VAULT_TAG_LEN,
        checkCt,
        (uint8_t*)checkPlain
    );
    mbedtls_gcm_free(&vGcm);
    secureZero(verifyKey, sizeof(verifyKey));
    free(checkCt);

    if (vDecrypt != 0) {
        secureZero(checkPlain, plainLen + 1);
        free(checkPlain);
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }
    checkPlain[plainLen] = '\0';
    bool matches = (strcmp(checkPlain, mnemonic) == 0);
    secureZero(checkPlain, plainLen + 1);
    free(checkPlain);

    if (!matches) {
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }

    // Replace the live backup without ever having zero valid copies on the card:
    // live -> .old, tmp -> live, then drop .old. Any failure puts the old backup back.
    SD_MMC.remove(SD_VAULT_OLD_FILE);   // leftover from an interrupted earlier run
    bool hadOld = SD_MMC.exists(SD_VAULT_SEED_FILE);
    if (hadOld && !SD_MMC.rename(SD_VAULT_SEED_FILE, SD_VAULT_OLD_FILE)) {
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;                   // old backup untouched
    }
    if (!SD_MMC.rename(SD_VAULT_TMP_FILE, SD_VAULT_SEED_FILE)) {
        if (hadOld) SD_MMC.rename(SD_VAULT_OLD_FILE, SD_VAULT_SEED_FILE);
        SD_MMC.remove(SD_VAULT_TMP_FILE);
        return false;
    }
    if (hadOld) SD_MMC.remove(SD_VAULT_OLD_FILE);
    return true;
}

bool SdVaultEngine::restoreSeed(char* mnemonicOut, size_t maxLen, const char* secret) {
    if (!mnemonicOut || maxLen == 0 || !secret) return false;
    mnemonicOut[0] = '\0';

    if (!begin()) return false;
    if (!SD_MMC.exists(SD_VAULT_SEED_FILE)) return false;

    File f = SD_MMC.open(SD_VAULT_SEED_FILE, FILE_READ);
    if (!f) return false;
    size_t fileSize = f.size();

    if (fileSize < sizeof(SdVaultHeader) + SD_VAULT_TAG_LEN) {
        f.close();
        return false;
    }

    char magic[SD_VAULT_MAGIC_LEN];
    uint16_t version = 0;
    if (f.read((uint8_t*)magic, sizeof(magic)) != sizeof(magic)) {
        f.close();
        return false;
    }
    if (memcmp(magic, SD_VAULT_MAGIC, SD_VAULT_MAGIC_LEN) != 0) {
        f.close();
        return false;
    }
    if (f.read((uint8_t*)&version, sizeof(version)) != sizeof(version)) {
        f.close();
        return false;
    }

    if (version == SD_VAULT_VERSION_V2) {
        if (fileSize < sizeof(SdVaultHeaderV2) + SD_VAULT_TAG_LEN) {
            f.close();
            return false;
        }

        f.seek(0);
        SdVaultHeaderV2 hdr;
        if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
            f.close();
            return false;
        }

        // Validate iteration bounds (1000..400000)
        if (hdr.iterations < SD_VAULT_MIN_ITERS || hdr.iterations > SD_VAULT_MAX_ITERS) {
            f.close();
            return false;
        }

        // Check payloadLen against maxLen and sane cap (512) BEFORE computing expected size
        if (hdr.payloadLen == 0 || hdr.payloadLen > 512 || hdr.payloadLen >= maxLen) {
            f.close();
            return false;
        }

        // Validate exact file size using subtraction to prevent integer overflow
        const size_t v2Overhead = sizeof(SdVaultHeaderV2) + SD_VAULT_TAG_LEN;
        if (fileSize < v2Overhead || fileSize - v2Overhead != hdr.payloadLen) {
            f.close();
            return false;
        }

        uint8_t* ciphertext = (uint8_t*)malloc(hdr.payloadLen);
        if (!ciphertext) {
            f.close();
            return false;
        }

        if (f.read(ciphertext, hdr.payloadLen) != hdr.payloadLen) {
            free(ciphertext);
            f.close();
            return false;
        }

        uint8_t tag[SD_VAULT_TAG_LEN];
        if (f.read(tag, SD_VAULT_TAG_LEN) != SD_VAULT_TAG_LEN) {
            free(ciphertext);
            f.close();
            return false;
        }
        f.close();

        // Key derivation using passphrase (secret)
        uint8_t key[32];
        int kdfRet = mbedtls_pkcs5_pbkdf2_hmac_ext(
            MBEDTLS_MD_SHA256,
            (const unsigned char*)secret,
            strlen(secret),
            hdr.salt,
            SD_VAULT_SALT_LEN,
            hdr.iterations,
            32,
            key
        );
        if (kdfRet != 0) {
            secureZero(key, sizeof(key));
            free(ciphertext);
            return false;
        }

        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);

        // Entire header authenticated as AAD
        int ret = mbedtls_gcm_auth_decrypt(
            &gcm,
            hdr.payloadLen,
            hdr.iv,
            SD_VAULT_IV_LEN,
            (const unsigned char*)&hdr,
            sizeof(hdr),
            tag,
            SD_VAULT_TAG_LEN,
            ciphertext,
            (uint8_t*)mnemonicOut
        );
        mbedtls_gcm_free(&gcm);
        secureZero(key, sizeof(key));
        free(ciphertext);

        if (ret != 0) {
            secureZero(mnemonicOut, maxLen);
            return false;
        }

        mnemonicOut[hdr.payloadLen] = '\0';
        return true;
    } else if (version == SD_VAULT_VERSION_V1) {
        // Legacy v1 container
        f.seek(0);
        SdVaultHeader hdr;
        if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
            f.close();
            return false;
        }

        // Check payloadLen against maxLen and sane cap (512) BEFORE computing expected size
        if (hdr.payloadLen == 0 || hdr.payloadLen > 512 || hdr.payloadLen >= maxLen) {
            f.close();
            return false;
        }

        // Validate exact file size using subtraction to prevent integer overflow
        const size_t v1Overhead = sizeof(SdVaultHeader) + SD_VAULT_TAG_LEN;
        if (fileSize < v1Overhead || fileSize - v1Overhead != hdr.payloadLen) {
            f.close();
            return false;
        }

        uint8_t* ciphertext = (uint8_t*)malloc(hdr.payloadLen);
        if (!ciphertext) {
            f.close();
            return false;
        }

        if (f.read(ciphertext, hdr.payloadLen) != hdr.payloadLen) {
            free(ciphertext);
            f.close();
            return false;
        }

        uint8_t tag[SD_VAULT_TAG_LEN];
        if (f.read(tag, SD_VAULT_TAG_LEN) != SD_VAULT_TAG_LEN) {
            free(ciphertext);
            f.close();
            return false;
        }
        f.close();

        // Legacy key derivation using secret as PIN (device MAC bound)
        uint8_t key[32];
        if (!deriveVaultKey(secret, key)) {
            free(ciphertext);
            return false;
        }

        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);

        int ret = mbedtls_gcm_auth_decrypt(
            &gcm,
            hdr.payloadLen,
            hdr.iv,
            SD_VAULT_IV_LEN,
            (const unsigned char*)SD_VAULT_MAGIC,
            SD_VAULT_MAGIC_LEN,
            tag,
            SD_VAULT_TAG_LEN,
            ciphertext,
            (uint8_t*)mnemonicOut
        );
        mbedtls_gcm_free(&gcm);
        secureZero(key, sizeof(key));
        free(ciphertext);

        if (ret != 0) {
            secureZero(mnemonicOut, maxLen);
            return false;
        }

        mnemonicOut[hdr.payloadLen] = '\0';
        return true;
    }

    f.close();
    return false;
}

bool SdVaultEngine::hasSeedBackup() {
    if (!begin()) return false;
    return SD_MMC.exists(SD_VAULT_SEED_FILE);
}

struct PasskeyRecord {
    uint8_t credIdLen;
    uint8_t credId[64];
    char rpId[64];
    char userName[64];
    uint8_t privKey[32];
};

bool SdVaultEngine::saveResidentPasskey(const uint8_t* credId, size_t credIdLen, const char* rpId, const char* userName, const uint8_t* privKey32, const char* pin) {
    if (!credId || credIdLen == 0 || credIdLen > 64 || !rpId || !privKey32 || !pin) return false;

    // Load existing records if any
    PasskeyRecord records[16];
    size_t count = 0;

    if (SD_MMC.exists(SD_VAULT_PASSKEY_FILE)) {
        size_t loadedLen = 0;
        if (readDecryptedFile(SD_VAULT_PASSKEY_FILE, (uint8_t*)records, sizeof(records), &loadedLen, pin)) {
            count = loadedLen / sizeof(PasskeyRecord);
        }
    }

    if (count >= 16) return false; // Vault full for this partition block

    PasskeyRecord* rec = &records[count];
    memset(rec, 0, sizeof(PasskeyRecord));
    rec->credIdLen = (uint8_t)credIdLen;
    memcpy(rec->credId, credId, credIdLen);
    strncpy(rec->rpId, rpId, sizeof(rec->rpId) - 1);
    if (userName) strncpy(rec->userName, userName, sizeof(rec->userName) - 1);
    memcpy(rec->privKey, privKey32, 32);
    count++;

    bool ok = writeEncryptedFile(
        SD_VAULT_PASSKEY_FILE,
        (const uint8_t*)records,
        count * sizeof(PasskeyRecord),
        pin
    );

    secureZero(records, sizeof(records));
    return ok;
}

bool SdVaultEngine::findResidentPasskey(const char* rpId, uint8_t* credIdOut, size_t* credIdLenOut, uint8_t* privKeyOut32, const char* pin) {
    if (!rpId || !credIdOut || !credIdLenOut || !privKeyOut32 || !pin) return false;
    if (!SD_MMC.exists(SD_VAULT_PASSKEY_FILE)) return false;

    PasskeyRecord records[16];
    size_t loadedLen = 0;
    if (!readDecryptedFile(SD_VAULT_PASSKEY_FILE, (uint8_t*)records, sizeof(records), &loadedLen, pin)) {
        return false;
    }

    size_t count = loadedLen / sizeof(PasskeyRecord);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(records[i].rpId, rpId) == 0) {
            *credIdLenOut = records[i].credIdLen;
            memcpy(credIdOut, records[i].credId, records[i].credIdLen);
            memcpy(privKeyOut32, records[i].privKey, 32);
            secureZero(records, sizeof(records));
            return true;
        }
    }

    secureZero(records, sizeof(records));
    return false;
}

size_t SdVaultEngine::getResidentPasskeyCount(const char* pin) {
    if (!begin() || !pin || !SD_MMC.exists(SD_VAULT_PASSKEY_FILE)) return 0;

    PasskeyRecord records[16];
    size_t loadedLen = 0;
    if (!readDecryptedFile(SD_VAULT_PASSKEY_FILE, (uint8_t*)records, sizeof(records), &loadedLen, pin)) {
        return 0;
    }
    size_t count = loadedLen / sizeof(PasskeyRecord);
    secureZero(records, sizeof(records));
    return count;
}

WipeResult SdVaultEngine::wipeVault() {
    if (!begin()) return WIPE_FAILED;

    bool anyFound = false;
    bool anyFailed = false;
    const char* files[] = { SD_VAULT_SEED_FILE, SD_VAULT_PASSKEY_FILE };

    for (int i = 0; i < 2; i++) {
        if (SD_MMC.exists(files[i])) {
            anyFound = true;
            bool fileFailed = false;

            File f = SD_MMC.open(files[i], "r+");
            if (!f) {
                fileFailed = true;
            } else {
                size_t fileSize = f.size();
                uint8_t chunk[128];

                if (fileSize > 0) {
                    // Pass 1: Overwrite entire length with cryptographically secure random bytes
                    if (!f.seek(0)) {
                        fileFailed = true;
                    } else {
                        size_t remaining = fileSize;
                        while (remaining > 0) {
                            size_t toWrite = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
                            CryptoP256::secureRandom(chunk, toWrite);
                            if (f.write(chunk, toWrite) != toWrite) {
                                fileFailed = true;
                                break;
                            }
                            remaining -= toWrite;
                        }
                        f.flush();
                    }

                    // Pass 2: Overwrite entire length with zeros
                    if (!f.seek(0)) {
                        fileFailed = true;
                    } else {
                        memset(chunk, 0, sizeof(chunk));
                        size_t remaining = fileSize;
                        while (remaining > 0) {
                            size_t toWrite = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
                            if (f.write(chunk, toWrite) != toWrite) {
                                fileFailed = true;
                                break;
                            }
                            remaining -= toWrite;
                        }
                        f.flush();
                    }
                }
                f.close();
            }

            // ALWAYS attempt remove(), even if open or overwrite failed
            if (!SD_MMC.remove(files[i])) {
                fileFailed = true;
            }

            if (fileFailed) {
                anyFailed = true;
            }
        }
    }

    if (!anyFound) return WIPE_NOTHING;
    if (anyFailed) return WIPE_FAILED;
    return WIPE_OK;
}

// ─── Full device-bound backup ────────────────────────────────────────────────
// Work buffers live on the heap only while a backup/restore runs: as static arrays they took
// 48 KB of RAM permanently and the key ran out of memory at boot (crash loop, 2026-09-25).
struct FullBuffers {
    uint8_t* bundle = (uint8_t*)malloc(NvsBackup::MAX_BLOB);
    uint8_t* sealed = (uint8_t*)malloc(NvsBackup::MAX_BLOB + 64);
    bool ok() const { return bundle && sealed; }
    ~FullBuffers() {
        if (bundle) { mbedtls_platform_zeroize(bundle, NvsBackup::MAX_BLOB); free(bundle); }
        if (sealed) { mbedtls_platform_zeroize(sealed, NvsBackup::MAX_BLOB + 64); free(sealed); }
    }
};

bool SdVaultEngine::hasFullBackup() {
    return begin() && SD_MMC.exists(SD_VAULT_FULL_FILE);
}

bool SdVaultEngine::backupFull(const char* setupPassword) {
    FullBuffers buf;
    if (!buf.ok()) return false;
    uint8_t* s_bundle = buf.bundle;
    uint8_t* s_sealed = buf.sealed;
    const size_t bundleCap = NvsBackup::MAX_BLOB, sealedCap = NvsBackup::MAX_BLOB + 64;
    uint8_t chip[NvsBackup::CHIP_ID_LEN];
    size_t len = 0, sealedLen = 0;
    bool ok = begin() && setupPassword && setupPassword[0] && NvsBackup::chipId(chip) &&
              NvsBackup::readNvs(NvsBackup::NAMESPACES, s_bundle, bundleCap, &len) &&
              NvsBackup::seal(s_bundle, len, setupPassword, chip, NvsBackup::DEFAULT_ITERS,
                              s_sealed, sealedCap, &sealedLen);
    mbedtls_platform_zeroize(s_bundle, bundleCap);
    if (!ok) return false;
    if (!SD_MMC.exists(SD_VAULT_DIR)) SD_MMC.mkdir(SD_VAULT_DIR);

    // Write a temp file, read it back and prove it opens, then swap it in (never zero copies)
    const char* tmp = SD_VAULT_FULL_FILE ".tmp";
    const char* old = SD_VAULT_FULL_FILE ".old";
    File f = SD_MMC.open(tmp, FILE_WRITE);
    ok = f && f.write(s_sealed, sealedLen) == sealedLen;
    if (f) f.close();
    if (ok) {
        File r = SD_MMC.open(tmp, FILE_READ);
        size_t n = r ? r.read(s_sealed, sealedCap) : 0;
        if (r) r.close();
        size_t plain = 0;
        ok = n == sealedLen && NvsBackup::open(s_sealed, n, setupPassword, chip, s_bundle, bundleCap, &plain);
        mbedtls_platform_zeroize(s_bundle, bundleCap);
    }
    if (ok) {
        SD_MMC.remove(old);
        bool hadOld = SD_MMC.exists(SD_VAULT_FULL_FILE);
        if (hadOld && !SD_MMC.rename(SD_VAULT_FULL_FILE, old)) ok = false;
        else if (!SD_MMC.rename(tmp, SD_VAULT_FULL_FILE)) {
            if (hadOld) SD_MMC.rename(old, SD_VAULT_FULL_FILE);
            ok = false;
        } else if (hadOld) {
            SD_MMC.remove(old);
        }
    }
    if (!ok) SD_MMC.remove(tmp);
    mbedtls_platform_zeroize(s_sealed, sealedCap);
    return ok;
}

bool SdVaultEngine::restoreFull(const char* setupPassword) {
    FullBuffers buf;
    if (!buf.ok()) return false;
    uint8_t* s_bundle = buf.bundle;
    uint8_t* s_sealed = buf.sealed;
    const size_t bundleCap = NvsBackup::MAX_BLOB, sealedCap = NvsBackup::MAX_BLOB + 64;
    uint8_t chip[NvsBackup::CHIP_ID_LEN];
    if (!setupPassword || !begin() || !NvsBackup::chipId(chip)) return false;
    File f = SD_MMC.open(SD_VAULT_FULL_FILE, FILE_READ);
    if (!f) return false;
    size_t sz = f.size();
    size_t n = (sz <= sealedCap) ? f.read(s_sealed, sz) : 0;
    f.close();
    size_t plain = 0;
    bool ok = n == sz && n > 0 &&
              NvsBackup::open(s_sealed, n, setupPassword, chip, s_bundle, bundleCap, &plain) &&
              NvsBackup::writeNvs(s_bundle, plain);
    mbedtls_platform_zeroize(s_bundle, bundleCap);
    mbedtls_platform_zeroize(s_sealed, sealedCap);
    return ok;
}
