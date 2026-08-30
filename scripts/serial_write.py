"""Stream a subject's raw PPG/ACC samples from a `.ssds` export over serial to the ESP32 test harness, standing in for the sensor the firmware would otherwise read; restart the ESP before running so its window sequence numbers line up with the export's."""

import sys
import time
import argparse
from pathlib import Path

import numpy as np
import serial

from .lib.capture import CaptureExport

DEFAULT_RATE_HZ = 32

CYCLE_BYTES = 3 * 4
BAUD = 115200


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('serial', type=Path, help="Serial device.")
    parser.add_argument('export', type=Path, nargs='?',
                        default=Path('shared/gen/exports/S1.ssds'),
                        help="Subject .ssds export to stream.")
    parser.add_argument('--rate', type=float, default=DEFAULT_RATE_HZ,
                        help="Cycle rate in Hz.")
    args = parser.parse_args()

    if not args.serial.exists():
        print(f"ERROR: {args.serial} does not exist", file=sys.stderr)
        sys.exit(1)
    if not args.export.exists():
        print(f"ERROR: {args.export} not found. Run the backend's "
              f"scripts.system.export_subject_data first.", file=sys.stderr)
        sys.exit(1)

    export = CaptureExport(args.export)
    bvp, acc = export.signal_stream()

    max_rate = BAUD / (CYCLE_BYTES * 10)
    if args.rate > max_rate:
        print(f"WARNING: {args.rate:g} Hz exceeds what {BAUD} baud carries "
              f"({max_rate:.0f} Hz); the line will pace the run instead", file=sys.stderr)

    port = serial.Serial(
        port=str(args.serial),
        baudrate=BAUD,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=None,
    )

    cycle_period = 1.0 / args.rate

    n_cycles = min(len(bvp) // 2, len(acc))
    duration_s = n_cycles / args.rate

    print(f"S{export.subject}: {n_cycles} cycles ({duration_s:.0f}s at {args.rate:g} Hz), "
          f"{len(export.windows)} windows")

    for i in range(n_cycles):
        t0 = time.monotonic()

        data = np.array([bvp[i * 2], bvp[i * 2 + 1], acc[i]], dtype=np.float32)
        port.write(data.tobytes())

        if (i + 1) % (int(args.rate) * 10) == 0:
            print(f"  {i + 1}/{n_cycles} cycles", end='\r')

        remaining = cycle_period - (time.monotonic() - t0)
        if remaining > 0:
            time.sleep(remaining)

    port.flush()
    print(f"  {n_cycles}/{n_cycles} cycles — done     ")


if __name__ == '__main__':
    main()
