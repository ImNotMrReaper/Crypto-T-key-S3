/**
 * evm_decoder.cpp — EVM RLP & ERC-20 PEPE Clear-Signing Engine
 */

#include "evm_decoder.h"
#include "bip32_engine.h"
#include <mbedtls/bignum.h>
#include <mbedtls/platform_util.h>
#include <string.h>
#include <stdio.h>


// ─── Strict RLP Cursor ───────────────────────────────────────────────────────
// Canonical RLP only (audit W5): every length is bounds-checked by subtraction, long forms
// must be needed and have no leading zero, a single byte < 0x80 must be encoded as itself,
// and callers check every read. Anything else is rejected, never guessed.
class RlpCursor {
public:
    RlpCursor(const uint8_t* buf, size_t len) : _buf(buf), _len(len), _pos(0) {}

    bool atEnd() const { return _pos == _len; }

    // Reads one item header. isList tells which kind; the payload is [_pos, _pos + *payloadLen).
    bool readHeader(bool* isList, size_t* payloadLen) {
        if (_pos >= _len) return false;
        uint8_t b = _buf[_pos];
        size_t left = _len - _pos - 1;
        if (b < 0x80) {                       // the byte is its own string
            *isList = false;
            *payloadLen = 1;
            return true;                      // _pos stays: the payload is the byte itself
        }
        _pos++;
        *isList = b >= 0xC0;
        uint8_t base = *isList ? 0xC0 : 0x80;
        uint8_t off = b - base;
        size_t l;
        if (off <= 55) {
            l = off;
            if (!*isList && l == 1 && (_pos >= _len || _buf[_pos] < 0x80)) return false;   // non-canonical
        } else {
            size_t lenBytes = off - 55;
            if (lenBytes > sizeof(size_t) || lenBytes > left || _buf[_pos] == 0) return false;
            l = 0;
            for (size_t i = 0; i < lenBytes; i++) l = (l << 8) | _buf[_pos++];
            if (l <= 55) return false;        // long form used for a short length
            left -= lenBytes;
        }
        if (l > left) return false;
        *payloadLen = l;
        return true;
    }

    bool readString(const uint8_t** data, size_t* dataLen) {
        bool isList;
        size_t l;
        if (!readHeader(&isList, &l) || isList) return false;
        *data = _buf + _pos;
        *dataLen = l;
        _pos += l;
        return true;
    }

    // Unsigned integer: at most maxBytes, no leading zero byte (0 is the empty string)
    bool readUintBytes(const uint8_t** data, size_t* dataLen, size_t maxBytes) {
        if (!readString(data, dataLen)) return false;
        return *dataLen <= maxBytes && (*dataLen == 0 || (*data)[0] != 0);
    }

    bool readUint64(uint64_t* val) {
        const uint8_t* d;
        size_t l;
        if (!readUintBytes(&d, &l, 8)) return false;
        uint64_t v = 0;
        for (size_t i = 0; i < l; i++) v = (v << 8) | d[i];
        *val = v;
        return true;
    }

    // Enters a list: returns a cursor over exactly its payload and skips it here.
    bool readList(RlpCursor* inner) {
        bool isList;
        size_t l;
        if (!readHeader(&isList, &l) || !isList) return false;
        *inner = RlpCursor(_buf + _pos, l);
        _pos += l;
        return true;
    }

private:
    const uint8_t* _buf;
    size_t _len;
    size_t _pos;
};

// EIP-2930 access list: [[address(20), [storageKey(32), ...]], ...]. It can't move funds,
// but it must be well-formed; returns the number of entries.
static bool readAccessList(RlpCursor& c, size_t* entries) {
    RlpCursor list(nullptr, 0);
    if (!c.readList(&list)) return false;
    *entries = 0;
    while (!list.atEnd()) {
        RlpCursor entry(nullptr, 0), keys(nullptr, 0);
        const uint8_t* d;
        size_t l;
        if (!list.readList(&entry) || !entry.readString(&d, &l) || l != 20 || !entry.readList(&keys)) return false;
        while (!keys.atEnd()) {
            if (!keys.readString(&d, &l) || l != 32) return false;
        }
        if (!entry.atEnd()) return false;
        (*entries)++;
    }
    return true;
}

// ERC-20 decimals we are sure of (Ethereum mainnet contracts from the coin catalog).
// Any other token's amount is shown in raw base units rather than with a guessed scale.
struct KnownToken { const char* contract; const char* symbol; uint8_t decimals; };
static const KnownToken KNOWN_TOKENS[] = {
    {"dac17f958d2ee523a2206206994597c13d831ec7", "USDT", 6},
    {"a0b86991c6218b36c1d19d4a2e9eb0ce3606eb48", "USDC", 6},
    {"2260fac5e5542a773aa44fbcfedf7c193bc2c599", "WBTC", 8},
    {"6b175474e89094c44da98b954eedeac495271d0f", "DAI", 18},
    {"514910771af9ca656af840dff83e8264ecf986ca", "LINK", 18},
    {"1f9840a85d5af5bf1d1762f925bdaddc4201f984", "UNI", 18},
    {"6982508145454ce325ddbe47a25d4ec3d2311933", "PEPE", 18},
    {"95ad61b0a150d79219dcf64e1e6cc01f0b64c4ce", "SHIB", 18},
};

static const KnownToken* findKnownToken(const uint8_t* addr20) {
    char hex[41];
    for (int i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02x", addr20[i]);
    for (const KnownToken& t : KNOWN_TOKENS) {
        if (strcmp(hex, t.contract) == 0) return &t;
    }
    return nullptr;
}

// ABI word holding an address: the 12 leading bytes must be zero
static bool abiAddress(const uint8_t* word32) {
    for (int i = 0; i < 12; i++) if (word32[i] != 0) return false;
    return true;
}

// ─── Utility Methods ─────────────────────────────────────────────────────────

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
    if (!symbol) symbol = "";
    char dec[160] = {0};   // 2^512 (the largest accepted input) has 155 decimal digits
    size_t n = 0;
    mbedtls_mpi m;
    mbedtls_mpi_init(&m);
    bool ok = valBytes && valLen && valLen <= 64 && mbedtls_mpi_read_binary(&m, valBytes, valLen) == 0 &&
              mbedtls_mpi_write_string(&m, 10, dec, sizeof(dec), &n) == 0;
    mbedtls_mpi_free(&m);
    size_t digits = ok ? strlen(dec) : 0;
    if (!valBytes || valLen == 0 || (ok && digits == 1 && dec[0] == '0')) {
        snprintf(outStr, maxLen, "0 %s", symbol);
        return;
    }
    if (!ok || digits == 0) {   // never show an unreadable amount as zero
        snprintf(outStr, maxLen, "? %s", symbol);
        return;
    }
    if (digits <= decimals) {
        // Below one whole unit: up to 4 significant places, never rounded up to look bigger
        size_t zeros = decimals - digits;
        if (zeros >= 4) snprintf(outStr, maxLen, "<0.0001 %s", symbol);
        else snprintf(outStr, maxLen, "0.%.*s%.*s %s", (int)zeros, "0000", (int)(4 - zeros), dec, symbol);
        return;
    }
    size_t whole = digits - decimals;   // digits before the decimal point
    if (whole > 15) {
        // Too large for exact grouping: mantissa and power of ten, e.g. "1.84e21 units"
        snprintf(outStr, maxLen, "%c.%.2se%u %s", dec[0], dec + 1, (unsigned)(whole - 1), symbol);
        return;
    }
    char wholeStr[16] = {0};
    memcpy(wholeStr, dec, whole);
    uint64_t w = strtoull(wholeStr, nullptr, 10);   // <= 15 digits: exact
    if (w >= 1000000000ULL)   snprintf(outStr, maxLen, "%.2fB %s", (double)w / 1e9, symbol);
    else if (w >= 1000000ULL) snprintf(outStr, maxLen, "%.2fM %s", (double)w / 1e6, symbol);
    else if (w >= 1000ULL)    snprintf(outStr, maxLen, "%.2fK %s", (double)w / 1e3, symbol);
    else if (decimals == 0)   snprintf(outStr, maxLen, "%llu %s", (unsigned long long)w, symbol);
    else snprintf(outStr, maxLen, "%llu.%.*s %s", (unsigned long long)w, decimals >= 2 ? 2 : 1, dec + whole, symbol);
}

// ─── Main Transaction Clear-Sign Decoder ─────────────────────────────────────

bool EvmTxDecoder::decodeTx(const uint8_t* rawTx, size_t len, EvmDecodedTx& out) {
    memset(&out, 0, sizeof(out));
    out.txType = EVM_TX_UNKNOWN;
    if (!rawTx || len < 2) return false;

    // 1. Envelope (EIP-2718): 0x02 || rlp(...) is EIP-1559; a bare list is legacy
    const uint8_t* rlp = rawTx;
    size_t rlpLen = len;
    if (rawTx[0] == 0x02) {
        out.txType = EVM_TX_EIP1559;
        rlp++;
        rlpLen--;
    } else if (rawTx[0] >= 0xC0) {
        out.txType = EVM_TX_LEGACY;
    } else {
        return false;   // 0x01 (EIP-2930), blob and future types aren't supported
    }

    RlpCursor outer(rlp, rlpLen), f(nullptr, 0);
    if (!outer.readList(&f) || !outer.atEnd()) return false;   // nothing may follow the list

    const uint8_t *toBytes, *valBytes, *dataBytes, *feeBytes, *tip;
    size_t toLen, valLen, dataLen, feeLen, tipLen;
    if (out.txType == EVM_TX_EIP1559) {
        // Unsigned EIP-1559: [chainId, nonce, maxPriorityFee, maxFee, gasLimit, to, value, data, accessList]
        size_t accessEntries = 0;
        if (!f.readUint64(&out.chainId) || !f.readUint64(&out.nonce) ||
            !f.readUintBytes(&tip, &tipLen, 32) || !f.readUintBytes(&feeBytes, &feeLen, 32) ||
            !f.readUint64(&out.gasLimit) || !f.readString(&toBytes, &toLen) ||
            !f.readUintBytes(&valBytes, &valLen, 32) || !f.readString(&dataBytes, &dataLen) ||
            !readAccessList(f, &accessEntries) || !f.atEnd()) {
            return false;
        }
    } else {
        // Unsigned EIP-155: [nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0].
        // Pre-EIP-155 (6 fields) is replayable on every chain: rejected.
        const uint8_t *z1, *z2;
        size_t z1Len, z2Len;
        if (!f.readUint64(&out.nonce) || !f.readUintBytes(&feeBytes, &feeLen, 32) ||
            !f.readUint64(&out.gasLimit) || !f.readString(&toBytes, &toLen) ||
            !f.readUintBytes(&valBytes, &valLen, 32) || !f.readString(&dataBytes, &dataLen) ||
            !f.readUint64(&out.chainId) || !f.readUintBytes(&z1, &z1Len, 32) ||
            !f.readUintBytes(&z2, &z2Len, 32) || z1Len != 0 || z2Len != 0 || !f.atEnd()) {
            return false;
        }
    }
    if (out.chainId == 0 || out.gasLimit == 0) return false;
    if (toLen != 0 && toLen != 20) return false;

    // 2. Target (an empty "to" deploys a contract)
    if (toLen == 20) {
        memcpy(out.toAddressRaw, toBytes, 20);
        toChecksumAddress(out.toAddressRaw, out.toAddress);
    } else {
        strncpy(out.toAddress, "0x (Contract Deploy)", sizeof(out.toAddress) - 1);
    }

    // 3. Native value and the maximum fee (gasLimit x maxFeePerGas / gasPrice), both in ETH
    out.valueRawLen = valLen;
    if (valLen) memcpy(out.valueRaw, valBytes, valLen);
    formatTokenValue(valBytes, valLen, 18, "ETH", out.valueEth, sizeof(out.valueEth));
    {
        mbedtls_mpi fee, gas;
        mbedtls_mpi_init(&fee);
        mbedtls_mpi_init(&gas);
        uint8_t feeRaw[64] = {0};
        bool ok = mbedtls_mpi_read_binary(&fee, feeBytes, feeLen) == 0 &&
                  mbedtls_mpi_lset(&gas, 0) == 0;
        for (int sh = 56; ok && sh >= 0; sh -= 8) {   // gas = gasLimit (uint64, one byte at a time)
            ok = mbedtls_mpi_shift_l(&gas, 8) == 0 && mbedtls_mpi_add_int(&gas, &gas, (out.gasLimit >> sh) & 0xFF) == 0;
        }
        ok = ok && mbedtls_mpi_mul_mpi(&fee, &fee, &gas) == 0 && mbedtls_mpi_write_binary(&fee, feeRaw, sizeof(feeRaw)) == 0;
        char feeEth[20];
        if (ok) formatTokenValue(feeRaw, sizeof(feeRaw), 18, "ETH", feeEth, sizeof(feeEth));
        if (ok) snprintf(out.dispFee, sizeof(out.dispFee), "Fee<%.19s", feeEth);   // "Fee<" + 19 fits 24
        else snprintf(out.dispFee, sizeof(out.dispFee), "Fee: ?");
        mbedtls_mpi_free(&fee);
        mbedtls_mpi_free(&gas);
    }

    // 4. Calldata: ERC-20 transfer / approve need exactly one selector + two ABI words
    if (dataLen >= 4 && toLen == 20) {
        uint32_t selector = ((uint32_t)dataBytes[0] << 24) | ((uint32_t)dataBytes[1] << 16) |
                            ((uint32_t)dataBytes[2] << 8)  | (uint32_t)dataBytes[3];
        bool erc20 = (selector == 0xa9059cbb || selector == 0x095ea7b3);
        if (erc20) {
            if (dataLen != 68 || !abiAddress(dataBytes + 4)) return false;   // malformed ABI: reject
            // Token names and decimals from the catalog only apply on Ethereum mainnet
            const KnownToken* tok = (out.chainId == 1) ? findKnownToken(out.toAddressRaw) : nullptr;
            const char* symbol = tok ? tok->symbol : "TOKEN";
            strncpy(out.tokenSymbol, symbol, sizeof(out.tokenSymbol) - 1);
            strncpy(out.tokenName, tok ? tok->symbol : "Unknown ERC-20", sizeof(out.tokenName) - 1);
            out.tokenDecimals = tok ? tok->decimals : 0;
            toChecksumAddress(dataBytes + 16, out.recipientOrSpender);
            const uint8_t* amt32 = dataBytes + 36;
            if (selector == 0x095ea7b3) {
                // An allowance of 2^128 or more is unlimited for every practical purpose
                bool huge = false;
                for (int i = 0; i < 16; i++) huge |= amt32[i] != 0;
                out.action = ACTION_ERC20_APPROVE;
                out.isUnlimitedApproval = huge;
            } else {
                out.action = ACTION_ERC20_TRANSFER;
            }
            if (out.isUnlimitedApproval) {
                snprintf(out.tokenAmount, sizeof(out.tokenAmount), "UNLIMITED %s", symbol);
            } else if (tok) {
                formatTokenValue(amt32, 32, tok->decimals, symbol, out.tokenAmount, sizeof(out.tokenAmount));
            } else {
                formatTokenValue(amt32, 32, 0, "units", out.tokenAmount, sizeof(out.tokenAmount));
            }
            snprintf(out.dispAction, sizeof(out.dispAction), "%s %s",
                     out.action == ACTION_ERC20_APPROVE ? "APPROVE" : "SEND", symbol);
            strncpy(out.dispAmount, out.tokenAmount, sizeof(out.dispAmount) - 1);
            char compact[20];
            toCompactAddress(out.recipientOrSpender, compact, sizeof(compact));
            snprintf(out.dispTarget, sizeof(out.dispTarget), "%s%.14s",   // "0x1234...abcd" is 13
                     out.action == ACTION_ERC20_APPROVE ? "Spender: " : "To: ", compact);
            return true;
        }
    }
    if (dataLen > 0) {
        // Generic contract call or deployment: show the ETH it moves and where
        out.action = ACTION_CONTRACT_CALL;
        strncpy(out.tokenSymbol, "ETH", sizeof(out.tokenSymbol) - 1);
        strncpy(out.tokenName, toLen ? "Smart Contract" : "Contract Deploy", sizeof(out.tokenName) - 1);
        snprintf(out.dispAction, sizeof(out.dispAction), toLen ? "CALL CONTRACT" : "DEPLOY CONTRACT");
        strncpy(out.dispAmount, out.valueEth, sizeof(out.dispAmount) - 1);
        char compact[20];
        toCompactAddress(out.toAddress, compact, sizeof(compact));
        snprintf(out.dispTarget, sizeof(out.dispTarget), "At: %s", compact);
        return true;
    }
    if (toLen == 0) return false;   // a deployment with no code does nothing

    // 5. Plain ETH transfer
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

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool EvmTxDecoder::decodeHexTx(const char* hexStr, EvmDecodedTx& out) {
    memset(&out, 0, sizeof(out));
    out.txType = EVM_TX_UNKNOWN;
    if (!hexStr) return false;
    if (hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) hexStr += 2;

    size_t hexLen = strlen(hexStr);
    if (hexLen % 2 != 0 || hexLen < 4 || hexLen > 2 * EVM_MAX_TX_BYTES) return false;

    size_t binLen = hexLen / 2;
    uint8_t* rawBuf = (uint8_t*)malloc(binLen);
    if (!rawBuf) return false;
    for (size_t i = 0; i < binLen; i++) {
        int hi = hexNibble(hexStr[i * 2]), lo = hexNibble(hexStr[i * 2 + 1]);
        if (hi < 0 || lo < 0) {   // every character must be hex (strtoul used to stop silently)
            free(rawBuf);
            return false;
        }
        rawBuf[i] = (uint8_t)(hi << 4 | lo);
    }
    bool ok = decodeTx(rawBuf, binLen, out);
    free(rawBuf);
    return ok;
}
