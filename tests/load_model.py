import sys
import asyncio
import pathlib
import numpy as np
import matplotlib.pyplot as plt
from ai_edge_litert.interpreter import Interpreter
from bleak import BleakClient, BleakScanner, BleakBackend

DEV_NAME = "SomaSafe Device"

MODEL_CHR_UUID      = "8f04f3a6-1dcb-8a86-c04d-6fa38c87f25a"
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

if len(sys.argv) != 2:
    print(f"USE: python {sys.argv[0]} <model file>", file=sys.stderr)
    exit(1)

model_file = pathlib.Path(sys.argv[1])
model_bytes = model_file.read_bytes()

tflite_model = Interpreter(model_content=model_bytes)

in_quant  = tflite_model.get_input_details()[0]['quantization_parameters']
in_scale, in_zero_point = in_quant['scales'][0], in_quant['zero_points'][0]

out_quant  = tflite_model.get_output_details()[0]['quantization_parameters']
out_scale, out_zero_point = out_quant['scales'][0], out_quant['zero_points'][0]


async def main():
    device = await BleakScanner.find_device_by_name(DEV_NAME)
    if device is None:
        print(f"Device \"{DEV_NAME}\" not found", file=sys.stderr)
        return

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

        await client.write_gatt_char(model_state_chr, bytes([BUFFER_STATE_NOT_READY]), response=True)

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

        pos = 0
        while pos < len(model_bytes):
            buf = model_bytes[pos: pos + model_chr.max_write_without_response_size]
            await client.write_gatt_char(model_chr, buf)
            pos += len(buf)
        print("Finished writing model", file=sys.stderr)

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

        # Snapshot accumulation state
        snapshot_done   = asyncio.Event()
        snapshot_inputs  = []
        snapshot_outputs = []
        snapshot_start_ms = 0
        snapshot_end_ms   = 0
        collecting = False

        def on_ml_results(_, data: bytearray):
            nonlocal collecting, snapshot_start_ms, snapshot_end_ms
            msg_type = data[0]
            if msg_type == 0:
                if collecting and snapshot_inputs:
                    # Second boundary: first snapshot is complete
                    snapshot_done.set()
                    return
                # First boundary: record metadata and start collecting
                snapshot_start_ms = int.from_bytes(data[1:5], byteorder='little')
                snapshot_end_ms   = int.from_bytes(data[5:9], byteorder='little')
                snapshot_inputs.clear()
                snapshot_outputs.clear()
                collecting = True
            elif msg_type == 1 and collecting and not snapshot_done.is_set():
                n = (len(data) - 1) // 2
                snapshot_inputs.extend(
                    int.from_bytes([b], byteorder='little', signed=True)
                    for b in data[1:1 + n]
                )
                snapshot_outputs.extend(
                    int.from_bytes([b], byteorder='little', signed=True)
                    for b in data[1 + n:1 + 2 * n]
                )

        def on_ml_error(_, data: bytearray):
            code = data[0]
            name = ML_ERROR_NAMES.get(code, "UNKNOWN")
            print(f"ML Error: {name} (code={code})", file=sys.stderr)

        await client.start_notify(ml_results_chr, on_ml_results)
        await client.start_notify(ml_errors_chr, on_ml_error)

        await client.write_gatt_char(model_state_chr, bytes([BUFFER_STATE_READY]), response=True)
        print("Model set to ready, waiting for a complete snapshot...", file=sys.stderr)

        await snapshot_done.wait()
        print(
            f"Snapshot received: {snapshot_start_ms}ms – {snapshot_end_ms}ms, "
            f"{len(snapshot_inputs)} samples",
            file=sys.stderr,
        )

        await client.stop_notify(ml_results_chr)
        await client.stop_notify(ml_errors_chr)

    # Dequantize and plot
    inputs_f  = (np.array(snapshot_inputs,  dtype=np.float32) - in_zero_point)  * in_scale
    outputs_f = (np.array(snapshot_outputs, dtype=np.float32) - out_zero_point) * out_scale

    duration_ms = snapshot_end_ms - snapshot_start_ms
    t = np.linspace(0, duration_ms, len(inputs_f))

    fig, (ax_in, ax_out) = plt.subplots(2, 1, sharex=True)
    ax_in.plot(t, inputs_f)
    ax_in.set_ylabel("PPG input")
    ax_out.plot(t, outputs_f)
    ax_out.set_ylabel("Model output")
    ax_out.set_xlabel("Time (ms)")
    fig.suptitle(f"Snapshot {snapshot_start_ms}ms – {snapshot_end_ms}ms")
    plt.tight_layout()
    plt.show()


asyncio.run(main())
