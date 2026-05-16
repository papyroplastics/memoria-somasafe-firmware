
SHELL := bash

BIN := build/embed-somasafe.bin
SRC := $(wildcard main/*.c) $(wildcard main/*/*.c)
HDR := $(wildcard main/*.h) $(wildcard main/*/*.h)

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

