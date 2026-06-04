import sys
import serial
import pathlib
import time

if len(sys.argv) != 2:
    print(f"USE: python {sys.argv[0]} <serial>", file=sys.stderr)
    exit(1)

device = pathlib.Path(sys.argv[1])

if not device.exists():
    print(f"ERROR: path {str(device)} does not exist", file=sys.stderr)
    exit(1)

port = serial.Serial(
    port=str(device),
    baudrate=115200,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=1,
)

time.sleep(0.1)

flushed_data = b''

while True:
    r_bytes = port.read()
    if len(r_bytes) == 0:
        break
    flushed_data += r_bytes

print(f"flushed data: {flushed_data}")

w_bytes = b'Hello, World!\n'
port.write(w_bytes)
port.flush()

r_bytes = port.read(len(w_bytes))
print(f"read: {r_bytes}")

