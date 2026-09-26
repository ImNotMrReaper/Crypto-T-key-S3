#ifndef SD_VAULT_H
#define SD_VAULT_H

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include "config.h"

#define SD_VAULT_MAGIC          "TKEY_ENC"
#define SD_VAULT_MAGIC_LEN      8
#define SD_VAULT_VERSION        0x0001
#define SD_VAULT_VERSION_V1     0x0001
#define SD_VAULT_VERSION_V2     0x0002
#define SD_VAULT_SALT_LEN       16
#define SD_VAULT_IV_LEN         12
#define SD_VAULT_TAG_LEN        16
#define SD_VAULT_DEFAULT_ITERS  100000
#define SD_VAULT_MIN_ITERS      1000
#define SD_VAULT_MAX_ITERS      400000
#define SD_VAULT_MIN_PASSPHRASE_LEN 12
#define SD_VAULT_DIR            "/vault"
#define SD_VAULT_SEED_FILE      "/vault/tkey_backup.vault"
#define SD_VAULT_TMP_FILE       "/vault/tkey_backup.tmp"
#define SD_VAULT_OLD_FILE       "/vault/tkey_backup.old"
#define SD_VAULT_FULL_FILE      "/vault/full.tkb"
#define SD_VAULT_PASSKEY_FILE   "/vault/passkeys.vault"

struct SdVaultHeader {
    char     magic[SD_VAULT_MAGIC_LEN]; // "TKEY_ENC"
    uint16_t version;                   // 0x0001
    uint16_t flags;                     // 0x0000
    uint8_t  iv[SD_VAULT_IV_LEN];       // 12 bytes IV
    uint32_t payloadLen;                // Length of unencrypted payload
};

struct __attribute__((packed)) SdVaultHeaderV2 {
    char     magic[SD_VAULT_MAGIC_LEN]; // "TKEY_ENC"
    uint16_t version;                   // 0x0002
    uint32_t iterations;                // KDF iterations e.g. 100000
    uint8_t  salt[SD_VAULT_SALT_LEN];   // 16 bytes salt
    uint8_t  iv[SD_VAULT_IV_LEN];       // 12 bytes IV
    uint32_t payloadLen;                // Length of unencrypted payload
};

enum WipeResult {
    WIPE_NOTHING = 0,
    WIPE_OK,
    WIPE_FAILED
};

class SdVaultEngine {
public:
    SdVaultEngine();

    // Hardware & Filesystem Initialization
    bool begin();
    void end();
    bool isMounted() const;
    uint64_t getCardSizeMB() const;
    uint64_t getUsedBytes() const;

    // Cryptographic Key Derivation (Hardware Silicon Bound + PIN)
    bool deriveVaultKey(const char* pin, uint8_t* keyOut32);

    // Authenticated Container File Operations (AES-256-GCM)
    bool writeEncryptedFile(const char* path, const uint8_t* plaintext, size_t len, const char* pin);
    bool readDecryptedFile(const char* path, uint8_t* outBuf, size_t maxLen, size_t* outLen, const char* pin);

    // High-Level Security Workflows
    bool backupSeedV2(const char* mnemonic, const char* passphrase);
    bool restoreSeed(char* mnemonicOut, size_t maxLen, const char* secret);
    bool hasSeedBackup();

    // Resident Passkey Storage (Unlimited Expansion)
    bool saveResidentPasskey(const uint8_t* credId, size_t credIdLen, const char* rpId, const char* userName, const uint8_t* privKey32, const char* pin);
    bool findResidentPasskey(const char* rpId, uint8_t* credIdOut, size_t* credIdLenOut, uint8_t* privKeyOut32, const char* pin);
    size_t getResidentPasskeyCount(const char* pin);

    // Full device-bound backup (nvs_backup.h): every setting and secret, opens on THIS chip only,
    // locked with the setup password. /vault/full.tkb, replaced atomically like the seed backup.
    bool backupFull(const char* setupPassword);
    bool restoreFull(const char* setupPassword);   // rewrites NVS; the caller reboots afterwards
    bool hasFullBackup();

    // Emergency Vault Scrub
    WipeResult wipeVault();

private:
    bool _mounted;
    void secureZero(void* ptr, size_t len);
};

extern SdVaultEngine sdVault;

#endif // SD_VAULT_H
