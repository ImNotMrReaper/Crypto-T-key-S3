#!/usr/bin/env python3
# noinspection SpellCheckingInspection
"""
Crypto T-Key S3 — Arduino Forge
================================
Build, flash, monitor, and inspect the LilyGo T-Dongle S3 security key.
"""

import argparse
import glob
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

BOLD = "\033[1m"
DIM = "\033[2m"
CYAN = "\033[96m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
PURPLE = "\033[95m"
BLUE = "\033[94m"
RESET = "\033[0m"

APP_NAME = "Crypto T-Key S3 Forge"
APP_VERSION = "1.1.0"
PROJECT_DIR = Path(__file__).resolve().parent
MAIN_SKETCH = PROJECT_DIR / "crypto-tkey-s3.ino"
ARDUINO_CLI = shutil.which("arduino-cli") or str(Path.home() / ".local" / "bin" / "arduino-cli")

TDONGLE_S3_BOARD_OPTIONS = (
    "esp32:esp32:esp32s3:"
    "JTAGAdapter=default,PSRAM=disabled,FlashMode=qio,FlashSize=16M,"
    "LoopCore=1,EventsCore=1,USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,"
    "DFUOnBoot=default,UploadMode=default,PartitionScheme=huge_app,CPUFreq=160,"
    "UploadSpeed=921600,DebugLevel=none,EraseFlash=none"
)

TDONGLE_PINS = {
    "TFT_CS": 4, "TFT_DC": 2, "TFT_RST": 1, "TFT_MOSI": 3,
    "TFT_SCLK": 5, "TFT_BACKLIGHT": 38, "LED_DATA": 40, "BUTTON": 0,
}


def detect_tdongle_port() -> Optional[str]:
    candidates = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    for port in candidates:
        try:
            dev_name = os.path.basename(port)
            vendor_path = f"/sys/class/tty/{dev_name}/device/../idVendor"
            if os.path.exists(vendor_path) and open(vendor_path).read().strip() == "303a":
                return port
        except (OSError, IOError):
            pass
    return candidates[0] if candidates else None


def compile_sketch(sketch_path: str, fqbn: str) -> bool:
    print(f"\n{CYAN}🔨 Compiling Crypto T-Key S3: {sketch_path}{RESET}")
    result = subprocess.run([ARDUINO_CLI, "compile", "--fqbn", fqbn, sketch_path], capture_output=True, text=True)
    if result.returncode == 0:
        print(f"{GREEN}✅ Compile OK{RESET}")
        return True
    print(f"{RED}❌ Compile FAILED{RESET}")
    print((result.stderr or result.stdout or "").strip())
    return False


def upload_sketch(sketch_path: str, fqbn: str, port: str) -> bool:
    print(f"\n{PURPLE}⚡ Uploading to {port}...{RESET}")
    result = subprocess.run([ARDUINO_CLI, "upload", "--fqbn", fqbn, "--port", port, sketch_path], capture_output=True, text=True)
    if result.returncode == 0:
        print(f"{GREEN}✅ Upload OK{RESET}")
        return True
    print(f"{RED}❌ Upload FAILED{RESET}")
    print((result.stderr or result.stdout or "").strip())
    return False


def show_pinmap():
    print(f"""\n{BOLD}LilyGo T-Dongle S3 — Hardware Pin Map{RESET}
TFT: CS GPIO 4 | DC GPIO 2 | RST GPIO 1 | MOSI GPIO 3 | SCLK GPIO 5 | BL GPIO 38
RGB LED: DATA GPIO 40
Button: BOOT GPIO 0 (active LOW, internal pullup)
USB: Hardware CDC + HID | VID 0x303A | PID 0x1001
""")


def main():
    parser = argparse.ArgumentParser(description=f"{APP_NAME} CLI")
    parser.add_argument("--detect", action="store_true")
    parser.add_argument("--flash", nargs="?", const=str(PROJECT_DIR))
    parser.add_argument("--compile", nargs="?", const=str(PROJECT_DIR))
    parser.add_argument("--monitor", action="store_true")
    parser.add_argument("--pins", action="store_true")
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    if args.pins:
        show_pinmap(); return
    port = args.port or detect_tdongle_port()
    if args.detect:
        print(port or "NOT_FOUND")
        sys.exit(0 if port else 1)
    if args.compile:
        sys.exit(0 if compile_sketch(args.compile, TDONGLE_S3_BOARD_OPTIONS) else 1)
    if args.flash:
        if not port: print(f"{RED}No device detected.{RESET}", file=sys.stderr); sys.exit(1)
        ok = compile_sketch(args.flash, TDONGLE_S3_BOARD_OPTIONS) and upload_sketch(args.flash, TDONGLE_S3_BOARD_OPTIONS, port)
        sys.exit(0 if ok else 1)
    if args.monitor:
        if not port: print(f"{RED}No device detected.{RESET}", file=sys.stderr); sys.exit(1)
        subprocess.run([ARDUINO_CLI, "monitor", "--port", port, "--config", f"baudrate={args.baud}"])
        return
    print(f"{PURPLE}{APP_NAME}{RESET} v{APP_VERSION} — {PROJECT_DIR}")
    show_pinmap()


if __name__ == "__main__":
    main()
