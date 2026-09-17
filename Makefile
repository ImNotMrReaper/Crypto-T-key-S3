# Antigravity Arduino Forge — T-Dongle S3 Security Key Makefile

PORT ?= $(shell ./arduino_forge.py --detect 2>/dev/null || echo "/dev/ttyACM0")

.PHONY: all flash compile monitor pins clean agy

all: flash

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

agy:
	cd "$$(pwd)" && agy -c
