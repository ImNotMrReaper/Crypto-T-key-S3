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

bool SdVaultEngine::backupSeed(const char* mnemonic, const char* pin) {
    if (!mnemonic || strlen(mnemonic) == 0 || !pin) return false;
    return writeEncryptedFile(
        SD_VAULT_SEED_FILE,
        (const uint8_t*)mnemonic,
        strlen(mnemonic),
        pin
    );
}

bool SdVaultEngine::restoreSeed(char* mnemonicOut, size_t maxLen, const char* pin) {
    if (!mnemonicOut || maxLen == 0 || !pin) return false;

    size_t outLen = 0;
    bool ok = readDecryptedFile(
        SD_VAULT_SEED_FILE,
        (uint8_t*)mnemonicOut,
        maxLen - 1,
        &outLen,
        pin
    );

    if (ok) {
        mnemonicOut[outLen] = '\0';
    } else {
        mnemonicOut[0] = '\0';
    }
    return ok;
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

bool SdVaultEngine::wipeVault() {
    if (!begin()) return false;

    bool anyWiped = false;
    const char* files[] = { SD_VAULT_SEED_FILE, SD_VAULT_PASSKEY_FILE };
    for (int i = 0; i < 2; i++) {
        if (SD_MMC.exists(files[i])) {
            // Overwrite with zeros before deletion
            File f = SD_MMC.open(files[i], FILE_WRITE);
            if (f) {
                uint8_t zeros[64] = {0};
                for (int z = 0; z < 4; z++) f.write(zeros, sizeof(zeros));
                f.flush();
                f.close();
            }
            SD_MMC.remove(files[i]);
            anyWiped = true;
        }
    }
    return anyWiped;
}
