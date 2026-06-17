SHELL := bash

BIN := build/somasafe-firmware.bin
SRC := $(wildcard main/*.c) $(wildcard main/*/*.c)
HDR := $(wildcard main/*.h) $(wildcard main/*/*.h)

.PHONY: run debug gfb qemu format
run:
	idf.py flash monitor

${BIN}: ${SRC} ${HDR}
	idf.py build

debug: ${BIN}
	TERM=xterm-color \
	idf.py openocd gdbtui

gdb:
	TERM=xterm-color idf.py gdbtui

qemu: ${BIN}
	idf.py qemu --gdb monitor

format:
	clang-format -i ${SRC} ${HDR}


test-model:
	uv run -m scripts.test_model models/feature-mlp/post-train-opti.tflite datasets --results=50

seria-write:
	uv run -m scripts.serial_write /dev/ttyACM1 datasets --rate=1000
