"""Upload a quantized .tflite model over BLE and compare the device's own inference results against a subject export, standing in for the phone: it assembles and signs the model payload, then scores the returned inferences against the export's stored labels."""

import sys
import asyncio
import argparse
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt
from bleak import BleakClient, BleakScanner

from .lib.ble_common import (
    BUF_CHR_UUID, BUF_STATE_CHR_UUID, BUF_SIZE_DSC_UUID, BUF_POS_DSC_UUID,
    ML_RESULTS_CHR_UUID, ML_ERRORS_CHR_UUID, ML_SVC_UUID,
    negotiate_mtu, discover_attributes,
)
from .lib.capture import CaptureExport
from .lib.client_buf import ClientBuffer
from .lib.ml_service import MlService
from .lib.litert_util import ModelQuant
from .lib.payload import build_payload, norm_bytes

DEV_NAME = "SomaSafe Device"

CONTRACT_VERSION = 1

ATTR_UUIDS = (
    BUF_CHR_UUID, BUF_STATE_CHR_UUID, BUF_SIZE_DSC_UUID, BUF_POS_DSC_UUID,
    ML_RESULTS_CHR_UUID, ML_ERRORS_CHR_UUID,
)


async def upload_and_collect(payload, n_results, quant, address=None):
    """Connect, upload the model payload and collect n_results inference results."""
    target = address
    if target is None:
        target = await BleakScanner.find_device_by_name(DEV_NAME)
        if target is None:
            print(f"Device \"{DEV_NAME}\" not found", file=sys.stderr)
            exit(1)

    async with BleakClient(target) as client:
        print(f"Connected to {client.name} ({client.address})", file=sys.stderr)

        attrs = await discover_attributes(client, ATTR_UUIDS, service=ML_SVC_UUID)
        await negotiate_mtu(client)

        buffer = ClientBuffer(client, attrs)
        await buffer.start()
        await buffer.upload(payload)

        ml = MlService(client, buffer, quant.input.size * 4, quant.output.size)
        return await ml.get_results(n_results)


def main():
    parser = argparse.ArgumentParser(
            description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument('model', type=Path, nargs='?',
                        default=Path('shared/gen/models/feature-mlp/quantized.tflite'),
                        help="Quantized .tflite model file.")
    parser.add_argument('export', type=Path, nargs='?',
                        default=Path('shared/gen/exports/S1.ssds'),
                        help="Subject .ssds export being streamed by serial_write.py.")
    parser.add_argument('--results', type=int, default=10,
                        help="Number of inference results to collect.")
    parser.add_argument('--contract', type=int, default=CONTRACT_VERSION,
                        help="Model contract version to sign into the payload.")
    parser.add_argument('--server-key', type=Path,
                        default=Path(__file__).resolve().parent.parent / 'shared' / 'gen' / 'server-private-key.pem',
                        help="ECDSA private key to sign the model payload with.")
    parser.add_argument('--address', default=None,
                        help="BLE address to connect to instead of scanning by name.")
    parser.add_argument('--no-plot', action='store_true',
                        help="Skip the confusion-matrix window.")
    args = parser.parse_args()

    model_bytes = args.model.read_bytes()
    quant = ModelQuant(model_bytes)
    export = CaptureExport(args.export)

    if len(export.norm_mean) != quant.n_features:
        print(f"Model expects {quant.n_features} features, export carries norm params "
              f"for {len(export.norm_mean)}", file=sys.stderr)
        exit(1)

    payload = build_payload(model_bytes, args.contract,
                            norm_bytes(export.norm_mean, export.norm_std),
                            args.server_key)

    results = asyncio.run(upload_and_collect(payload, args.results, quant,
                                            args.address))

    y_true, y_pred, feature_mses = [], [], []

    for sequence_n, features_raw, score_raw in results:
        features = np.frombuffer(features_raw, dtype=np.float32)
        features = features.reshape(quant.batch_size, quant.n_features)[0]

        score = float(quant.output.dequantize(np.frombuffer(score_raw, dtype=np.int8))[0])

        expected = export.features(sequence_n)
        label = export.label(sequence_n)
        if expected is None or label is None:
            print(f"WARNING: sequence {sequence_n} has no reference window in "
                  f"{args.export}, skipping", file=sys.stderr)
            continue

        pred = int(score > 0.0)
        mse = float(np.mean((features - expected) ** 2))

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

    print(f"\nFeature MSE (on-device vs export): mean={np.mean(feature_mses):.6f} "
          f"max={np.max(feature_mses):.6f}")
    for label, name in enumerate(('normal', 'anomalous')):
        total = confusion[label].sum()
        if total == 0:
            print(f"Accuracy ({name}): n/a (no windows)")
        else:
            print(f"Accuracy ({name}): {confusion[label, label] / total:.3f} ({total} windows)")
    print(f"Accuracy (total): {np.trace(confusion) / confusion.sum():.3f} ({confusion.sum()} windows)")

    if args.no_plot:
        return

    fig, ax = plt.subplots()
    ax.imshow(confusion, cmap='Blues')
    ax.set_xticks([0, 1], ['normal', 'anomalous'])
    ax.set_yticks([0, 1], ['normal', 'anomalous'])
    ax.set_xlabel("Predicted")
    ax.set_ylabel("True")
    ax.set_title(f"S{export.subject} on-device confusion matrix ({len(y_true)} windows)")
    for i in range(2):
        for j in range(2):
            color = 'white' if confusion[i, j] > confusion.max() / 2 else 'black'
            ax.text(j, i, str(confusion[i, j]), ha='center', va='center', color=color)
    plt.tight_layout()
    plt.show()


if __name__ == '__main__':
    main()
