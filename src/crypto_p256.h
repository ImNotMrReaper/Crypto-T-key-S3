#pragma once
#include <Arduino.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

/**
 * NIST P-256 (secp256r1) & Cryptographic Core for Crypto TKey S3
 * Powered by ESP32-S3 Hardware-Accelerated SHA & mbedTLS
 */

#define P256_PRIVATE_KEY_LEN 32
#define P256_PUBLIC_KEY_LEN  65  // 0x04 || X (32) || Y (32)
#define P256_RAW_POINT_LEN   64  // X (32) || Y (32)
#define SHA256_DIGEST_LEN    32
#define CREDENTIAL_ID_LEN    32

class CryptoP256 {
public:
    CryptoP256();
    bool begin();
    
    // Entropy & Key Generation
    // True-random bytes: the ESP32-S3 RNG is only a PRNG while the radio is off, so
    // this switches on the SAR-ADC noise source (bootloader_random) around each draw.
    static void secureRandom(uint8_t* out, size_t len);
    bool getRandomBytes(uint8_t* out, size_t len);
    bool generateKeypair(uint8_t* privKeyOut, uint8_t* pubKeyOutRaw); // pubKeyOutRaw is 64 bytes (X || Y)

    // Non-Resident Deterministic Derivation
    bool deriveCredentialKey(const char* rpId, const uint8_t* userId, size_t userLen, 
                             uint8_t* privKeyOut, uint8_t* credIdOut);
    bool deriveCredentialKeyRaw(const uint8_t* rpHash32, uint8_t* privKeyOut, uint8_t* credIdOut);
    bool verifyCredentialId(const char* rpId, const uint8_t* credId, uint8_t* privKeyOut);
    bool verifyCredentialIdRaw(const uint8_t* rpHash32, const uint8_t* credId, uint8_t* privKeyOut);

    // ECDSA Signing & Verification (secp256r1)
    bool signDigest(const uint8_t* privKey, const uint8_t* digest, uint8_t* sigOutDer, size_t* sigLen);

    // NIST P-256 ECDH & Symmetric Encryption (PIN Protocol 1)
    bool computeSharedSecretP256(const uint8_t* privKey32, const uint8_t* peerPubKeyRaw64, uint8_t* sharedSecretOut32);
    static bool aes256CbcDecrypt(const uint8_t* key32, const uint8_t* iv16, const uint8_t* in, size_t len, uint8_t* out);
    static bool aes256CbcEncrypt(const uint8_t* key32, const uint8_t* iv16, const uint8_t* in, size_t len, uint8_t* out);

    // hmac-secret: per-credential CredRandom = HMAC(master, "hmac-secret" || credId)
    void deriveCredRandom(const uint8_t* credId, size_t credIdLen, uint8_t out32[32]);

    // authenticatorReset: replace the master secret, invalidating every credential ever issued
    bool rotateMasterSecret();

    // Per-device attestation key + self-signed X.509 certificate (packed / U2F).
    // Generated on first use from the hardware RNG and kept in NVS "fido_vault";
    // nothing about it is in the source. The cert carries the AAGUID extension.
    bool attestation(const uint8_t aaguid[16], const uint8_t** key32, const uint8_t** certDer, size_t* certLen);

    // Monotonic Replay Protection Counter
    uint32_t getSignatureCounter();
    uint32_t incrementSignatureCounter();

    // SHA-256 Utility
    static void sha256(const uint8_t* data, size_t len, uint8_t* digestOut);
    static void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t* outDigest);

private:
    bool loadOrGenerateMasterSecret();
    bool generateAttestation(const uint8_t aaguid[16]);
    uint8_t _masterSecret[32];
    uint8_t _attKey[32];
    uint8_t _attCert[640];
    size_t  _attCertLen = 0;
    uint32_t _sigCounter;
    bool _initialized;
};

extern CryptoP256 cryptoP256;
