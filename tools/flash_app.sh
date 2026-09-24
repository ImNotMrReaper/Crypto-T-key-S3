#!/usr/bin/env bash
# Flash only the application partition (0x10000) of the Crypto TKey S3.
#
# Why not `arduino-cli upload`: its automatic reset into the ROM bootloader fails through
# the composite TinyUSB CDC+HID stack ("No serial data received"). This does the 1200-baud
# touch, waits for the ROM's USB-JTAG port, and writes the app partition only; the NVS
# partition (setup, wallet config, FIDO master secret, passkeys) is never touched.
#
#   tools/flash_app.sh [build-dir] [serial-port]
#   (build-dir must contain crypto-tkey-s3.ino.bin from `arduino-cli compile --output-dir`)
set -euo pipefail
BUILD="${1:-build}"
PORT="${2:-$(ls /dev/ttyACM* 2>/dev/null | head -1)}"
BIN="$BUILD/crypto-tkey-s3.ino.bin"
ESPTOOL="$(command -v esptool || echo "$HOME/.local/bin/esptool")"
[ -f "$BIN" ] || { echo "missing $BIN"; exit 1; }
[ -n "$PORT" ] || { echo "no /dev/ttyACM* port found"; exit 1; }

systemctl --user stop tkey-tracker.service 2>/dev/null || true

echo "⚡ 1200-baud touch on $PORT → ROM download mode"
python3 - "$PORT" <<'PY' || true
import serial, sys, time
try:
    s = serial.Serial(sys.argv[1], 1200); time.sleep(0.2); s.close()
except Exception:
    pass
PY
for _ in $(seq 20); do
    lsusb | grep -q "303a:1001 Espressif USB JTAG" && break
    sleep 0.5
done
# the ROM's USB-Serial-JTAG port needs a moment before udev grants access
for _ in $(seq 20); do
    PORT="$(ls /dev/ttyACM* 2>/dev/null | head -1)"
    [ -n "$PORT" ] && python3 -c "import serial,sys; serial.Serial(sys.argv[1]).close()" "$PORT" 2>/dev/null && break
    sleep 0.5
done

"$ESPTOOL" --chip esp32s3 --port "$PORT" --before no_reset --after no_reset write_flash 0x10000 "$BIN"
"$ESPTOOL" --chip esp32s3 --port "$PORT" --before no_reset --after watchdog_reset chip_id >/dev/null
sleep 6
systemctl --user start tkey-tracker.service 2>/dev/null || true
echo "✅ App flashed; NVS (settings, wallet, passkeys) untouched. Port: $(ls /dev/ttyACM* 2>/dev/null | head -1)"
