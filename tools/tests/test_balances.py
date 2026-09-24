"""Offline tests for tools/balances.py parsers (fixtures shaped like the real API replies)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import balances as b  # noqa: E402


class Parsers(unittest.TestCase):
    def test_esplora(self):
        self.assertAlmostEqual(b.parse_esplora({"chain_stats": {"funded_txo_sum": 150000000, "spent_txo_sum": 50000000}}), 1.0)

    def test_blockcypher(self):
        self.assertAlmostEqual(b.parse_blockcypher({"final_balance": 2500000000}), 25.0)

    def test_haskoin(self):
        self.assertAlmostEqual(b.parse_haskoin({"confirmed": 100000000, "unconfirmed": 5000000}), 1.05)

    def test_3xpl(self):
        j = {"data": {"balances": {"zcash-main": {"zcash": {"balance": "1000", "events": 27}}}}}
        self.assertAlmostEqual(b.parse_3xpl(j, "zcash-main", "zcash", 8), 1e-5)
        self.assertEqual(b.parse_3xpl({"data": {"balances": {"zcash-main": []}}}, "zcash-main", "zcash", 8), 0.0)

    def test_hex(self):
        self.assertAlmostEqual(b.parse_hex_amount("0xde0b6b3a7640000", 18), 1.0)
        self.assertEqual(b.parse_hex_amount("0x", 18), 0.0)

    def test_spl(self):
        acc = lambda s: {"account": {"data": {"parsed": {"info": {"tokenAmount": {"uiAmountString": s}}}}}}
        self.assertAlmostEqual(b.parse_spl_accounts({"value": [acc("1.5"), acc("2")]}), 3.5)
        self.assertEqual(b.parse_spl_accounts({"value": []}), 0.0)

    def test_trongrid(self):
        j = {"data": [{"balance": 12000000, "trc20": [{"TCFLL5dx5ZJdKnWuesXxi1VPwjLVmWZZy9": "5000000000000000000"}]}]}
        self.assertAlmostEqual(b.parse_trongrid(j), 12.0)
        self.assertAlmostEqual(b.parse_trongrid(j, "TCFLL5dx5ZJdKnWuesXxi1VPwjLVmWZZy9", 18), 5.0)
        self.assertEqual(b.parse_trongrid({"data": []}), 0.0)

    def test_xrpl_horizon(self):
        self.assertAlmostEqual(b.parse_xrpl({"account_data": {"Balance": "2268996827"}}), 2268.996827)
        self.assertAlmostEqual(b.parse_horizon({"balances": [{"asset_type": "credit_alphanum4"}, {"asset_type": "native", "balance": "12.5"}]}), 12.5)

    def test_cosmos_near_sui_vet(self):
        self.assertAlmostEqual(b.parse_cosmos_by_denom({"balance": {"denom": "uatom", "amount": "3"}}, 6), 3e-6)
        self.assertAlmostEqual(b.parse_near({"amount": "310569868922602840000000000"}), 310.56986892260284)
        self.assertAlmostEqual(b.parse_sui_graphql({"data": {"address": {"balance": {"totalBalance": "1500000000"}}}}), 1.5)
        self.assertEqual(b.parse_sui_graphql({"data": {"address": None}}), 0.0)
        self.assertAlmostEqual(b.parse_vechain({"balance": "0xde0b6b3a7640000"}), 1.0)

    def test_parse_addrs(self):
        text = ("ADDR BTC bc1qxyz Bitcoin Bitcoin|\n"
                "ADDR PEPE 0xabc EVM ERC-20 (ETH)|0x6982508145454ce325ddbe47a25d4ec3d2311933\n"
                "ADDR XRP rABC XRP Ledger XRP Ledger|\n"
                "ADDR BCH bitcoincash:qq1 Bitcoin Cash Bitcoin Cash|\n"
                "ADDR ETH 0xold\nADDR END\n")
        e = {x[0]: x for x in b.parse_addrs(text)}
        self.assertEqual(e["PEPE"], ("PEPE", "0xabc", "EVM", "ERC-20 (ETH)", "0x6982508145454ce325ddbe47a25d4ec3d2311933"))
        self.assertEqual(e["XRP"][2:4], ("XRP Ledger", "XRP Ledger"))
        self.assertEqual(e["BCH"][2:4], ("Bitcoin Cash", "Bitcoin Cash"))
        self.assertEqual(e["ETH"], ("ETH", "0xold", "", "", ""))   # old 3-field format still parsed

    def test_failure_is_none_not_zero(self):
        orig = b._fetch
        b._fetch = lambda *a: (_ for _ in ()).throw(TimeoutError("timed out"))
        try:
            self.assertIsNone(b.balance_for("BTC", "bc1q", "Bitcoin", "Bitcoin"))
        finally:
            b._fetch = orig


if __name__ == "__main__":
    unittest.main()
