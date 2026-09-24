#!/usr/bin/env python3
"""
Crypto TKey S3 — FIDO2 hardware conformance suite.

Drives the real key over USB HID with python-fido2's WebAuthn client and verifies
every response with python-fido2's relying-party server (signatures, flags, counters).

User presence:
  * firmware built with -DTKEY_TEST_SERIAL_TOUCH: approvals are sent as "touch" over CDC
  * production firmware: press the button when the screen asks

The suite sets a throwaway FIDO PIN and registers test credentials. With --reset it ends
with authenticatorReset (needs a replug / reset within 10 s), wiping every FIDO
credential on the key, so only use --reset before real accounts are enrolled.

  pip install fido2 pyserial
  python3 tools/test_fido2_hw.py [--port /dev/ttyACM0] [--reset]
"""
import argparse
import sys
import threading
import time

import serial
from fido2.client import ClientError, DefaultClientDataCollector, Fido2Client, UserInteraction
from fido2.ctap import CtapError
from fido2.ctap1 import Ctap1
from fido2.ctap2 import Ctap2
from fido2.ctap2.pin import ClientPin
from fido2.hid import CtapHidDevice
from fido2.server import Fido2Server
from fido2.webauthn import (PublicKeyCredentialRpEntity, PublicKeyCredentialUserEntity,
                            ResidentKeyRequirement, UserVerificationRequirement)

TEST_PIN = "tkey-test-4821"
results = []


def check(cond, name, detail=""):
    results.append((bool(cond), name))
    print(f"  {'PASS' if cond else 'FAIL'}  {name}{('  — ' + detail) if detail and not cond else ''}")
    return cond


class Presence(UserInteraction):
    """Approves touch prompts over serial (test firmware) or asks for the button."""

    def __init__(self, port, pin=None):
        self.port = port
        self.pin = pin
        self.deny = False

    def prompt_up(self):
        if self.deny:
            return
        if self.port:
            self.port.write(b"touch\n")
            self.port.flush()
        else:
            print("    👆 press the TKey button")

    def request_pin(self, permissions, rp_id):
        return self.pin

    def request_uv(self, permissions, rp_id):
        return True


def find_device():
    for dev in CtapHidDevice.list_devices():
        if dev.descriptor.vid == 0x303A:
            return dev
    raise SystemExit("Crypto TKey S3 not found (is /dev/hidraw* accessible?)")


def open_serial(path):
    try:
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = path, 115200, 0.1
        s.rts, s.dtr = False, True  # never assert reset on open
        s.open()
        return s
    except Exception as e:
        print(f"(no serial control: {e}; press the button when prompted)")
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--no-serial", action="store_true", help="always wait for the physical button")
    ap.add_argument("--reset", action="store_true", help="finish with authenticatorReset")
    args = ap.parse_args()

    ser = None if args.no_serial else open_serial(args.port)
    dev = find_device()
    ctap2 = Ctap2(dev)
    presence = Presence(ser)

    origin = "https://webauthn.io"
    rp = PublicKeyCredentialRpEntity(id="webauthn.io", name="WebAuthn.io")
    server = Fido2Server(rp)
    client = Fido2Client(dev, DefaultClientDataCollector(origin), presence)

    print("\n[1] Transport & getInfo")
    check(dev.ping(b"tkey" * 40) == b"tkey" * 40, "CTAPHID PING, 160 bytes over continuation packets")
    info = ctap2.get_info()
    check("FIDO_2_0" in info.versions and "U2F_V2" in info.versions, "versions FIDO_2_0 + U2F_V2", str(info.versions))
    check(info.options.get("rk") is True, "resident keys (passkeys) advertised")
    pin_was_set = bool(info.options.get("clientPin"))
    print(f"    FIDO PIN currently {'set' if pin_was_set else 'not set'}; max msg {info.max_msg_size}")

    print("\n[2] Security-key style (non-resident, no UV)")
    user = PublicKeyCredentialUserEntity(id=b"hw-user-1", name="imnotmrreaper", display_name="Mr Reaper")
    if pin_was_set:
        presence.pin = TEST_PIN
    opts, state = server.register_begin(user, resident_key_requirement=ResidentKeyRequirement.DISCOURAGED,
                                        user_verification=UserVerificationRequirement.DISCOURAGED)
    reg = client.make_credential(opts.public_key)
    ad = server.register_complete(state, reg)
    cred = ad.credential_data
    check(ad.is_user_present(), "register: server verified packed attestation + UP flag")
    att = reg.response.attestation_object
    check(att.fmt == "packed" and "x5c" in att.att_stmt, "packed attestation with x5c certificate")

    opts, state = server.authenticate_begin([cred], user_verification=UserVerificationRequirement.DISCOURAGED)
    resp = client.get_assertion(opts.public_key).get_response(0)
    server.authenticate_complete(state, [cred], resp)
    c1 = resp.response.authenticator_data.counter
    check(True, "sign-in with allowList verified by server")

    # allowList with foreign credential IDs first (other keys on the same account)
    from fido2.webauthn import PublicKeyCredentialDescriptor, PublicKeyCredentialType
    foreign = [PublicKeyCredentialDescriptor(type=PublicKeyCredentialType.PUBLIC_KEY, id=bytes([i]) * 64) for i in range(3)]
    opts, state = server.authenticate_begin(foreign + [cred], user_verification=UserVerificationRequirement.DISCOURAGED)
    resp = client.get_assertion(opts.public_key).get_response(0)
    server.authenticate_complete(state, [cred], resp)
    check(resp.raw_id == cred.credential_id, "picks our credential out of a multi-key allowList")
    check(resp.response.authenticator_data.counter > c1, "signature counter increases")

    print("\n[3] Protocol edge cases")
    opts, _ = server.register_begin(user, [cred], user_verification=UserVerificationRequirement.DISCOURAGED)
    try:
        client.make_credential(opts.public_key)
        check(False, "excludeList blocks re-registration")
    except ClientError as e:
        check(e.code == ClientError.ERR.DEVICE_INELIGIBLE, "excludeList blocks re-registration", str(e))

    rp_hash = __import__("hashlib").sha256(b"webauthn.io").digest()
    silent = ctap2.get_assertion("webauthn.io", b"\x01" * 32,
                                 [{"type": "public-key", "id": cred.credential_id}], options={"up": False})
    check(silent.auth_data.flags & 0x01 == 0, "silent up=false probe answers without a touch")

    try:
        ctap2.get_assertion("not-registered.example", b"\x02" * 32,
                            [{"type": "public-key", "id": cred.credential_id}], options={"up": False})
        check(False, "credential scoped to its RP ID")
    except CtapError as e:
        check(e.code == CtapError.ERR.NO_CREDENTIALS, "credential scoped to its RP ID", str(e))

    presence.deny = True
    cancel = threading.Event()
    threading.Timer(2.0, cancel.set).start()
    t0 = time.time()
    try:
        ctap2.get_assertion("webauthn.io", b"\x03" * 32, [{"type": "public-key", "id": cred.credential_id}],
                            event=cancel)
        check(False, "browser cancel aborts the touch prompt")
    except CtapError as e:
        check(e.code == CtapError.ERR.KEEPALIVE_CANCEL and time.time() - t0 < 6,
              "browser cancel aborts the touch prompt", f"{e} after {time.time() - t0:.1f}s")
    presence.deny = False
    time.sleep(1.2)

    print("\n[4] FIDO PIN (clientPIN protocol 1)")
    cp = ClientPin(ctap2)
    if not pin_was_set:
        cp.set_pin(TEST_PIN)
        check(ctap2.get_info().options.get("clientPin") is True, "setPin; getInfo now reports clientPin")
    presence.pin = TEST_PIN
    retries = cp.get_pin_retries()[0]
    try:
        cp.get_pin_token("wrong-pin-000")
        check(False, "wrong PIN rejected")
    except CtapError as e:
        check(e.code == CtapError.ERR.PIN_INVALID and cp.get_pin_retries()[0] == retries - 1,
              "wrong PIN rejected, one retry spent", str(e))
    cp.get_pin_token(TEST_PIN)
    check(cp.get_pin_retries()[0] == 8, "correct PIN restores retries to 8")
    cp.change_pin(TEST_PIN, TEST_PIN + "x")
    cp.change_pin(TEST_PIN + "x", TEST_PIN)
    check(True, "changePin round trip")

    print("\n[5] Passkeys (resident + PIN user verification)")
    passkeys = []
    for i, name in enumerate(["alice@example", "bob@example"]):
        u = PublicKeyCredentialUserEntity(id=f"pk-user-{i}".encode(), name=name, display_name=name.title())
        opts, state = server.register_begin(u, resident_key_requirement=ResidentKeyRequirement.REQUIRED,
                                            user_verification=UserVerificationRequirement.REQUIRED)
        ad = server.register_complete(state, client.make_credential(opts.public_key))
        passkeys.append(ad.credential_data)
        check(ad.is_user_verified(), f"passkey {name} registered with UV")

    opts, state = server.authenticate_begin(user_verification=UserVerificationRequirement.REQUIRED)
    sel = client.get_assertion(opts.public_key)
    handles = set()
    for i in range(len(sel.get_assertions())):
        r = sel.get_response(i)
        server.authenticate_complete(state, passkeys, r)
        handles.add(r.response.user_handle)
    check(handles == {b"pk-user-0", b"pk-user-1"}, "username-less sign-in lists both accounts (getNextAssertion)",
          str(handles))
    names = {a.user.get("name") for a in sel.get_assertions()}
    check(names == {"alice@example", "bob@example"}, "account names returned for the account picker", str(names))

    print("\n[6] Legacy U2F (CTAP1)")
    ctap1 = Ctap1(dev)
    app = __import__("hashlib").sha256(b"https://u2f.example").digest()
    chal = b"\x05" * 32
    for _ in range(3):
        try:
            reg1 = ctap1.register(chal, app)
            break
        except Exception:
            presence.prompt_up()
            time.sleep(0.5)
    reg1.verify(app, chal)
    check(True, "U2F register, attestation signature verifies")
    for _ in range(3):
        try:
            sig = ctap1.authenticate(chal, app, reg1.key_handle)
            break
        except Exception:
            presence.prompt_up()
            time.sleep(0.5)
    sig.verify(app, chal, reg1.public_key)
    check(True, "U2F authenticate signature verifies")

    if args.reset:
        print("\n[7] authenticatorReset (clean slate)")
        input("    Replug the key, then press Enter within 5 seconds of it booting... ")
        dev = find_device()
        ser2 = open_serial(args.port) if ser else None
        presence.port = ser2
        if ser2:
            threading.Timer(0.8, presence.prompt_up).start()
        Ctap2(dev).reset()
        check(Ctap2(dev).get_info().options.get("clientPin") is False, "reset clears PIN and passkeys")

    ok = sum(r for r, _ in results)
    print(f"\n{ok}/{len(results)} checks passed")
    sys.exit(0 if ok == len(results) else 1)


if __name__ == "__main__":
    main()
