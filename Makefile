SHELL := bash

shared_repo := https://github.com/papyroplastics/memoria-somasafe-shared.git

BIN := build/somasafe-firmware.bin
SRC := $(wildcard main/*.c) $(wildcard main/*/*.c)
HDR := $(wildcard main/*.h) $(wildcard main/*/*.h)

serial_dir := /dev/serial/by-id
port_usb  := ${serial_dir}/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:B8:BD:68-if00
port_uart := ${serial_dir}/usb-1a86_USB_Single_Serial_5A79064524-if00

.PHONY: shared monitor debug gfb qemu format test-model seria-write
shared:
	@if [ -e shared ] || [ -L shared ]; then \
		echo "shared already present"; \
	elif [ -d ../shared ]; then \
		ln -sr ../shared/ .; \
	else \
		git clone ${shared_repo} shared; \
	fi
	$(MAKE) -C shared setup

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

export-image: ${BIN}
	uv run -m scripts.export_image ${BIN} --interface 1 --contracts 1

test-model:
	uv run -m scripts.test_model shared/gen/models/feature-mlp/quantized.tflite datasets --results=100

seria-write:
	uv run -m scripts.serial_write ${port_uart} datasets --rate=5000

