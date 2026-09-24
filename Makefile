# Crypto T-Key S3 — Arduino Forge
# LilyGo T-Dongle S3 FIDO2 security key and offline crypto vault

PORT ?= $(shell ./arduino_forge.py --detect 2>/dev/null || echo "/dev/ttyACM0")

.PHONY: all flash compile monitor pins detect clean agy

all: compile

compile:
	./arduino_forge.py --compile

flash:
	./arduino_forge.py --flash

monitor:
	./arduino_forge.py --monitor

pins:
	./arduino_forge.py --pins

detect:
	./arduino_forge.py --detect

clean:
	rm -rf build

agy:
	cd "$$(pwd)" && agy -c
