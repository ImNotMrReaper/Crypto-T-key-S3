/**
 * evm_decoder.h — EVM RLP & ERC-20 PEPE Transaction Clear-Sign Decoder
 * =====================================================================
 * Embedded zero-allocation RLP parser for LilyGo T-Dongle S3.
 * Supports:
 *   - EIP-2718 Typed Envelopes: Legacy Type 0 / EIP-155 & Modern EIP-1559 Type 2
 *   - ERC-20 canonical PEPE contract (0x6982508145454ce325ddbe47a25d4ec3d2311933)
 *   - ERC-20 transfer (0xa9059cbb) and approve (0x095ea7b3) ABI calldata
 *   - Safe big-number integer decimal formatting (18 decimals) via mbedTLS MPI
 *   - Responsive WYSIWYS display strings formatted for 160x80 ST7735 LCD
 */

#pragma once

#include <Arduino.h>

enum EvmTxType {
    EVM_TX_LEGACY = 0,
    EVM_TX_EIP1559 = 2,
    EVM_TX_UNKNOWN = 0xFF
};

enum EvmActionType {
    ACTION_ETH_TRANSFER = 0,
    ACTION_ERC20_TRANSFER,
    ACTION_ERC20_APPROVE,
    ACTION_CONTRACT_CALL
};

struct EvmDecodedTx {
    EvmTxType     txType;
    uint64_t      chainId;
    uint64_t      nonce;
    uint64_t      gasLimit;
    
    // Target address (Contract or Recipient)
    char          toAddress[44];        // Full EIP-55 0x... (42 chars + null)
    uint8_t       toAddressRaw[20];
    
    // Native ETH Value
    char          valueEth[32];         // e.g. "0.050 ETH"
    uint8_t       valueRaw[32];
    size_t        valueRawLen;

    // Action & Token details
    EvmActionType action;
    char          tokenSymbol[12];      // "PEPE", "ETH", "USDT"
    char          tokenName[24];        // "Pepe", "Ethereum"
    uint8_t       tokenDecimals;
    
    // For ERC-20:
    char          recipientOrSpender[44]; // Full 0x...
    char          tokenAmount[32];        // e.g. "5.00M PEPE" or "1,000,000 PEPE"
    bool          isUnlimitedApproval;

    // LCD WYSIWYS Display Fields (strictly <= 22 chars for 160x80 ST7735)
    char          dispAction[24];       // "SEND PEPE" / "APPROVE PEPE" / "SEND ETH"
    char          dispAmount[24];       // "5.00M PEPE"
    char          dispTarget[24];       // "To: 0x6982...1933"
    char          dispFee[24];          // "Fee: <0.001 ETH"
};

class EvmTxDecoder {
public:
    // Decodes raw RLP bytes (either legacy list or 0x02 || RLP list)
    static bool decodeTx(const uint8_t* rawTx, size_t len, EvmDecodedTx& out);

    // Decodes hex-encoded transaction string (with or without 0x prefix)
    static bool decodeHexTx(const char* hexStr, EvmDecodedTx& out);

    // Formats a 20-byte raw address to checksummed EIP-55 hex string
    static void toChecksumAddress(const uint8_t* addr20, char* out43);

    // Formats a compact address for 160x80 display (e.g. "0x6982...1933")
    static void toCompactAddress(const char* fullAddr, char* outCompact, size_t maxLen);

    // Formats arbitrary-precision uint256 bytes with specified decimals
    static void formatTokenValue(const uint8_t* valBytes, size_t valLen, uint8_t decimals, 
                                 const char* symbol, char* outStr, size_t maxLen);

private:
    static bool isPepeContract(const uint8_t* addr20);
};
