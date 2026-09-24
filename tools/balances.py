"""
On-chain balances for every coin in the catalog, from keyless public endpoints.

    balance_for(symbol, address, family, network, contract) -> float | None

None means "couldn't find out" (network error, rate limit, unknown chain): the caller must
skip it rather than send 0, or a flaky API would wipe the balance shown on the key.
An account that doesn't exist yet on its chain (never funded) is a real 0.0.

Families and networks match the device's `addrs` output:
  ADDR <SYM> <address> <family> <network>|<contract>
"""
import json
import time
import urllib.error
import urllib.request

UA = {"User-Agent": "CryptoTKeyS3-Companion/2.0"}
TIMEOUT = 10

EVM_RPC = {
    "ethereum": "https://ethereum-rpc.publicnode.com",
    "bsc": "https://bsc-rpc.publicnode.com",
    "base": "https://base-rpc.publicnode.com",
    "arbitrum": "https://arbitrum-one-rpc.publicnode.com",
    "polygon": "https://polygon-bor-rpc.publicnode.com",
    "avalanche": "https://avalanche-c-chain-rpc.publicnode.com",
    "mantle": "https://mantle-rpc.publicnode.com",
}
EVM_NETWORK = {  # catalog network label -> EVM_RPC key
    "Ethereum": "ethereum", "ERC-20 (ETH)": "ethereum",
    "BNB Chain": "bsc", "BEP-20 (BSC)": "bsc",
    "Base": "base", "Arbitrum": "arbitrum", "Polygon": "polygon",
    "Avalanche C": "avalanche", "Mantle": "mantle",
}
TRC20_DECIMALS = {"JST": 18, "BTT": 18, "SUN": 18}
_decimals_cache = {}


class NotFound(Exception):
    """The account doesn't exist on-chain yet (never funded): balance is 0."""


def _get(url):
    req = urllib.request.Request(url, headers=UA)
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        if e.code == 404:
            raise NotFound(url) from e
        raise


def _post(url, body):
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers={**UA, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return json.loads(r.read().decode())


def _rpc(url, method, params):
    res = _post(url, {"jsonrpc": "2.0", "id": 1, "method": method, "params": params})
    if "error" in res:
        raise RuntimeError(f"{method}: {res['error']}")
    return res["result"]


# ─── Parsers (pure: JSON in, float out) ───────────────────────────────────────

def parse_esplora(j):            # mempool.space / litecoinspace.org address stats
    c = j["chain_stats"]
    return (c["funded_txo_sum"] - c["spent_txo_sum"]) / 1e8


def parse_blockcypher(j):
    return j["final_balance"] / 1e8


def parse_haskoin(j):          # Blockchain.com haskoin-store (BCH)
    return (j["confirmed"] + j.get("unconfirmed", 0)) / 1e8


def parse_3xpl(j, chain, currency, decimals):
    bal = j["data"]["balances"].get(chain) or {}
    if not bal:
        return 0.0
    return int(bal.get(currency, {}).get("balance", 0)) / 10 ** decimals


def parse_sui_graphql(j):
    addr = j["data"]["address"]
    if not addr or not addr.get("balance"):
        return 0.0
    return int(addr["balance"]["totalBalance"]) / 1e9


def parse_hex_amount(hex_str, decimals):
    v = int(hex_str, 16) if hex_str and hex_str != "0x" else 0
    return v / 10 ** decimals


def parse_spl_accounts(result):
    total = 0.0
    for acc in result.get("value", []):
        amt = acc["account"]["data"]["parsed"]["info"]["tokenAmount"]
        total += float(amt.get("uiAmountString") or amt.get("uiAmount") or 0)
    return total


def parse_trongrid(j, contract=None, decimals=6):
    data = j.get("data") or []
    if not data:
        return 0.0                               # unactivated account
    acct = data[0]
    if not contract:
        return acct.get("balance", 0) / 1e6
    for entry in acct.get("trc20", []):
        if contract in entry:
            return int(entry[contract]) / 10 ** decimals
    return 0.0


def parse_xrpl(result):
    return int(result["account_data"]["Balance"]) / 1e6


def parse_horizon(j):
    for b in j.get("balances", []):
        if b.get("asset_type") == "native":
            return float(b["balance"])
    return 0.0


def parse_cosmos_by_denom(j, decimals):
    return int(j["balance"]["amount"]) / 10 ** decimals


def parse_near(result):
    return int(result["amount"]) / 1e24


def parse_vechain(j):
    return int(j["balance"], 16) / 1e18


# ─── Fetchers ─────────────────────────────────────────────────────────────────

def _evm_native(net, address):
    return parse_hex_amount(_rpc(EVM_RPC[net], "eth_getBalance", [address, "latest"]), 18)


def _evm_token(net, address, contract):
    url = EVM_RPC[net]
    key = (net, contract.lower())
    if key not in _decimals_cache:
        dec = _rpc(url, "eth_call", [{"to": contract, "data": "0x313ce567"}, "latest"])
        _decimals_cache[key] = int(dec, 16)
    data = "0x70a08231" + address[2:].lower().rjust(64, "0")
    raw = _rpc(url, "eth_call", [{"to": contract, "data": data}, "latest"])
    return parse_hex_amount(raw, _decimals_cache[key])


SOLANA_RPC = "https://api.mainnet-beta.solana.com"


def _sol_native(address):
    return _rpc(SOLANA_RPC, "getBalance", [address])["value"] / 1e9


def _spl_token(address, mint):
    res = _rpc(SOLANA_RPC, "getTokenAccountsByOwner", [address, {"mint": mint}, {"encoding": "jsonParsed"}])
    return parse_spl_accounts(res)


def _xrp(address):
    try:
        return parse_xrpl(_rpc("https://xrplcluster.com", "account_info",
                               [{"account": address, "ledger_index": "validated"}]))
    except RuntimeError as e:
        if "actNotFound" in str(e):
            raise NotFound(address) from e
        raise


def _near(address):
    try:
        return parse_near(_rpc("https://free.rpc.fastnear.com", "query",
                               {"request_type": "view_account", "finality": "final", "account_id": address}))
    except RuntimeError as e:
        if "UNKNOWN_ACCOUNT" in str(e) or "does not exist" in str(e):
            raise NotFound(address) from e
        raise


def _aptos(address):
    j = _get(f"https://api.mainnet.aptoslabs.com/v1/accounts/{address}/balance/0x1::aptos_coin::AptosCoin")
    return int(j) / 1e8


def _fetch(symbol, address, family, network, contract):
    if family == "Bitcoin":
        return parse_esplora(_get(f"https://mempool.space/api/address/{address}"))
    if family == "Litecoin":
        return parse_esplora(_get(f"https://litecoinspace.org/api/address/{address}"))
    if family == "Dogecoin":
        return parse_blockcypher(_get(f"https://api.blockcypher.com/v1/doge/main/addrs/{address}/balance"))
    if family == "Bitcoin Cash":
        a = address.split(":")[-1]
        return parse_haskoin(_get(f"https://api.blockchain.info/haskoin-store/bch/address/{a}/balance"))
    if family == "Zcash":
        # 3xpl's keyless sandbox (Blockchair and the Trezor explorers now require keys / block scripts)
        return parse_3xpl(_get(f"https://sandbox-api.3xpl.com/zcash/address/{address}?data=balances"),
                          "zcash-main", "zcash", 8)
    if family == "EVM":
        net = EVM_NETWORK.get(network)
        if not net:
            return None
        return _evm_token(net, address, contract) if contract else _evm_native(net, address)
    if family == "Solana":
        return _spl_token(address, contract) if contract else _sol_native(address)
    if family == "TRON":
        j = _get(f"https://api.trongrid.io/v1/accounts/{address}")
        return parse_trongrid(j, contract or None, TRC20_DECIMALS.get(symbol, 18) if contract else 6)
    if family == "XRP Ledger":
        return _xrp(address)
    if family == "Stellar":
        return parse_horizon(_get(f"https://horizon.stellar.org/accounts/{address}"))
    if family == "Cosmos":
        return parse_cosmos_by_denom(_get(
            f"https://rest.cosmos.directory/cosmoshub/cosmos/bank/v1beta1/balances/{address}/by_denom?denom=uatom"), 6)
    if family == "Injective":
        return parse_cosmos_by_denom(_get(
            f"https://sentry.lcd.injective.network/cosmos/bank/v1beta1/balances/{address}/by_denom?denom=inj"), 18)
    if family == "NEAR":
        return _near(address)
    if family == "Aptos":
        return _aptos(address)
    if family == "Sui":
        # public fullnodes dropped JSON-RPC; Sui's GraphQL service is the keyless replacement
        q = 'query($a:SuiAddress!){ address(address:$a){ balance(coinType:"0x2::sui::SUI"){ totalBalance } } }'
        return parse_sui_graphql(_post("https://graphql.mainnet.sui.io/graphql", {"query": q, "variables": {"a": address}}))
    if family == "VeChain":
        return parse_vechain(_get(f"https://mainnet.vechain.org/accounts/{address}"))
    return None


def balance_for(symbol, address, family, network, contract=""):
    """Balance as a float, 0.0 for never-funded accounts, None when it couldn't be fetched."""
    try:
        return _fetch(symbol, address, family, network, contract or "")
    except NotFound:
        return 0.0
    except Exception as e:   # network errors, rate limits, schema changes: skip this cycle
        print(f"[BALANCES] {symbol} ({family}/{network}): {type(e).__name__}: {str(e)[:120]}")
        return None


def parse_addrs(text):
    """Device `addrs` output -> list of (symbol, address, family, network, contract)."""
    out = []
    for line in text.splitlines():
        parts = line.strip().split(" ", 3)
        if len(parts) < 3 or parts[0] != "ADDR":
            continue
        sym, addr = parts[1].upper(), parts[2]
        family, network, contract = "", "", ""
        if len(parts) == 4:
            rest = parts[3]
            fam_net, _, contract = rest.rpartition("|") if "|" in rest else (rest, "", "")
            # family names contain spaces ("XRP Ledger", "Bitcoin Cash"); match the known list
            for fam in ("Bitcoin Cash", "XRP Ledger", "Bitcoin", "EVM", "Solana", "Dogecoin", "Litecoin",
                        "TRON", "Cosmos", "Injective", "Stellar", "NEAR", "Aptos", "VeChain", "Sui", "Zcash"):
                if fam_net.startswith(fam + " ") or fam_net == fam:
                    family, network = fam, fam_net[len(fam):].strip()
                    break
        out.append((sym, addr, family, network, contract))
    return out


def refresh_all(entries, pause=0.25):
    """[(sym, addr, family, network, contract)] -> {sym: balance} (only balances actually fetched)."""
    result = {}
    for sym, addr, family, network, contract in entries:
        b = balance_for(sym, addr, family, network, contract)
        if b is not None:
            result[sym] = b
        time.sleep(pause)   # stay polite to the free endpoints
    return result
