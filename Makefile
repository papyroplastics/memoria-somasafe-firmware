SHELL := bash

BIN := build/somasafe-firmware.bin
SRC := $(wildcard main/*.c) $(wildcard main/*/*.c)
HDR := $(wildcard main/*.h) $(wildcard main/*/*.h)

serial_dir := /dev/serial/by-id
port_usb  := ${serial_dir}/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:B8:BD:68-if00
port_uart := ${serial_dir}/usb-1a86_USB_Single_Serial_5A79064524-if00

.PHONY: monitor debug gfb qemu format test-model seria-write
monitor:
	idf.py -p ${port_usb} flash monitor

debug: ${BIN}
	TERM=xterm-color \
		idf.py -p ${port_usb} openocd gdbtui

gdb:
	TERM=xterm-color \
		idf.py -p ${port_usb} gdbtui

qemu: ${BIN}
	idf.py qemu --gdb monitor

format:
	clang-format -i ${SRC} ${HDR}

test-model:
	uv run -m scripts.test_model models/feature-mlp/post-train-opti.tflite datasets --results=100

seria-write:
	uv run -m scripts.serial_write ${port_uart} datasets --rate=5000
