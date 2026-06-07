import sys
import serial
import pathlib
import time

import numpy as np

if len(sys.argv) < 3:
    print(f"USE: python {sys.argv[0]} <serial> <feature_dir>", file=sys.stderr)
    print(f"  feature_dir: directory containing features.npy and labels.npy", file=sys.stderr)
    exit(1)

device = pathlib.Path(sys.argv[1])
feature_dir = pathlib.Path(sys.argv[2])

if not device.exists():
    print(f"ERROR: {device} does not exist", file=sys.stderr)
    exit(1)

features_path = feature_dir / 'features.npy'
labels_path = feature_dir / 'labels.npy'

if not features_path.exists():
    print(f"ERROR: features.npy not found in {feature_dir}", file=sys.stderr)
    exit(1)

features = np.load(features_path).astype(np.float32)  # (N, 17)
labels = np.load(labels_path).astype(np.float32)      # (N, 1)

print(f"Loaded {len(features)} windows ({features.shape[1]} features each, "
      f"{int(labels.sum())} anomalous)")

port = serial.Serial(
    port=str(device),
    baudrate=115200,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=1,
)

time.sleep(0.1)

for i, window in enumerate(features):
    port.write(window.tobytes())
    if (i + 1) % 1000 == 0:
        port.flush()
        print(f"Sent {i + 1}/{len(features)} windows")

port.flush()
print(f"Done — sent {len(features)} feature vectors.")
