#!/usr/bin/env bash
# Crypto TKey S3 — one entry point for every test stage.
#
#   tools/run_all_tests.sh                 host unit tests + both firmware builds (no device needed)
#   tools/run_all_tests.sh --hw-safe       + read-only checks against the flashed key (no state change)
#   tools/run_all_tests.sh --hw-full       + the conformance suites that CHANGE the key (throwaway FIDO
#                                            PIN / credentials / seed). Only on a TEST build that holds
#                                            nothing you need; asks for confirmation.
#
# Stages stop at the first failure. The hardware suites use ~/.venvs/tkey-tests (fido2, pyserial).
set -euo pipefail
cd "$(dirname "$0")/.."

HW=none
for a in "$@"; do
    case "$a" in
        --hw-safe) HW=safe ;;
        --hw-full) HW=full ;;
        -h|--help) sed -n 2,11p "$0"; exit 0 ;;
        *) echo "unknown option $a"; exit 2 ;;
    esac
done

PY="${TKEY_PY:-$HOME/.venvs/tkey-tests/bin/python}"
PORT="${TKEY_PORT:-$(ls /dev/ttyACM* 2>/dev/null | head -1 || true)}"
OUT="${TKEY_BUILD_OUT:-/tmp/tkey-build}"
pass=0

stage() { printf '\n\033[1;35m== %s ==\033[0m\n' "$1"; }
ok()    { printf '\033[32m✔ %s\033[0m\n' "$1"; pass=$((pass + 1)); }

stage "1. Host unit tests (ASan + UBSan)"
make -s -C tests/host
ok "host unit tests"

stage "2. Firmware builds (production + test)"
build() {  # $1 = name, $2 = extra C++ flags
    local log="$OUT/$1.log"
    mkdir -p "$OUT/$1"
    if ! arduino-cli compile --warnings all --build-path "$OUT/$1-cache" --output-dir "$OUT/$1" \
            --build-property "compiler.cpp.extra_flags=$2" . >"$log" 2>&1; then
        tail -30 "$log"; echo "build $1 failed (log: $log)"; exit 1
    fi
    # New warnings in project files fail the build (printf %d on uint32_t is the known exception)
    local warn
    warn=$(grep -E "crypto-tkey-s3/.*warning:" "$log" | grep -v -e "-Wformat=" \
           -e "wall_ticker.cpp:125" | sort -u || true)
    if [ -n "$warn" ]; then echo "$warn"; echo "new warnings in $1 build"; exit 1; fi
    grep -E "^Sketch uses|^Global variables" "$log"
    # RAM budget: static RAM above this left too little heap to boot (crash loop, 2026-09-25)
    local ram
    ram=$(sed -n 's/^Global variables use \([0-9]*\) bytes.*/\1/p' "$log")
    if [ "${ram:-0}" -gt 165000 ]; then echo "$1 build uses $ram bytes of static RAM (budget 165000)"; exit 1; fi
}
build prod ""
ok "production build"
build test "-DTKEY_TEST_SERIAL_TOUCH"
ok "test build"
# The production image must not contain the serial-approval hooks
if strings "$OUT/prod/crypto-tkey-s3.ino.bin" | grep -q "Simulated User Presence"; then
    echo "production image contains the serial touch hook"; exit 1
fi
ok "production image has no test hooks"

if [ "$HW" != none ]; then
    [ -n "$PORT" ] || { echo "no /dev/ttyACM* device found"; exit 1; }
    stage "3. Hardware: read-only checks on $PORT"
    "$PY" tools/test_smoke_hw.py --port "$PORT"
    ok "hardware smoke (read-only)"
fi

if [ "$HW" = full ]; then
    stage "4. Hardware: state-changing conformance suites"
    echo "These set a throwaway FIDO PIN, register credentials and replace the wallet seed."
    read -r -p "Is the key running a TEST build with nothing you need on it? [type YES] " ans
    [ "$ans" = YES ] || { echo "skipped"; exit 1; }
    "$PY" tools/test_fido2_hw.py --port "$PORT";       ok "FIDO2 conformance"
    "$PY" tools/test_hmac_secret_hw.py --port "$PORT"; ok "hmac-secret"
    "$PY" tools/test_wallet_hw.py;                     ok "wallet persistence"
fi

printf '\n\033[1;32mAll %d stages passed.\033[0m\n' "$pass"
