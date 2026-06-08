import sys
import time
import argparse
import pathlib

import numpy as np
import serial

# Each cycle: 2 PPG samples + 1 ACC sample sent at this rate.
# PPG effective rate = SEND_RATE_HZ * 2 (64 Hz at default)
# ACC effective rate = SEND_RATE_HZ     (32 Hz at default)
DEFAULT_RATE_HZ = 32


def main():
    parser = argparse.ArgumentParser(
        description="Stream raw anomalous PPG/ACC data over serial to the ESP32 test harness.")
    parser.add_argument('serial',        type=pathlib.Path, help="Serial device (e.g. /dev/ttyUSB0)")
    parser.add_argument('anomalous_dir', type=pathlib.Path,
                        help="anomalous-signals directory (contains S{id}/bvp.npy)")
    parser.add_argument('subjects_dir',  type=pathlib.Path,
                        help="subject-signals directory (contains S{id}/acc_mag.npy)")
    parser.add_argument('--rate', type=float, default=DEFAULT_RATE_HZ,
                        help=f"Cycle rate in Hz (default {DEFAULT_RATE_HZ}). "
                             "One cycle = 2 PPG floats + 1 ACC float.")
    args = parser.parse_args()

    if not args.serial.exists():
        print(f"ERROR: {args.serial} does not exist", file=sys.stderr)
        sys.exit(1)
    if not args.anomalous_dir.is_dir():
        print(f"ERROR: {args.anomalous_dir} is not a directory", file=sys.stderr)
        sys.exit(1)
    if not args.subjects_dir.is_dir():
        print(f"ERROR: {args.subjects_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    subject_dirs = sorted(args.anomalous_dir.glob('S*'))
    if not subject_dirs:
        print(f"ERROR: no subject directories found in {args.anomalous_dir}", file=sys.stderr)
        sys.exit(1)

    port = serial.Serial(
        port=str(args.serial),
        baudrate=115200,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=1,
    )
    time.sleep(0.1)

    cycle_period = 1.0 / args.rate

    for subject_dir in subject_dirs:
        sid = subject_dir.name
        bvp_path = subject_dir / 'bvp.npy'
        acc_path  = args.subjects_dir / sid / 'acc_mag.npy'

        if not bvp_path.exists():
            print(f"WARNING: {bvp_path} not found, skipping {sid}", file=sys.stderr)
            continue
        if not acc_path.exists():
            print(f"WARNING: {acc_path} not found, skipping {sid}", file=sys.stderr)
            continue

        bvp = np.load(bvp_path).astype(np.float32)
        acc = np.load(acc_path).astype(np.float32)

        # Each cycle consumes 2 BVP samples and 1 ACC sample
        n_cycles = min(len(bvp) // 2, len(acc))
        duration_s = n_cycles / args.rate

        print(f"{sid}: {n_cycles} cycles ({duration_s:.0f}s at {args.rate} Hz)")

        for i in range(n_cycles):
            t0 = time.monotonic()

            # Interleave as the firmware expects: ppg, ppg, acc
            data = np.array([bvp[i * 2], bvp[i * 2 + 1], acc[i]], dtype=np.float32)
            port.write(data.tobytes())

            if (i + 1) % (int(args.rate) * 10) == 0:
                port.flush()
                print(f"  {i + 1}/{n_cycles} cycles", end='\r')

            elapsed = time.monotonic() - t0
            remaining = cycle_period - elapsed
            if remaining > 0:
                time.sleep(remaining)

        print(f"  {n_cycles}/{n_cycles} cycles — done     ")

    port.flush()
    print("All subjects sent.")


if __name__ == '__main__':
    main()
