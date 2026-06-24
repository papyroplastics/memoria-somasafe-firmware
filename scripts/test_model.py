import sys
import asyncio
import argparse
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt
from bleak import BleakClient, BleakScanner

from .lib.ble_common import (
    BUF_CHR_UUID, BUF_STATE_CHR_UUID, BUF_SIZE_DSC_UUID, BUF_POS_DSC_UUID,
    ML_RESULTS_CHR_UUID, ML_ERRORS_CHR_UUID,
    negotiate_mtu, discover_attributes,
)
from .lib.client_buf import ClientBuffer
from .lib.ml_service import MlService
from .lib.litert_util import ModelQuant

DEV_NAME = "SomaSafe Device"

ATTR_UUIDS = (
    BUF_CHR_UUID, BUF_STATE_CHR_UUID, BUF_SIZE_DSC_UUID, BUF_POS_DSC_UUID,
    ML_RESULTS_CHR_UUID, ML_ERRORS_CHR_UUID,
)


async def upload_and_collect(model_bytes, n_results, quant):
    """Connect, upload the model and collect n_results inference results."""
    device = await BleakScanner.find_device_by_name(DEV_NAME)
    if device is None:
        print(f"Device \"{DEV_NAME}\" not found", file=sys.stderr)
        exit(1)

    async with BleakClient(device) as client:
        print(f"Connected to {client.name} ({client.address})", file=sys.stderr)

        attrs = await discover_attributes(client, ATTR_UUIDS)
        await negotiate_mtu(client)

        buffer = ClientBuffer(client, attrs)
        await buffer.start()
        await buffer.upload(model_bytes)

        ml = MlService(client, buffer, quant.input.size, quant.output.size)
        return await ml.get_results(n_results)


def main():
    parser = argparse.ArgumentParser(
            description="Upload a .tflite model over BLE and compare on-device "
                        "inference results against the feature dataset.")

    parser.add_argument('model', type=Path, default=Path('models/feature-mlp/quantized.tflite'),
                        help=".tflite model file")
    parser.add_argument('datasets_dir', type=Path, default=Path('datasets'),
                        help="datasets directory (contains mixed-features)")
    parser.add_argument('--subject', type=int, default=1,
                        help="Subject id being streamed by serial_write.py (default 1)")
    parser.add_argument('--results', type=int, default=10,
                        help="Number of inference results to receive before stopping (default 10)")
    args = parser.parse_args()

    model_bytes = args.model.read_bytes()
    quant = ModelQuant(model_bytes)

    feature_dir = args.datasets_dir / 'mixed-features' / f'S{args.subject}'
    ds_features = np.load(feature_dir / 'features.npy')
    ds_labels = np.load(feature_dir / 'labels.npy').reshape(-1)

    if ds_features.shape[1] != quant.n_features:
        print(f"Model expects {quant.n_features} features, dataset has {ds_features.shape[1]}", file=sys.stderr)
        exit(1)

    results = asyncio.run(upload_and_collect(model_bytes, args.results, quant))

    # Dequantize and compare against the dataset
    y_true, y_pred, feature_mses = [], [], []

    for sequence_n, features_raw, score_raw in results:
        features = np.frombuffer(features_raw, dtype=np.int8)
        features = quant.input.dequantize(features.reshape(quant.batch_size, quant.n_features)[0])

        score = float(quant.output.dequantize(np.frombuffer(score_raw, dtype=np.int8))[0])

        if sequence_n >= len(ds_labels):
            print(f"WARNING: sequence {sequence_n} out of dataset range "
                  f"({len(ds_labels)} windows), skipping", file=sys.stderr)
            continue

        label = int(ds_labels[sequence_n])
        pred = int(score > 0.5)
        mse = float(np.mean((features - ds_features[sequence_n]) ** 2))

        y_true.append(label)
        y_pred.append(pred)
        feature_mses.append(mse)

        print(f"seq={sequence_n:5d} label={label} pred={pred} "
              f"score={score:.4f} feature_mse={mse:.6f}")

    if not y_true:
        print("No comparable results received", file=sys.stderr)
        exit(1)

    confusion = np.zeros((2, 2), dtype=int)
    for label, pred in zip(y_true, y_pred):
        confusion[label, pred] += 1

    print(f"\nFeature MSE (on-device vs dataset): mean={np.mean(feature_mses):.6f} "
          f"max={np.max(feature_mses):.6f}")
    for label, name in enumerate(('normal', 'anomalous')):
        total = confusion[label].sum()
        if total == 0:
            print(f"Accuracy ({name}): n/a (no windows)")
        else:
            print(f"Accuracy ({name}): {confusion[label, label] / total:.3f} ({total} windows)")
    print(f"Accuracy (total): {np.trace(confusion) / confusion.sum():.3f} ({confusion.sum()} windows)")

    fig, ax = plt.subplots()
    ax.imshow(confusion, cmap='Blues')
    ax.set_xticks([0, 1], ['normal', 'anomalous'])
    ax.set_yticks([0, 1], ['normal', 'anomalous'])
    ax.set_xlabel("Predicted")
    ax.set_ylabel("True")
    ax.set_title(f"S{args.subject} on-device confusion matrix ({len(y_true)} windows)")
    for i in range(2):
        for j in range(2):
            color = 'white' if confusion[i, j] > confusion.max() / 2 else 'black'
            ax.text(j, i, str(confusion[i, j]), ha='center', va='center', color=color)
    plt.tight_layout()
    plt.show()


if __name__ == '__main__':
    main()
