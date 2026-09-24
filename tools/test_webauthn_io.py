#!/usr/bin/env python3
"""
Live test against https://webauthn.io's real relying-party server.

python-fido2 plays the browser (origin https://webauthn.io) and talks to the same
JSON endpoints webauthn.io's page uses; webauthn.io's server does the verification.
Press the TKey button when prompted (twice: register, then sign in).

  pip install fido2 requests
  python3 tools/test_webauthn_io.py [--attestation direct] [--discoverable]
"""
import argparse
import secrets
import sys

import requests
from fido2.client import DefaultClientDataCollector, Fido2Client, UserInteraction
from fido2.hid import CtapHidDevice
from fido2.webauthn import PublicKeyCredentialCreationOptions, PublicKeyCredentialRequestOptions

BASE = "https://webauthn.io"


class Button(UserInteraction):
    def prompt_up(self):
        print("    👆 Press the TKey button now")

    def request_pin(self, permissions, rp_id):
        import getpass
        return getpass.getpass("    FIDO PIN: ")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--attestation", default="direct", choices=["none", "indirect", "direct"])
    ap.add_argument("--discoverable", action="store_true", help="register a passkey (resident key)")
    args = ap.parse_args()

    dev = next((d for d in CtapHidDevice.list_devices() if d.descriptor.vid == 0x303A), None)
    if not dev:
        sys.exit("Crypto TKey S3 not found")
    client = Fido2Client(dev, DefaultClientDataCollector(BASE), Button())
    http = requests.Session()
    http.get(BASE)
    username = f"tkey-s3-{secrets.token_hex(3)}"
    print(f"webauthn.io username: {username}")

    print("\n[register]")
    opts = http.post(f"{BASE}/registration/options", json={
        "username": username, "user_verification": "discouraged" if not args.discoverable else "preferred",
        "attestation": args.attestation, "attachment": "all", "algorithms": ["es256"],
        "discoverable_credential": "required" if args.discoverable else "discouraged",
        "hints": ["security-key"],
    }).json()
    reg = client.make_credential(PublicKeyCredentialCreationOptions.from_dict(opts))
    ver = http.post(f"{BASE}/registration/verification",
                    json={"username": username, "response": dict(reg)}).json()
    print(f"    webauthn.io says: {ver}")
    if not ver.get("verified"):
        sys.exit(1)

    print("\n[sign in]")
    opts = http.post(f"{BASE}/authentication/options", json={
        "username": None if args.discoverable else username,
        "user_verification": "discouraged" if not args.discoverable else "preferred",
        "hints": ["security-key"],
    }).json()
    auth = client.get_assertion(PublicKeyCredentialRequestOptions.from_dict(opts)).get_response(0)
    ver = http.post(f"{BASE}/authentication/verification",
                    json={"username": username, "response": dict(auth)}).json()
    print(f"    webauthn.io says: {ver}")
    sys.exit(0 if ver.get("verified") else 1)


if __name__ == "__main__":
    main()
