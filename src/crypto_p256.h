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
    bool getRandomBytes(uint8_t* out, size_t len);
    bool generateKeypair(uint8_t* privKeyOut, uint8_t* pubKeyOutRaw); // pubKeyOutRaw is 64 bytes (X || Y)

    // Non-Resident Deterministic Derivation
    bool deriveCredentialKey(const char* rpId, const uint8_t* userId, size_t userLen, 
                             uint8_t* privKeyOut, uint8_t* credIdOut);
    bool verifyCredentialId(const char* rpId, const uint8_t* credId, uint8_t* privKeyOut);

    // ECDSA Signing & Verification (secp256r1)
    bool signDigest(const uint8_t* privKey, const uint8_t* digest, uint8_t* sigOutDer, size_t* sigLen);

    // Monotonic Replay Protection Counter
    uint32_t getSignatureCounter();
    uint32_t incrementSignatureCounter();

    // SHA-256 Utility
    static void sha256(const uint8_t* data, size_t len, uint8_t* digestOut);
    static void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t* outDigest);

private:
    bool loadOrGenerateMasterSecret();
    uint8_t _masterSecret[32];
    uint32_t _sigCounter;
    bool _initialized;
};

extern CryptoP256 cryptoP256;
