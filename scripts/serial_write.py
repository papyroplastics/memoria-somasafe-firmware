import os
import sys
import time
import argparse
from pathlib import Path

import numpy as np
import serial

# Each cycle: 2 PPG samples + 1 ACC sample sent at this rate.
# PPG effective rate = SEND_RATE_HZ * 2 (64 Hz at default)
# ACC effective rate = SEND_RATE_HZ     (32 Hz at default)
DEFAULT_RATE_HZ = 32

MARKER = bytes([0xAA, 0xBB, 0xCC, 0xDD])
ACK_TIMEOUT_S = 1.0


def read_exact(port, n):
    data = bytearray()
    while len(data) < n:
        data.extend(port.read(n - len(data)))
    return bytes(data)


def handshake(port):
    """Send marker + nonce to the ESP, wait for them echoed back, then echo
    the ESP's nonce to confirm both sides are in sync."""
    script_nonce = os.urandom(4)
    port.write(MARKER + script_nonce)

    expect = MARKER + script_nonce
    window = bytearray()
    while True:
        window.extend(read_exact(port, 1))
        del window[:-len(expect)]
        if bytes(window) == expect:
            break

    esp_nonce = read_exact(port, 4)
    port.write(esp_nonce)


def main():
    parser = argparse.ArgumentParser(
        description="Stream raw anomalous PPG/ACC data over serial to the ESP32 test harness.")
    parser.add_argument('serial',       type=Path, help="Serial device (e.g. /dev/ttyUSB0)")
    parser.add_argument('datasets_dir', type=Path, default=Path('datasets'),
                        help="datasets/ directory (contains anomalous-signals/ and subject-signals/)")
    parser.add_argument('--subject', type=int, default=1,
                        help="Subject id to stream (default 1)")
    parser.add_argument('--rate', type=float, default=DEFAULT_RATE_HZ,
                        help=f"Cycle rate in Hz (default {DEFAULT_RATE_HZ}). "
                             "One cycle = 2 PPG floats + 1 ACC float.")
    args = parser.parse_args()

    sid = f'S{args.subject}'
    bvp_path = args.datasets_dir / 'anomalous-signals' / sid / 'bvp.npy'
    acc_path = args.datasets_dir / 'subject-signals'   / sid / 'acc_mag.npy'

    if not args.serial.exists():
        print(f"ERROR: {args.serial} does not exist", file=sys.stderr)
        sys.exit(1)
    for path in (bvp_path, acc_path):
        if not path.exists():
            print(f"ERROR: {path} not found", file=sys.stderr)
            sys.exit(1)

    bvp = np.load(bvp_path).astype(np.float32)
    acc = np.load(acc_path).astype(np.float32)

    port = serial.Serial(
        port=str(args.serial),
        baudrate=115200,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=None,
    )
    time.sleep(0.1)

    print("Initiating handshake with ESP (restart the ESP if it hangs here)...")
    handshake(port)
    print("Handshake complete")

    port.timeout = ACK_TIMEOUT_S
    cycle_period = 1.0 / args.rate

    # Each cycle consumes 2 BVP samples and 1 ACC sample
    n_cycles = min(len(bvp) // 2, len(acc))
    duration_s = n_cycles / args.rate

    print(f"{sid}: {n_cycles} cycles ({duration_s:.0f}s at {args.rate} Hz)")

    for i in range(n_cycles):
        t0 = time.monotonic()

        # Interleave as the firmware expects: ppg, ppg, acc + random ack byte
        data = np.array([bvp[i * 2], bvp[i * 2 + 1], acc[i]], dtype=np.float32)
        postfix = os.urandom(1)
        port.write(data.tobytes() + postfix)

        ack = port.read(1)
        if len(ack) == 0:
            print(f"ERROR: ack timeout at cycle {i}", file=sys.stderr)
            sys.exit(1)
        if ack != postfix:
            print(f"ERROR: bad ack at cycle {i}: sent {postfix.hex()}, got {ack.hex()}",
                  file=sys.stderr)
            sys.exit(1)

        if (i + 1) % (int(args.rate) * 10) == 0:
            print(f"  {i + 1}/{n_cycles} cycles", end='\r')

        elapsed = time.monotonic() - t0
        remaining = cycle_period - elapsed
        if remaining > 0:
            time.sleep(remaining)

    print(f"  {n_cycles}/{n_cycles} cycles — done     ")


if __name__ == '__main__':
    main()
