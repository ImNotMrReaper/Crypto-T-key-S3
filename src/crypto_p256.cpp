#include "crypto_p256.h"
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <mbedtls/platform_util.h>

CryptoP256 cryptoP256;

CryptoP256::CryptoP256() : _sigCounter(0), _initialized(false) {
    mbedtls_platform_zeroize(_masterSecret, sizeof(_masterSecret));
}

bool CryptoP256::begin() {
    if (_initialized) return true;
    
    if (!loadOrGenerateMasterSecret()) {
        return false;
    }

    Preferences prefs;
    prefs.begin("fido_vault", false);
    _sigCounter = prefs.getUInt("sig_counter", 1);
    prefs.end();

    _initialized = true;
    return true;
}

bool CryptoP256::loadOrGenerateMasterSecret() {
    Preferences prefs;
    prefs.begin("fido_vault", false);
    
    if (prefs.isKey("master_sec")) {
        size_t read = prefs.getBytes("master_sec", _masterSecret, sizeof(_masterSecret));
        prefs.end();
        return (read == sizeof(_masterSecret));
    }

    // Generate fresh 256-bit master secret using ESP32-S3 TRNG
    getRandomBytes(_masterSecret, sizeof(_masterSecret));
    prefs.putBytes("master_sec", _masterSecret, sizeof(_masterSecret));
    prefs.putUInt("sig_counter", 1);
    prefs.end();
    return true;
}

bool CryptoP256::getRandomBytes(uint8_t* out, size_t len) {
    if (!out || len == 0) return false;
    esp_fill_random(out, len);
    return true;
}

void CryptoP256::sha256(const uint8_t* data, size_t len, uint8_t* digestOut) {
    mbedtls_sha256(data, len, digestOut, 0); // 0 = SHA-256
}

void CryptoP256::hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen, uint8_t* outDigest) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, data, dataLen);
    mbedtls_md_hmac_finish(&ctx, outDigest);
    mbedtls_md_free(&ctx);
}

bool CryptoP256::deriveCredentialKey(const char* rpId, const uint8_t* userId, size_t userLen, 
                                     uint8_t* privKeyOut, uint8_t* credIdOut) {
    if (!rpId || !privKeyOut || !credIdOut) return false;

    // 1. Compute RP ID hash
    uint8_t rpHash[32];
    sha256((const uint8_t*)rpId, strlen(rpId), rpHash);
    bool ok = deriveCredentialKeyRaw(rpHash, privKeyOut, credIdOut);
    mbedtls_platform_zeroize(rpHash, sizeof(rpHash));
    return ok;
}

bool CryptoP256::deriveCredentialKeyRaw(const uint8_t* rpHash32, uint8_t* privKeyOut, uint8_t* credIdOut) {
    if (!rpHash32 || !privKeyOut || !credIdOut) return false;

    // 1. Generate random credential nonce (16 bytes)
    uint8_t nonce[16];
    getRandomBytes(nonce, 16);

    // 2. Derive private key: HMAC-SHA256(MasterSecret, rpHash || nonce)
    uint8_t inputBuf[48];
    memcpy(inputBuf, rpHash32, 32);
    memcpy(inputBuf + 32, nonce, 16);
    hmacSha256(_masterSecret, 32, inputBuf, 48, privKeyOut);

    // 3. Create Credential ID: nonce (16 bytes) || HMAC-SHA256(MasterSecret, privKeyOut)[0..15]
    uint8_t mac[32];
    hmacSha256(_masterSecret, 32, privKeyOut, 32, mac);
    
    memcpy(credIdOut, nonce, 16);
    memcpy(credIdOut + 16, mac, 16);

    // Secure zero ephemeral stack secrets
    mbedtls_platform_zeroize(inputBuf, sizeof(inputBuf));
    mbedtls_platform_zeroize(mac, sizeof(mac));
    return true;
}

bool CryptoP256::verifyCredentialId(const char* rpId, const uint8_t* credId, uint8_t* privKeyOut) {
    if (!rpId || !credId || !privKeyOut) return false;

    uint8_t rpHash[32];
    sha256((const uint8_t*)rpId, strlen(rpId), rpHash);
    bool ok = verifyCredentialIdRaw(rpHash, credId, privKeyOut);
    mbedtls_platform_zeroize(rpHash, sizeof(rpHash));
    return ok;
}

bool CryptoP256::verifyCredentialIdRaw(const uint8_t* rpHash32, const uint8_t* credId, uint8_t* privKeyOut) {
    if (!rpHash32 || !credId || !privKeyOut) return false;

    const uint8_t* nonce = credId;
    const uint8_t* expectedMac = credId + 16;

    // Re-derive candidate private key
    uint8_t inputBuf[48];
    memcpy(inputBuf, rpHash32, 32);
    memcpy(inputBuf + 32, nonce, 16);
    hmacSha256(_masterSecret, 32, inputBuf, 48, privKeyOut);

    // Verify MAC
    uint8_t mac[32];
    hmacSha256(_masterSecret, 32, privKeyOut, 32, mac);

    // Constant-time compare
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) {
        diff |= (mac[i] ^ expectedMac[i]);
    }

    mbedtls_platform_zeroize(inputBuf, sizeof(inputBuf));
    mbedtls_platform_zeroize(mac, sizeof(mac));

    if (diff != 0) {
        mbedtls_platform_zeroize(privKeyOut, 32);
        return false;
    }
    return true;
}

static int f_rng(void* p_rng, unsigned char* output, size_t output_len) {
    esp_fill_random(output, output_len);
    return 0;
}

bool CryptoP256::generateKeypair(uint8_t* privKeyOut, uint8_t* pubKeyOutRaw) {
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d);

    mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&d, privKeyOut, 32);
    mbedtls_mpi_mod_mpi(&d, &d, &grp.N);
    if (mbedtls_mpi_cmp_int(&d, 0) == 0) {
        mbedtls_mpi_lset(&d, 1);
    }

    int ret = mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, f_rng, NULL);
    if (ret != 0) {
        mbedtls_ecp_group_free(&grp);
        mbedtls_ecp_point_free(&Q);
        mbedtls_mpi_free(&d);
        return false;
    }

    // Export raw uncompressed X (32) and Y (32) coordinates (skip leading 0x04 format byte)
    uint8_t uncompressed[65];
    size_t olen = 0;
    ret = mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, uncompressed, sizeof(uncompressed));
    if (ret != 0 || olen != 65) {
        mbedtls_ecp_group_free(&grp);
        mbedtls_ecp_point_free(&Q);
        mbedtls_mpi_free(&d);
        return false;
    }

    memcpy(pubKeyOutRaw, uncompressed + 1, 64);

    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    return true;
}

bool CryptoP256::signDigest(const uint8_t* privKey, const uint8_t* digest, uint8_t* sigOutDer, size_t* sigLen) {
    mbedtls_mpi r, s, d;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_init(&d);

    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);

    mbedtls_mpi_read_binary(&d, privKey, 32);

    int ret = mbedtls_ecdsa_sign(&grp, &r, &s, &d, digest, 32, f_rng, NULL);
    if (ret != 0) {
        mbedtls_mpi_free(&r);
        mbedtls_mpi_free(&s);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&grp);
        return false;
    }

    // Low-S normalization (RFC canonical non-malleability: if s > N/2, then s = N - s)
    mbedtls_mpi halfN;
    mbedtls_mpi_init(&halfN);
    if (mbedtls_mpi_copy(&halfN, &grp.N) == 0 && mbedtls_mpi_shift_r(&halfN, 1) == 0) {
        if (mbedtls_mpi_cmp_mpi(&s, &halfN) > 0) {
            mbedtls_mpi_sub_mpi(&s, &grp.N, &s);
        }
    }
    mbedtls_mpi_free(&halfN);

    // Export raw r and s integers
    uint8_t rRaw[32], sRaw[32];
    mbedtls_mpi_write_binary(&r, rRaw, 32);
    mbedtls_mpi_write_binary(&s, sRaw, 32);

    // Canonical ASN.1 DER integer normalization:
    // 1. Strip leading zeros
    // 2. Prepend 0x00 if MSB is set
    size_t rOffset = 0;
    while (rOffset < 31 && rRaw[rOffset] == 0) rOffset++;
    size_t rLen = 32 - rOffset;
    bool rPad = (rRaw[rOffset] & 0x80) != 0;
    size_t rEncLen = rLen + (rPad ? 1 : 0);

    size_t sOffset = 0;
    while (sOffset < 31 && sRaw[sOffset] == 0) sOffset++;
    size_t sLen = 32 - sOffset;
    bool sPad = (sRaw[sOffset] & 0x80) != 0;
    size_t sEncLen = sLen + (sPad ? 1 : 0);

    size_t totalLen = 2 + rEncLen + 2 + sEncLen;

    uint8_t* p = sigOutDer;
    *p++ = 0x30; // SEQUENCE
    *p++ = (uint8_t)totalLen;

    // R
    *p++ = 0x02; // INTEGER
    *p++ = (uint8_t)rEncLen;
    if (rPad) *p++ = 0x00;
    memcpy(p, rRaw + rOffset, rLen);
    p += rLen;

    // S
    *p++ = 0x02; // INTEGER
    *p++ = (uint8_t)sEncLen;
    if (sPad) *p++ = 0x00;
    memcpy(p, sRaw + sOffset, sLen);
    p += sLen;

    *sigLen = (p - sigOutDer);

    // Secure zeroize stack buffers and free MPIs
    mbedtls_platform_zeroize(rRaw, sizeof(rRaw));
    mbedtls_platform_zeroize(sRaw, sizeof(sRaw));
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return true;
}

bool CryptoP256::computeSharedSecretP256(const uint8_t* privKey32, const uint8_t* peerPubKeyRaw64, uint8_t* sharedSecretOut32) {
    if (!privKey32 || !peerPubKeyRaw64 || !sharedSecretOut32) return false;

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Qpeer, P;
    mbedtls_mpi d;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Qpeer);
    mbedtls_ecp_point_init(&P);
    mbedtls_mpi_init(&d);

    mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi_read_binary(&d, privKey32, 32);
    mbedtls_mpi_mod_mpi(&d, &d, &grp.N);
    if (mbedtls_mpi_cmp_int(&d, 0) == 0) {
        mbedtls_mpi_lset(&d, 1);
    }

    uint8_t uncompressed[65];
    uncompressed[0] = 0x04;
    memcpy(uncompressed + 1, peerPubKeyRaw64, 64);
    int ret = mbedtls_ecp_point_read_binary(&grp, &Qpeer, uncompressed, 65);
    if (ret != 0) {
        mbedtls_ecp_group_free(&grp);
        mbedtls_ecp_point_free(&Qpeer);
        mbedtls_ecp_point_free(&P);
        mbedtls_mpi_free(&d);
        return false;
    }

    ret = mbedtls_ecp_mul(&grp, &P, &d, &Qpeer, f_rng, NULL);
    if (ret != 0 || mbedtls_ecp_is_zero(&P)) {
        mbedtls_ecp_group_free(&grp);
        mbedtls_ecp_point_free(&Qpeer);
        mbedtls_ecp_point_free(&P);
        mbedtls_mpi_free(&d);
        return false;
    }

    // Export X coordinate of shared point P (32 bytes)
    uint8_t sharedPoint[65];
    size_t olen = 0;
    ret = mbedtls_ecp_point_write_binary(&grp, &P, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, sharedPoint, sizeof(sharedPoint));
    if (ret != 0 || olen != 65) {
        mbedtls_ecp_group_free(&grp);
        mbedtls_ecp_point_free(&Qpeer);
        mbedtls_ecp_point_free(&P);
        mbedtls_mpi_free(&d);
        return false;
    }

    // Compute sharedKey = SHA-256(Z) as mandated by CTAP2 PIN Protocol 1
    sha256(sharedPoint + 1, 32, sharedSecretOut32);

    mbedtls_platform_zeroize(sharedPoint, sizeof(sharedPoint));
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Qpeer);
    mbedtls_ecp_point_free(&P);
    mbedtls_mpi_free(&d);
    return true;
}

bool CryptoP256::aes256CbcDecrypt(const uint8_t* key32, const uint8_t* iv16, const uint8_t* in, size_t len, uint8_t* out) {
    if (!key32 || !in || !out || len % 16 != 0) return false;

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int ret = mbedtls_aes_setkey_dec(&ctx, key32, 256);
    if (ret != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    uint8_t ivCopy[16];
    if (iv16) {
        memcpy(ivCopy, iv16, 16);
    } else {
        memset(ivCopy, 0, 16);
    }

    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, len, ivCopy, in, out);
    mbedtls_aes_free(&ctx);
    return (ret == 0);
}

bool CryptoP256::aes256CbcEncrypt(const uint8_t* key32, const uint8_t* iv16, const uint8_t* in, size_t len, uint8_t* out) {
    if (!key32 || !in || !out || len % 16 != 0) return false;

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int ret = mbedtls_aes_setkey_enc(&ctx, key32, 256);
    if (ret != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    uint8_t ivCopy[16];
    if (iv16) {
        memcpy(ivCopy, iv16, 16);
    } else {
        memset(ivCopy, 0, 16);
    }

    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, len, ivCopy, in, out);
    mbedtls_aes_free(&ctx);
    return (ret == 0);
}

uint32_t CryptoP256::getSignatureCounter() {
    return _sigCounter;
}

uint32_t CryptoP256::incrementSignatureCounter() {
    _sigCounter++;
    Preferences prefs;
    prefs.begin("fido_vault", false);
    prefs.putUInt("sig_counter", _sigCounter);
    prefs.end();
    return _sigCounter;
}
