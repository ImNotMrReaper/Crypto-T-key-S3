#!/usr/bin/env python3
"""
test_evm_tx.py — EVM RLP & PEPE Transaction Encoder & Test Generator
====================================================================
Generates authentic EIP-1559 (Type 2) and Legacy (Type 0) RLP transaction
payloads for on-device and host clear-sign testing on Crypto TKey S3.
"""

PEPE_CONTRACT = bytes.fromhex("6982508145454ce325ddbe47a25d4ec3d2311933")
RECIPIENT     = bytes.fromhex("d8da6bf26964af9d7eed9e03e53415d37aa96045") # vitalik.eth

def rlp_encode_bytes(b: bytes) -> bytes:
    if len(b) == 1 and b[0] < 0x80:
        return b
    elif len(b) <= 55:
        return bytes([0x80 + len(b)]) + b
    else:
        len_bytes = len(b).to_bytes((len(b).bit_length() + 7) // 8, 'big')
        return bytes([0xB7 + len(len_bytes)]) + len_bytes + b

def rlp_encode_int(n: int) -> bytes:
    if n == 0:
        return b'\x80'
    b = n.to_bytes((n.bit_length() + 7) // 8, 'big')
    return rlp_encode_bytes(b)

def rlp_encode_list(items: list) -> bytes:
    payload = b''.join(items)
    if len(payload) <= 55:
        return bytes([0xC0 + len(payload)]) + payload
    else:
        len_bytes = len(payload).to_bytes((len(payload).bit_length() + 7) // 8, 'big')
        return bytes([0xF7 + len(len_bytes)]) + len_bytes + payload

def encode_erc20_transfer(to_addr: bytes, amount_wei: int) -> bytes:
    selector = bytes.fromhex("a9059cbb")
    to_padded = to_addr.rjust(32, b"\x00")
    amt_bytes = amount_wei.to_bytes(32, byteorder="big")
    return selector + to_padded + amt_bytes

def encode_erc20_approve(spender_addr: bytes, amount_wei: int) -> bytes:
    selector = bytes.fromhex("095ea7b3")
    spender_padded = spender_addr.rjust(32, b"\x00")
    amt_bytes = amount_wei.to_bytes(32, byteorder="big")
    return selector + spender_padded + amt_bytes

def generate_eip1559_pepe_tx(tokens_count: int = 5000000):
    """Generates an EIP-1559 transaction sending PEPE."""
    amount_wei = tokens_count * (10**18)
    data = encode_erc20_transfer(RECIPIENT, amount_wei)
    
    # EIP-1559 items:
    # [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList]
    encoded_items = [
        rlp_encode_int(1),                         # chainId: Ethereum Mainnet
        rlp_encode_int(42),                        # nonce
        rlp_encode_int(2 * (10**9)),               # maxPriorityFeePerGas: 2 Gwei
        rlp_encode_int(30 * (10**9)),              # maxFeePerGas: 30 Gwei
        rlp_encode_int(65000),                     # gasLimit
        rlp_encode_bytes(PEPE_CONTRACT),           # to: PEPE contract
        rlp_encode_int(0),                         # value: 0 ETH
        rlp_encode_bytes(data),                    # data: ERC-20 transfer
        rlp_encode_list([])                        # accessList: empty
    ]

    encoded_payload = rlp_encode_list(encoded_items)
    # Prefix with 0x02 envelope byte
    eip1559_raw = bytes([0x02]) + encoded_payload
    return eip1559_raw.hex()

def generate_legacy_eth_tx(amount_eth: float = 0.05):
    """Generates a standard legacy Type 0 ETH transfer."""
    amount_wei = int(amount_eth * (10**18))
    encoded_items = [
        rlp_encode_int(12),                        # nonce
        rlp_encode_int(25 * (10**9)),              # gasPrice: 25 Gwei
        rlp_encode_int(21000),                     # gasLimit
        rlp_encode_bytes(RECIPIENT),               # to
        rlp_encode_int(amount_wei),                # value
        rlp_encode_bytes(b""),                     # data (empty)
        rlp_encode_int(1),                         # chainId / v
        rlp_encode_int(0),                         # r
        rlp_encode_int(0)                          # s
    ]
    return rlp_encode_list(encoded_items).hex()

def main():
    print("==========================================================")
    print("  Crypto TKey S3 — EVM Transaction Test Vector Generator")
    print("==========================================================")

    pepe_tx_hex = generate_eip1559_pepe_tx(5000000)
    print("\n1. EIP-1559 (Type 2) PEPE Transfer (5,000,000 PEPE):")
    print(f"Raw Hex: {pepe_tx_hex}")
    print(f"CLI Command: decode_evm {pepe_tx_hex}")

    eth_tx_hex = generate_legacy_eth_tx(0.050)
    print("\n2. Legacy (Type 0) ETH Transfer (0.050 ETH):")
    print(f"Raw Hex: {eth_tx_hex}")
    print(f"CLI Command: decode_evm {eth_tx_hex}")

if __name__ == "__main__":
    main()
