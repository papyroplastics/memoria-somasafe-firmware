import sys
import asyncio
import argparse
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt
from ai_edge_litert.interpreter import Interpreter
from bleak import BleakClient, BleakScanner, BleakBackend

DEV_NAME = "SomaSafe Device"

MODEL_CHR_UUID       = "8f04f3a6-1dcb-8a86-c04d-6fa38c87f25a"
MODEL_STATE_CHR_UUID = "794d330a-8c90-229b-b94c-d1025b1c7d19"
MODEL_SIZE_DSC_UUID  = "237384c8-bf34-5587-ab48-ae4b0a7e2585"
MODEL_POS_DSC_UUID   = "f5565c9c-b6be-57af-484a-0f2079374bae"

ML_RESULTS_CHR_UUID = "7228d086-4fc1-4b9c-fa4d-1f715ac23c54"
ML_ERRORS_CHR_UUID  = "9c8b8c42-5a25-6ea5-c642-8a7acf1f330e"

BUFFER_STATE_NOT_READY = 0
BUFFER_STATE_READY = 1

ML_ERROR_NAMES = {
    0: "NONE",
    1: "MODEL_LOAD",
    2: "UNSUPPORTED_OP",
    3: "TENSOR_ALLOC",
    4: "INVOKE",
    5: "INVALID_SHAPE",
}

def parse_args():
    parser = argparse.ArgumentParser(
        description="Upload a .tflite model over BLE and compare on-device "
                    "inference results against the feature dataset.")

    parser.add_argument('model', type=Path, default=Path('models/feature-mlp/post-train-opti.tflite'),
                        help=".tflite model file")
    parser.add_argument('datasets_dir', type=Path, default=Path('datasets'),
                        help="datasets directory (contains feature-anomaly)")
    parser.add_argument('--subject', type=int, default=1,
                        help="Subject id being streamed by serial_write.py (default 1)")
    parser.add_argument('--results', type=int, default=10,
                        help="Number of inference results to receive before stopping (default 10)")
    return parser.parse_args()


async def upload_and_collect(args, model_bytes, expected_len):
    """Connect, upload the model and collect args.results inference results.

    Returns a list of (sequence_n, raw_bytes) where raw_bytes is the
    reassembled [input | score] int8 stream.
    """
    device = await BleakScanner.find_device_by_name(DEV_NAME)
    if device is None:
        print(f"Device \"{DEV_NAME}\" not found", file=sys.stderr)
        exit(1)

    results = []

    async with BleakClient(device) as client:
        print(f"Connection stablised with {client.name} address {client.address}", file=sys.stderr)

        model_chr = None
        model_state_chr = None
        model_size_dsc = None
        model_pos_dsc  = None
        ml_results_chr = None
        ml_errors_chr  = None

        print("\nDevice Services: ", file=sys.stderr)
        for svc in client.services:
            print(f"- Service \"{svc.description}\" - {svc.uuid} - {svc.handle}:", file=sys.stderr)
            for chr in svc.characteristics:
                print(f"  - Characteristic \"{chr.description}\" - {chr.uuid} - {chr.handle}:", file=sys.stderr)
                if chr.uuid == MODEL_CHR_UUID:
                    model_chr = chr
                elif chr.uuid == MODEL_STATE_CHR_UUID:
                    model_state_chr = chr
                elif chr.uuid == ML_RESULTS_CHR_UUID:
                    ml_results_chr = chr
                elif chr.uuid == ML_ERRORS_CHR_UUID:
                    ml_errors_chr = chr

                for dsc in chr.descriptors:
                    print(f"    - Descriptor \"{dsc.description}\" - {dsc.uuid} - {dsc.handle}", file=sys.stderr)
                    if dsc.uuid == MODEL_SIZE_DSC_UUID:
                        model_size_dsc = dsc
                    elif dsc.uuid == MODEL_POS_DSC_UUID:
                        model_pos_dsc = dsc
            print()

        if model_chr is None or \
            model_state_chr is None or \
            model_size_dsc is None or \
            model_pos_dsc is None or \
            ml_results_chr is None or \
            ml_errors_chr is None:
            print("Unable to find model attributes", file=sys.stderr)
            exit(1)

        state_unready = asyncio.Event()

        def wait_unready(_, data: bytearray):
            state = int.from_bytes(data)

            if state == BUFFER_STATE_NOT_READY:
                state_unready.set()

        print(f"Setting buffer state to not ready", file=sys.stderr)
        await client.start_notify(model_state_chr, wait_unready)
        await client.write_gatt_char(model_state_chr, bytes([BUFFER_STATE_NOT_READY]), response=True)

        await state_unready.wait()
        await client.stop_notify(model_state_chr)


        await client.write_gatt_descriptor(model_size_dsc, int.to_bytes(len(model_bytes), length=4, byteorder='little'))
        model_size_od = int.from_bytes(await client.read_gatt_descriptor(model_size_dsc), byteorder='little')
        if model_size_od != len(model_bytes):
            print(f"Failed to set model size {len(model_bytes)}, read {model_size_od}", file=sys.stderr)
            exit(1)
        print(f"Model size on-device: {model_size_od}", file=sys.stderr)

        if client.backend_id == BleakBackend.BLUEZ_DBUS:
            await client._backend._acquire_mtu()  # type: ignore
            async with asyncio.timeout(5):
                while client.mtu_size == 23:
                    await asyncio.sleep(0.25)

        mtu = client.mtu_size
        print(f"MTU set to {mtu}", file=sys.stderr)

        print(f"Writing model... ", file=sys.stderr, end="")
        pos = 0
        while pos < len(model_bytes):
            buf = model_bytes[pos: pos + model_chr.max_write_without_response_size]
            await client.write_gatt_char(model_chr, buf)
            pos += len(buf)
        print("finished", file=sys.stderr)

        await client.write_gatt_descriptor(model_pos_dsc, int.to_bytes(0, length=4, byteorder='little'))
        read_data = bytearray()
        while len(read_data) < len(model_bytes):
            chunk = await client.read_gatt_char(model_chr)
            if not chunk:
                break
            read_data.extend(chunk)
        if read_data[:len(model_bytes)] == model_bytes:
            print("Model readback matches written data", file=sys.stderr)
        else:
            print("Model readback mismatch", file=sys.stderr)
            exit(1)

        done = asyncio.Event()
        sequence_n = None
        reassembly = bytearray()

        def on_ml_results(_, data: bytearray):
            nonlocal sequence_n, reassembly
            if done.is_set():
                return
            if data[0] == 1:
                if sequence_n is not None:
                    print(f"WARNING: dropped incomplete result for sequence {sequence_n}", file=sys.stderr)
                sequence_n = int.from_bytes(data[1:5], byteorder='little')
                reassembly = bytearray(data[5:])
            elif sequence_n is not None:
                reassembly.extend(data[1:])

            if sequence_n is not None and len(reassembly) >= expected_len:
                results.append((sequence_n, bytes(reassembly[:expected_len])))
                print(f"Received result {len(results)}/{args.results} (sequence {sequence_n})", file=sys.stderr)
                sequence_n = None
                if len(results) >= args.results:
                    done.set()

        def on_ml_error(_, data: bytearray):
            code = data[0]
            name = ML_ERROR_NAMES.get(code, "UNKNOWN")
            print(f"ML Error: {name} (code={code})", file=sys.stderr)

        await client.start_notify(ml_results_chr, on_ml_results)
        await client.start_notify(ml_errors_chr, on_ml_error)

        await client.write_gatt_char(model_state_chr, bytes([BUFFER_STATE_READY]), response=True)
        print(f"Model set to ready, waiting for {args.results} results...", file=sys.stderr)

        await done.wait()

        await client.stop_notify(ml_results_chr)
        await client.stop_notify(ml_errors_chr)

    return results


def main():
    args = parse_args()

    model_bytes = args.model.read_bytes()

    interpreter = Interpreter(model_content=model_bytes)

    in_detail = interpreter.get_input_details()[0]
    in_scale, in_zero_point = in_detail['quantization']
    batch_size, n_features = (int(d) for d in in_detail['shape'])

    out_detail = interpreter.get_output_details()[0]
    out_scale, out_zero_point = out_detail['quantization']
    score_size = int(np.prod(out_detail['shape']))

    expected_len = batch_size * n_features + score_size

    feature_dir = args.datasets_dir / 'feature-anomaly' / f'S{args.subject}'
    ds_features = np.load(feature_dir / 'features.npy')
    ds_labels = np.load(feature_dir / 'labels.npy').reshape(-1)

    if ds_features.shape[1] != n_features:
        print(f"Model expects {n_features} features, dataset has {ds_features.shape[1]}", file=sys.stderr)
        exit(1)

    results = asyncio.run(upload_and_collect(args, model_bytes, expected_len))

    # Dequantize and compare against the dataset
    y_true, y_pred, feature_mses = [], [], []

    for sequence_n, raw in results:
        arr = np.frombuffer(raw, dtype=np.int8)

        features = arr[:batch_size * n_features].reshape(batch_size, n_features)[0]
        features = (features.astype(np.float32) - in_zero_point) * in_scale

        scores = arr[batch_size * n_features:].astype(np.float32)
        score = float((scores[0] - out_zero_point) * out_scale)

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
