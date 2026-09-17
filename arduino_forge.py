#!/usr/bin/env python3
# noinspection SpellCheckingInspection
"""
Antigravity Arduino Forge — T-Dongle S3 Security Key & Crypto Vault Terminal
==============================================================================
Location: ~/Arduino Projects/tdongle-s3-security-key/arduino_forge.py
Hardware: LilyGo T-Dongle S3 (ESP32-S3, ST7735 0.96" TFT, WS2812 RGB LED)
"""

import argparse
import glob
import json
import os
import re
import select
import shutil
import subprocess
import sys
import textwrap
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

BOLD = "\033[1m"
DIM = "\033[2m"
CYAN = "\033[96m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
PURPLE = "\033[95m"
BLUE = "\033[94m"
RESET = "\033[0m"

APP_NAME = "Antigravity Security Key Forge"
APP_VERSION = "1.0.0"
PROJECT_DIR = Path("/home/mr-reaper/Arduino Projects/tdongle-s3-security-key")
MAIN_SKETCH = PROJECT_DIR / "tdongle-s3-security-key.ino"
ARDUINO_CLI = shutil.which("arduino-cli") or str(Path.home() / ".local" / "bin" / "arduino-cli")

TDONGLE_S3_FQBN = "esp32:esp32:esp32s3"
TDONGLE_S3_BOARD_OPTIONS = (
    "esp32:esp32:esp32s3:"
    "JTAGAdapter=default,"
    "PSRAM=disabled,"
    "FlashMode=qio,"
    "FlashSize=16M,"
    "LoopCore=1,"
    "EventsCore=1,"
    "USBMode=hwcdc,"
    "CDCOnBoot=cdc,"
    "MSCOnBoot=default,"
    "DFUOnBoot=default,"
    "UploadMode=default,"
    "PartitionScheme=default,"
    "CPUFreq=240,"
    "UploadSpeed=921600,"
    "DebugLevel=none,"
    "EraseFlash=none"
)

TDONGLE_PINS = {
    "TFT_CS": 4,
    "TFT_DC": 2,
    "TFT_RST": 1,
    "TFT_MOSI": 3,
    "TFT_SCLK": 5,
    "TFT_BACKLIGHT": 38,
    "LED_DATA": 40,
    "BUTTON": 0,
}


def detect_tdongle_port() -> Optional[str]:
    candidates = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    for port in candidates:
        try:
            dev_name = os.path.basename(port)
            vendor_path = f"/sys/class/tty/{dev_name}/device/../idVendor"
            if os.path.exists(vendor_path):
                with open(vendor_path) as f:
                    if f.read().strip() == "303a":
                        return port
        except (OSError, IOError):
            pass
    return candidates[0] if candidates else None


def compile_sketch(sketch_path: str, fqbn: str) -> bool:
    print(f"\n{CYAN}🔨 Compiling sketch in: {sketch_path}{RESET}")
    cmd = [ARDUINO_CLI, "compile", "--fqbn", fqbn, sketch_path]
    start = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    elapsed = time.time() - start
    if result.returncode == 0:
        print(f"{GREEN}✅ Compile OK ({elapsed:.1f}s){RESET}")
        for line in result.stdout.splitlines():
            if "Sketch uses" in line or "Global variables" in line:
                print(f"   {line.strip()}")
        return True
    else:
        print(f"{RED}❌ Compile FAILED ({elapsed:.1f}s){RESET}")
        for line in (result.stderr or result.stdout or "").splitlines():
            if "error:" in line.lower():
                print(f"   {RED}{line.strip()}{RESET}")
        return False


def upload_sketch(sketch_path: str, fqbn: str, port: str) -> bool:
    print(f"\n{PURPLE}⚡ Uploading to {port}...{RESET}")
    cmd = [ARDUINO_CLI, "upload", "--fqbn", fqbn, "--port", port, sketch_path]
    start = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    elapsed = time.time() - start
    if result.returncode == 0:
        print(f"{GREEN}✅ Upload OK ({elapsed:.1f}s){RESET}")
        # Automatically reset ESP32-S3 out of download mode into application execution
        subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port, "run"],
                       capture_output=True, text=True)
        print(f"{GREEN}🚀 Device reset & running firmware!{RESET}")
        return True
    else:
        print(f"{RED}❌ Upload FAILED ({elapsed:.1f}s){RESET}")
        for line in (result.stderr or result.stdout or "").splitlines()[-5:]:
            print(f"   {RED}{line.strip()}{RESET}")
        return False


def serial_monitor(port: str, baud: int = 115200) -> None:
    print(f"\n{CYAN}📡 Serial Monitor — {port} @ {baud} baud (Ctrl+C to exit){RESET}\n")
    try:
        import serial
        ser = serial.Serial(port, baud, timeout=0.1)
        try:
            while True:
                if ser.in_waiting:
                    data = ser.readline().decode("utf-8", errors="replace").rstrip()
                    if data:
                        print(f"{GREEN}← {data}{RESET}")
                if select.select([sys.stdin], [], [], 0.05)[0]:
                    user_input = sys.stdin.readline().strip()
                    if user_input:
                        ser.write((user_input + "\n").encode())
                        print(f"{BLUE}→ {user_input}{RESET}")
        finally:
            ser.close()
    except ImportError:
        subprocess.run([ARDUINO_CLI, "monitor", "--port", port, "--config", f"baudrate={baud}"])
    except KeyboardInterrupt:
        print(f"\n{DIM}Serial monitor closed.{RESET}")


def show_pinmap():
    print(f"""
{BOLD}LilyGo T-Dongle S3 — Hardware Pin Map{RESET}
{'─' * 45}
{CYAN}TFT Display (ST7735 0.96" 80×160):{RESET}
  CS    → GPIO {TDONGLE_PINS['TFT_CS']}
  DC    → GPIO {TDONGLE_PINS['TFT_DC']}
  RST   → GPIO {TDONGLE_PINS['TFT_RST']}
  MOSI  → GPIO {TDONGLE_PINS['TFT_MOSI']}
  SCLK  → GPIO {TDONGLE_PINS['TFT_SCLK']}
  BL    → GPIO {TDONGLE_PINS['TFT_BACKLIGHT']}

{GREEN}RGB LED (WS2812 × 1):{RESET}
  DATA  → GPIO {TDONGLE_PINS['LED_DATA']}

{YELLOW}Button:{RESET}
  BOOT  → GPIO {TDONGLE_PINS['BUTTON']} (active LOW, internal pullup)

{PURPLE}USB:{RESET}
  Mode  → Hardware CDC + HID
  VID   → 0x303A (Espressif)
  PID   → 0x1001
""")


def main():
    parser = argparse.ArgumentParser(description="Antigravity Security Key Forge — LilyGo T-Dongle S3 CLI")
    parser.add_argument("--detect", action="store_true", help="Detect T-Dongle S3 and print serial port")
    parser.add_argument("--flash", nargs="?", const=str(PROJECT_DIR), help="Compile and upload sketch")
    parser.add_argument("--compile", nargs="?", const=str(PROJECT_DIR), help="Compile sketch")
    parser.add_argument("--monitor", action="store_true", help="Open serial monitor")
    parser.add_argument("--pins", action="store_true", help="Show hardware pin map")
    parser.add_argument("--port", help="Serial port override")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")

    args = parser.parse_args()

    if args.pins:
        show_pinmap()
        return

    port = args.port or detect_tdongle_port()

    if args.detect:
        if port:
            print(port)
            sys.exit(0)
        else:
            print("NOT_FOUND", file=sys.stderr)
            sys.exit(1)

    if args.compile:
        target = args.compile or str(PROJECT_DIR)
        ok = compile_sketch(target, TDONGLE_S3_BOARD_OPTIONS)
        sys.exit(0 if ok else 1)

    if args.flash:
        target = args.flash or str(PROJECT_DIR)
        if not port:
            print(f"{RED}No device detected.{RESET}", file=sys.stderr)
            sys.exit(1)
        ok = compile_sketch(target, TDONGLE_S3_BOARD_OPTIONS)
        if ok:
            ok = upload_sketch(target, TDONGLE_S3_BOARD_OPTIONS, port)
        sys.exit(0 if ok else 1)

    if args.monitor:
        if not port:
            print(f"{RED}No device detected.{RESET}", file=sys.stderr)
            sys.exit(1)
        serial_monitor(port, args.baud)
        return

    # Default interactive
    print(f"{PURPLE}⚡ Antigravity Security Key Forge{RESET} — Project: {CYAN}{PROJECT_DIR}{RESET}")
    if port:
        print(f"{GREEN}🔌 T-Dongle S3 connected on {port}{RESET}")
    show_pinmap()


if __name__ == "__main__":
    main()
