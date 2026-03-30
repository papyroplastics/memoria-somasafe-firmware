
SHELL := bash

BIN := build/example.bin
SRC := $(wildcard main/*.c)
HDR := $(wildcard main/include/*.h)

run: ${BIN}
	idf.py flash monitor

debug: ${BIN}
	TERM=xterm-color \
	idf.py openocd gdbtui

gdb:
	TERM=xterm-color idf.py gdbtui

qemu: ${BIN}
	idf.py qemu --gdb monitor

${BIN}: ${SRC} ${HDR}
	idf.py build

format:
	clang-format -i ${SRC} ${HDR}

