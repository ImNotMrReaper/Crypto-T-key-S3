#!/usr/bin/env python3
"""
Crypto TKey S3 — hmac-secret extension test (what LUKS/systemd-cryptenroll relies on).

Registers a throwaway credential with hmac-secret, then checks that the key returns a
deterministic 32-byte secret per (credential, salt), different secrets for different
salts/credentials, and correct two-salt output. Approvals over serial on a
-DTKEY_TEST_SERIAL_TOUCH build, otherwise press the button.

  python3 tools/test_hmac_secret_hw.py [--port /dev/ttyACM1]
"""
import argparse
import os
import sys

from fido2.client import DefaultClientDataCollector, Fido2Client
from fido2.ctap2.extensions import HmacSecretExtension
from fido2.server import Fido2Server
from fido2.utils import websafe_decode
from fido2.webauthn import (PublicKeyCredentialRpEntity, PublicKeyCredentialUserEntity,
                            ResidentKeyRequirement, UserVerificationRequirement)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_fido2_hw import Presence, check, find_device, open_serial, results  # noqa: E402


def secret(client, server, cred, salt1, salt2=None):
    opts, state = server.authenticate_begin([cred], user_verification=UserVerificationRequirement.DISCOURAGED)
    ext = {"hmacGetSecret": {"salt1": salt1, **({"salt2": salt2} if salt2 else {})}}
    pk = opts.public_key
    pk = type(pk)(challenge=pk.challenge, timeout=pk.timeout, rp_id=pk.rp_id,
                  allow_credentials=pk.allow_credentials, user_verification=pk.user_verification,
                  extensions=ext)
    resp = client.get_assertion(pk).get_response(0)
    server.authenticate_complete(state, [cred], resp)  # signature still verifies with extension data
    out = resp.client_extension_results.get("hmacGetSecret") or {}
    # python-fido2 returns JSON-style (base64url) values; decode to raw bytes
    dec = lambda v: websafe_decode(v) if isinstance(v, str) else v
    return dec(out.get("output1")), dec(out.get("output2"))


def register(client, server, name):
    opts, state = server.register_begin(PublicKeyCredentialUserEntity(id=name.encode(), name=name),
                                        resident_key_requirement=ResidentKeyRequirement.DISCOURAGED,
                                        user_verification=UserVerificationRequirement.DISCOURAGED)
    pk = opts.public_key
    pk = type(pk)(rp=pk.rp, user=pk.user, challenge=pk.challenge, pub_key_cred_params=pk.pub_key_cred_params,
                  timeout=pk.timeout, authenticator_selection=pk.authenticator_selection,
                  attestation=pk.attestation, extensions={"hmacCreateSecret": True})
    reg = client.make_credential(pk)
    ad = server.register_complete(state, reg)
    return ad.credential_data, reg.client_extension_results.get("hmacCreateSecret")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    args = ap.parse_args()
    ser = open_serial(args.port)
    dev = find_device()
    rp = PublicKeyCredentialRpEntity(id="luks.example", name="hmac-secret test")
    server = Fido2Server(rp)
    # legacy hmacCreateSecret/hmacGetSecret inputs are opt-in in python-fido2
    client = Fido2Client(dev, DefaultClientDataCollector("https://luks.example"), Presence(ser),
                         extensions=[HmacSecretExtension(allow_hmac_secret=True)])

    print("[1] Registration")
    cred_a, created = register(client, server, "disk-a")
    check(created is True, "makeCredential reports hmacCreateSecret=true")
    cred_b, _ = register(client, server, "disk-b")

    print("\n[2] Secrets")
    s1, s2 = os.urandom(32), os.urandom(32)
    a1, _ = secret(client, server, cred_a, s1)
    check(a1 is not None and len(a1) == 32, "32-byte secret returned, assertion signature still valid")
    a1_again, _ = secret(client, server, cred_a, s1)
    check(a1 == a1_again, "same credential + same salt -> same secret (deterministic)")
    a2, _ = secret(client, server, cred_a, s2)
    check(a2 != a1, "different salt -> different secret")
    b1, _ = secret(client, server, cred_b, s1)
    check(b1 != a1, "different credential -> different secret")
    x1, x2 = secret(client, server, cred_a, s1, s2)
    check(x1 == a1 and x2 == a2, "two-salt request returns both outputs correctly")

    ok = sum(r for r, _ in results)
    print(f"\n{ok}/{len(results)} checks passed")
    sys.exit(0 if ok == len(results) else 1)


if __name__ == "__main__":
    main()
