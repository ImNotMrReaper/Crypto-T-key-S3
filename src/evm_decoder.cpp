/**
 * evm_decoder.cpp — EVM RLP & ERC-20 PEPE Clear-Signing Engine
 */

#include "evm_decoder.h"
#include "bip32_engine.h"
#include <mbedtls/bignum.h>
#include <mbedtls/platform_util.h>
#include <string.h>
#include <stdio.h>

// Canonical PEPE ERC-20 Contract on Ethereum Mainnet:
// 0x6982508145454ce325ddbe47a25d4ec3d2311933
static const uint8_t CANONICAL_PEPE_CONTRACT[20] = {
    0x69, 0x82, 0x50, 0x81, 0x45, 0x45, 0x4c, 0xe3,
    0x25, 0xdd, 0xbe, 0x47, 0xa2, 0x5d, 0x4e, 0xc3,
    0xd2, 0x31, 0x19, 0x33
};

// ─── Lightweight Embedded RLP Cursor ─────────────────────────────────────────
class RlpCursor {
public:
    RlpCursor(const uint8_t* buf, size_t len) : _buf(buf), _len(len), _pos(0) {}

    bool hasMore() const { return _pos < _len; }
    size_t getRemaining() const { return (_pos < _len) ? (_len - _pos) : 0; }

    bool readListHeader(size_t* listLen) {
        if (_pos >= _len) return false;
        uint8_t b = _buf[_pos++];
        if (b >= 0xC0 && b <= 0xF7) {
            *listLen = b - 0xC0;
            return true;
        } else if (b >= 0xF8) {
            size_t lenBytes = b - 0xF7;
            if (_pos + lenBytes > _len) return false;
            size_t l = 0;
            for (size_t i = 0; i < lenBytes; i++) {
                l = (l << 8) | _buf[_pos++];
            }
            *listLen = l;
            return true;
        }
        return false;
    }

    bool readItem(const uint8_t** data, size_t* dataLen) {
        if (_pos >= _len) return false;
        uint8_t b = _buf[_pos];
        if (b < 0x80) {
            *data = _buf + _pos;
            *dataLen = 1;
            _pos++;
            return true;
        } else if (b <= 0xB7) {
            _pos++;
            size_t l = b - 0x80;
            if (_pos + l > _len) return false;
            *data = _buf + _pos;
            *dataLen = l;
            _pos += l;
            return true;
        } else if (b <= 0xBF) {
            _pos++;
            size_t lenBytes = b - 0xB7;
            if (_pos + lenBytes > _len) return false;
            size_t l = 0;
            for (size_t i = 0; i < lenBytes; i++) {
                l = (l << 8) | _buf[_pos++];
            }
            if (_pos + l > _len) return false;
            *data = _buf + _pos;
            *dataLen = l;
            _pos += l;
            return true;
        } else {
            // Embedded list as an item
            size_t listLen = 0;
            size_t start = _pos;
            if (!readListHeader(&listLen)) return false;
            if (_pos + listLen > _len) return false;
            _pos += listLen;
            *data = _buf + start;
            *dataLen = _pos - start;
            return true;
        }
    }

    bool readUint64(uint64_t* val) {
        const uint8_t* d = nullptr;
        size_t l = 0;
        if (!readItem(&d, &l)) return false;
        if (l == 0) {
            *val = 0;
            return true;
        }
        if (l > 8) l = 8; // clamp to 64-bit
        uint64_t v = 0;
        for (size_t i = 0; i < l; i++) {
            v = (v << 8) | d[i];
        }
        *val = v;
        return true;
    }

    bool skipItem() {
        const uint8_t* dummy = nullptr;
        size_t l = 0;
        return readItem(&dummy, &l);
    }

private:
    const uint8_t* _buf;
    size_t _len;
    size_t _pos;
};

// ─── Utility Methods ─────────────────────────────────────────────────────────

bool EvmTxDecoder::isPepeContract(const uint8_t* addr20) {
    if (!addr20) return false;
    for (int i = 0; i < 20; i++) {
        uint8_t a = addr20[i];
        uint8_t b = CANONICAL_PEPE_CONTRACT[i];
        if (a != b) return false;
    }
    return true;
}

void EvmTxDecoder::toChecksumAddress(const uint8_t* addr20, char* out43) {
    Bip32Engine::eip55Encode(addr20, out43);
}

void EvmTxDecoder::toCompactAddress(const char* fullAddr, char* outCompact, size_t maxLen) {
    if (!fullAddr || !outCompact || maxLen < 15) return;
    size_t len = strlen(fullAddr);
    if (len >= 42) {
        // "0x1234...5678"
        snprintf(outCompact, maxLen, "%.6s...%.4s", fullAddr, fullAddr + len - 4);
    } else {
        strncpy(outCompact, fullAddr, maxLen - 1);
        outCompact[maxLen - 1] = '\0';
    }
}

void EvmTxDecoder::formatTokenValue(const uint8_t* valBytes, size_t valLen, uint8_t decimals, 
                                   const char* symbol, char* outStr, size_t maxLen) {
    if (!outStr || maxLen == 0) return;
    if (!valBytes || valLen == 0) {
        snprintf(outStr, maxLen, "0.00 %s", symbol ? symbol : "");
        return;
    }

    mbedtls_mpi m;
    mbedtls_mpi_init(&m);
    if (mbedtls_mpi_read_binary(&m, valBytes, valLen) != 0) {
        mbedtls_mpi_free(&m);
        snprintf(outStr, maxLen, "0.00 %s", symbol ? symbol : "");
        return;
    }

    char decBuf[96] = {0};
    size_t olen = 0;
    mbedtls_mpi_write_string(&m, 10, decBuf, sizeof(decBuf), &olen);
    mbedtls_mpi_free(&m);

    if (olen == 0) {
        snprintf(outStr, maxLen, "0.00 %s", symbol ? symbol : "");
        return;
    }

    if (olen > decimals) {
        size_t wholeLen = olen - decimals;
        char wholeStr[64] = {0};
        memcpy(wholeStr, decBuf, (wholeLen < 63) ? wholeLen : 63);
        
        char fracStr[8] = {0};
        memcpy(fracStr, decBuf + wholeLen, (decimals >= 2) ? 2 : decimals);

        // Convert whole part to float/double or integer for metric prefix (K, M, B)
        uint64_t wholeInt = strtoull(wholeStr, NULL, 10);
        if (wholeInt >= 1000000000ULL) {
            double bVal = (double)wholeInt / 1000000000.0;
            snprintf(outStr, maxLen, "%.2fB %s", bVal, symbol);
        } else if (wholeInt >= 1000000ULL) {
            double mVal = (double)wholeInt / 1000000.0;
            snprintf(outStr, maxLen, "%.2fM %s", mVal, symbol);
        } else if (wholeInt >= 1000ULL) {
            double kVal = (double)wholeInt / 1000.0;
            snprintf(outStr, maxLen, "%.2fK %s", kVal, symbol);
        } else {
            snprintf(outStr, maxLen, "%llu.%s %s", (unsigned long long)wholeInt, fracStr, symbol);
        }
    } else {
        // Less than 1 unit
        size_t leadingZeros = decimals - olen;
        if (leadingZeros >= 4) {
            snprintf(outStr, maxLen, "<0.0001 %s", symbol);
        } else {
            char fullFrac[32] = {0};
            memset(fullFrac, '0', leadingZeros);
            strncat(fullFrac, decBuf, 4);
            snprintf(outStr, maxLen, "0.%.4s %s", fullFrac, symbol);
        }
    }
}

// ─── Main Transaction Clear-Sign Decoder ─────────────────────────────────────

bool EvmTxDecoder::decodeTx(const uint8_t* rawTx, size_t len, EvmDecodedTx& out) {
    if (!rawTx || len < 4) return false;
    memset(&out, 0, sizeof(out));

    const uint8_t* rlpPayload = rawTx;
    size_t rlpLen = len;

    // 1. Detect Envelope Type (EIP-2718)
    if (rawTx[0] == 0x02) {
        out.txType = EVM_TX_EIP1559;
        rlpPayload = rawTx + 1;
        rlpLen = len - 1;
    } else if (rawTx[0] >= 0xC0) {
        out.txType = EVM_TX_LEGACY;
    } else {
        out.txType = EVM_TX_UNKNOWN;
        return false;
    }

    RlpCursor cursor(rlpPayload, rlpLen);
    size_t listPayloadLen = 0;
    if (!cursor.readListHeader(&listPayloadLen)) return false;

    const uint8_t* toBytes = nullptr;
    size_t toLen = 0;
    const uint8_t* valBytes = nullptr;
    size_t valLen = 0;
    const uint8_t* dataBytes = nullptr;
    size_t dataLen = 0;

    if (out.txType == EVM_TX_EIP1559) {
        // EIP-1559 Type 2 Envelope:
        // [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, ...]
        cursor.readUint64(&out.chainId);
        cursor.readUint64(&out.nonce);
        
        // maxPriorityFeePerGas
        cursor.skipItem();

        // maxFeePerGas
        const uint8_t* feeBytes = nullptr;
        size_t feeLen = 0;
        cursor.readItem(&feeBytes, &feeLen);

        // gasLimit
        cursor.readUint64(&out.gasLimit);

        // to
        cursor.readItem(&toBytes, &toLen);

        // value
        cursor.readItem(&valBytes, &valLen);

        // data
        cursor.readItem(&dataBytes, &dataLen);

        snprintf(out.dispFee, sizeof(out.dispFee), "Fee: Dynamic EIP1559");
    } else {
        // Legacy EIP-155 Envelope:
        // [nonce, gasPrice, gasLimit, to, value, data, v, r, s]
        cursor.readUint64(&out.nonce);

        // gasPrice
        const uint8_t* priceBytes = nullptr;
        size_t priceLen = 0;
        cursor.readItem(&priceBytes, &priceLen);

        // gasLimit
        cursor.readUint64(&out.gasLimit);

        // to
        cursor.readItem(&toBytes, &toLen);

        // value
        cursor.readItem(&valBytes, &valLen);

        // data
        cursor.readItem(&dataBytes, &dataLen);

        snprintf(out.dispFee, sizeof(out.dispFee), "Fee: Legacy Type 0");
    }

    // 2. Process Target / Contract Address
    if (toBytes && toLen == 20) {
        memcpy(out.toAddressRaw, toBytes, 20);
        toChecksumAddress(out.toAddressRaw, out.toAddress);
    } else {
        strncpy(out.toAddress, "0x (Contract Deploy)", sizeof(out.toAddress) - 1);
    }

    // 3. Process Native Value
    if (valBytes && valLen > 0) {
        out.valueRawLen = (valLen < 32) ? valLen : 32;
        memcpy(out.valueRaw, valBytes, out.valueRawLen);
        formatTokenValue(valBytes, valLen, 18, "ETH", out.valueEth, sizeof(out.valueEth));
    } else {
        strncpy(out.valueEth, "0.00 ETH", sizeof(out.valueEth) - 1);
    }

    // 4. Calldata Inspection & ERC-20 Parsing
    if (dataBytes && dataLen >= 4) {
        uint32_t selector = ((uint32_t)dataBytes[0] << 24) |
                            ((uint32_t)dataBytes[1] << 16) |
                            ((uint32_t)dataBytes[2] << 8)  |
                            (uint32_t)dataBytes[3];

        bool isPepe = isPepeContract(out.toAddressRaw);
        const char* symbol = isPepe ? "PEPE" : "TOKEN";
        const char* name   = isPepe ? "Pepe (ERC-20)" : "ERC-20 Token";

        if (selector == 0xa9059cbb && dataLen >= 68) {
            // ERC-20 transfer(address _to, uint256 _value)
            out.action = ACTION_ERC20_TRANSFER;
            strncpy(out.tokenSymbol, symbol, sizeof(out.tokenSymbol) - 1);
            strncpy(out.tokenName, name, sizeof(out.tokenName) - 1);
            out.tokenDecimals = 18;

            const uint8_t* recipient20 = dataBytes + 16;
            toChecksumAddress(recipient20, out.recipientOrSpender);

            const uint8_t* amt32 = dataBytes + 36;
            formatTokenValue(amt32, 32, 18, symbol, out.tokenAmount, sizeof(out.tokenAmount));

            snprintf(out.dispAction, sizeof(out.dispAction), "SEND %s", symbol);
            strncpy(out.dispAmount, out.tokenAmount, sizeof(out.dispAmount) - 1);

            char compactRecipient[20];
            toCompactAddress(out.recipientOrSpender, compactRecipient, sizeof(compactRecipient));
            snprintf(out.dispTarget, sizeof(out.dispTarget), "To: %s", compactRecipient);

            return true;
        } else if (selector == 0x095ea7b3 && dataLen >= 68) {
            // ERC-20 approve(address _spender, uint256 _value)
            out.action = ACTION_ERC20_APPROVE;
            strncpy(out.tokenSymbol, symbol, sizeof(out.tokenSymbol) - 1);
            strncpy(out.tokenName, name, sizeof(out.tokenName) - 1);
            out.tokenDecimals = 18;

            const uint8_t* spender20 = dataBytes + 16;
            toChecksumAddress(spender20, out.recipientOrSpender);

            const uint8_t* amt32 = dataBytes + 36;
            // Check for Unlimited Allowance (uint256.max or very high)
            if (amt32[0] == 0xFF && amt32[1] == 0xFF && amt32[2] == 0xFF) {
                out.isUnlimitedApproval = true;
                snprintf(out.tokenAmount, sizeof(out.tokenAmount), "UNLIMITED %s", symbol);
            } else {
                formatTokenValue(amt32, 32, 18, symbol, out.tokenAmount, sizeof(out.tokenAmount));
            }

            snprintf(out.dispAction, sizeof(out.dispAction), "APPROVE %s", symbol);
            strncpy(out.dispAmount, out.tokenAmount, sizeof(out.dispAmount) - 1);

            char compactSpender[20];
            toCompactAddress(out.recipientOrSpender, compactSpender, sizeof(compactSpender));
            snprintf(out.dispTarget, sizeof(out.dispTarget), "Spender: %s", compactSpender);

            return true;
        } else {
            // Generic contract interaction
            out.action = ACTION_CONTRACT_CALL;
            strncpy(out.tokenSymbol, "ETH", sizeof(out.tokenSymbol) - 1);
            strncpy(out.tokenName, "Smart Contract", sizeof(out.tokenName) - 1);

            snprintf(out.dispAction, sizeof(out.dispAction), "CALL CONTRACT");
            strncpy(out.dispAmount, out.valueEth, sizeof(out.dispAmount) - 1);

            char compactContract[20];
            toCompactAddress(out.toAddress, compactContract, sizeof(compactContract));
            snprintf(out.dispTarget, sizeof(out.dispTarget), "At: %s", compactContract);

            return true;
        }
    }

    // 5. Standard Native ETH Transfer
    out.action = ACTION_ETH_TRANSFER;
    strncpy(out.tokenSymbol, "ETH", sizeof(out.tokenSymbol) - 1);
    strncpy(out.tokenName, "Ethereum", sizeof(out.tokenName) - 1);
    out.tokenDecimals = 18;

    snprintf(out.dispAction, sizeof(out.dispAction), "SEND ETH");
    strncpy(out.dispAmount, out.valueEth, sizeof(out.dispAmount) - 1);

    char compactTo[20];
    toCompactAddress(out.toAddress, compactTo, sizeof(compactTo));
    snprintf(out.dispTarget, sizeof(out.dispTarget), "To: %s", compactTo);

    return true;
}

bool EvmTxDecoder::decodeHexTx(const char* hexStr, EvmDecodedTx& out) {
    if (!hexStr) return false;
    if (hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
        hexStr += 2;
    }

    size_t hexLen = strlen(hexStr);
    if (hexLen % 2 != 0 || hexLen < 8) return false;

    size_t binLen = hexLen / 2;
    uint8_t* rawBuf = (uint8_t*)malloc(binLen);
    if (!rawBuf) return false;

    for (size_t i = 0; i < binLen; i++) {
        char byteHex[3] = { hexStr[i * 2], hexStr[i * 2 + 1], 0 };
        rawBuf[i] = (uint8_t)strtoul(byteHex, NULL, 16);
    }

    bool ok = decodeTx(rawBuf, binLen, out);
    free(rawBuf);
    return ok;
}
