// EVM transaction decoder tests (audit W5): strict canonical RLP, EIP-155/1559 layouts, ERC-20
// amounts with the right decimals, unlimited approvals, and rejection of anything malformed.
// Vectors were generated with an independent Python RLP encoder; eip155_example is the signing
// payload published in EIP-155 (https://eips.ethereum.org/EIPS/eip-155, "Example").
#include "test.h"
#include "evm_decoder.h"
#include <string>
#include <random>

static bool decode(const char* hex, EvmDecodedTx& t) { return EvmTxDecoder::decodeHexTx(hex, t); }
static bool has(const char* s, const char* sub) { return strstr(s, sub) != nullptr; }

TEST(eip155_example_legacy) {
    EvmDecodedTx t;
    CHECK(decode("0xec098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a764000080018080", t));
    CHECK(t.txType == EVM_TX_LEGACY);
    CHECK(t.chainId == 1 && t.nonce == 9 && t.gasLimit == 21000);
    CHECK(t.action == ACTION_ETH_TRANSFER);
    CHECK(strcasecmp(t.toAddress, "0x3535353535353535353535353535353535353535") == 0);
    CHECK(has(t.valueEth, "1.00 ETH"));
    CHECK(has(t.dispFee, "0.0004 ETH"));   // 21000 x 20 gwei = 0.00042 ETH
}

TEST(eip1559_eth_transfer) {
    EvmDecodedTx t;
    CHECK(decode("02f00107843b9aca008506fc23ac0082ea609435353535353535353535353535353535353535358803782dace9d9000080c0", t));
    CHECK(t.txType == EVM_TX_EIP1559 && t.chainId == 1 && t.nonce == 7 && t.gasLimit == 60000);
    CHECK(has(t.valueEth, "0.2500 ETH"));
    CHECK(has(t.dispFee, "0.0018 ETH"));   // 60000 x 30 gwei
}

TEST(usdt_uses_six_decimals) {
    EvmDecodedTx t;
    CHECK(decode("02f86d0107843b9aca008506fc23ac0082ea6094dac17f958d2ee523a2206206994597c13d831ec780b844a9059cbb0000000000000000000000002222222222222222222222222222222222222222000000000000000000000000000000000000000000000000000000000016e360c0", t));
    CHECK(t.action == ACTION_ERC20_TRANSFER);
    CHECK(strcmp(t.tokenSymbol, "USDT") == 0);
    CHECK(has(t.tokenAmount, "1.50 USDT"));   // 1,500,000 base units / 10^6 (the old decoder showed ~0)
    CHECK(strcasecmp(t.recipientOrSpender, "0x2222222222222222222222222222222222222222") == 0);
}

TEST(unknown_token_shows_raw_units) {
    EvmDecodedTx t;
    CHECK(decode("02f86d0107843b9aca008506fc23ac0082ea6094111111111111111111111111111111111111111180b844a9059cbb0000000000000000000000002222222222222222222222222222222222222222000000000000000000000000000000000000000000000000000000000016e360c0", t));
    CHECK(strcmp(t.tokenSymbol, "TOKEN") == 0);
    CHECK(has(t.tokenAmount, "1.50M units"));
}

TEST(catalog_names_only_on_mainnet) {
    EvmDecodedTx t;   // the USDT contract address on chain 56 is not "USDT"
    CHECK(decode("02f86d3807843b9aca008506fc23ac0082ea6094dac17f958d2ee523a2206206994597c13d831ec780b844a9059cbb0000000000000000000000002222222222222222222222222222222222222222000000000000000000000000000000000000000000000000000000000016e360c0", t));
    CHECK(t.chainId == 56);
    CHECK(strcmp(t.tokenSymbol, "TOKEN") == 0);
    CHECK(has(t.tokenAmount, "units"));
}

TEST(unlimited_approvals_flagged) {
    EvmDecodedTx t;
    CHECK(decode("02f86d0107843b9aca008506fc23ac0082ea60946982508145454ce325ddbe47a25d4ec3d231193380b844095ea7b30000000000000000000000002222222222222222222222222222222222222222ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc0", t));
    CHECK(t.action == ACTION_ERC20_APPROVE && t.isUnlimitedApproval);
    CHECK(has(t.tokenAmount, "UNLIMITED PEPE"));
    // 2^200 starts with zero bytes: the old 3-byte 0xFF check missed it
    CHECK(decode("02f86d0107843b9aca008506fc23ac0082ea60946982508145454ce325ddbe47a25d4ec3d231193380b844095ea7b300000000000000000000000022222222222222222222222222222222222222220000000000000100000000000000000000000000000000000000000000000000c0", t));
    CHECK(t.isUnlimitedApproval);
    CHECK(decode("02f86d0107843b9aca008506fc23ac0082ea60946982508145454ce325ddbe47a25d4ec3d231193380b844095ea7b3000000000000000000000000222222222222222222222222222222222222222200000000000000000000000000000000000000000000003635c9adc5dea00000c0", t));
    CHECK(!t.isUnlimitedApproval);
    CHECK(has(t.tokenAmount, "1.00K PEPE"));
}

TEST(access_list_accepted_when_well_formed) {
    EvmDecodedTx t;
    CHECK(decode("02f8610107843b9aca008506fc23ac0082ea609435353535353535353535353535353535353535350180f838f7943535353535353535353535353535353535353535e1a00101010101010101010101010101010101010101010101010101010101010101", t));
}

TEST(malformed_rejected) {
    const char* bad[] = {
        "02e80107843b9aca008506fc23ac0082ea609435353535353535353535353535353535353535350180c000",   // trailing byte
        "02f86d0107843b9aca008506fc23ac0082ea6094dac17f958d2ee523a2206206994597c13d831ec780b844a9059cbb00000000000000000000000122222222222222222222222222222222222222220000000000000000000000000000000000000000000000000000000000000005c0",   // dirty address padding
        "02f86e0107843b9aca008506fc23ac0082ea6094dac17f958d2ee523a2206206994597c13d831ec780b845a9059cbb0000000000000000000000002222222222222222222222222222222222222222000000000000000000000000000000000000000000000000000000000000000500c0",   // 69-byte transfer calldata
        "e9098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a764000080",   // pre-EIP-155 (replayable)
        "02e20107010282ea609435353535353535353535353535353535353535350180c0010506",   // already signed
        "02e88007843b9aca008506fc23ac0082ea609435353535353535353535353535353535353535350180c0",   // chainId 0
        "01de01070182ea609435353535353535353535353535353535353535350180c0",   // EIP-2930 envelope
        "02e70107843b9aca008506fc23ac0082ea6093353535353535353535353535353535353535350180c0",   // 19-byte "to"
        "02f83e0107843b9aca008506fc23ac0082ea609435353535353535353535353535353535353535350180d6d59335353535353535353535353535353535353535c0",   // bad access list
        "02ea01820007843b9aca008506fc23ac0082ea609435353535353535353535353535353535353535350180c0",   // nonce with a leading zero
        "02e8010g843b9aca008506fc23ac0082ea609435353535353535353535353535353535353535350180c0",   // non-hex character
        "02e8010",   // odd length
        "",
    };
    for (const char* h : bad) {
        EvmDecodedTx t;
        bool ok = decode(h, t);
        if (ok) printf("    accepted: %.40s...\n", h);
        CHECK(!ok);
    }
}

TEST(every_truncation_rejected) {
    std::string good = "02f86d0107843b9aca008506fc23ac0082ea6094dac17f958d2ee523a2206206994597c13d831ec780b844a9059cbb0000000000000000000000002222222222222222222222222222222222222222000000000000000000000000000000000000000000000000000000000016e360c0";
    for (size_t n = 0; n < good.size(); n += 2) {
        EvmDecodedTx t;
        CHECK(!decode(good.substr(0, n).c_str(), t));
    }
}

TEST(fuzz_never_crashes) {
    std::mt19937 rng(1234);
    std::string good = "02f86d0107843b9aca008506fc23ac0082ea6094dac17f958d2ee523a2206206994597c13d831ec780b844095ea7b30000000000000000000000002222222222222222222222222222222222222222ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc0";
    const char* hex = "0123456789abcdef";
    int accepted = 0;
    for (int i = 0; i < 20000; i++) {
        std::string m = good;
        int edits = 1 + rng() % 4;
        for (int e = 0; e < edits; e++) m[rng() % m.size()] = hex[rng() % 16];
        EvmDecodedTx t;
        if (decode(m.c_str(), t)) {
            accepted++;
            // anything accepted must still be a complete, self-consistent decode
            CHECK(t.chainId != 0 && t.gasLimit != 0 && t.dispAction[0] && t.dispAmount[0]);
        }
    }
    printf("    %d of 20000 mutations still decoded (all consistent)\n", accepted);
}

int main() {
    RUN(eip155_example_legacy);
    RUN(eip1559_eth_transfer);
    RUN(usdt_uses_six_decimals);
    RUN(unknown_token_shows_raw_units);
    RUN(catalog_names_only_on_mainnet);
    RUN(unlimited_approvals_flagged);
    RUN(access_list_accepted_when_well_formed);
    RUN(malformed_rejected);
    RUN(every_truncation_rejected);
    RUN(fuzz_never_crashes);
    DONE();
}
