import sys
import asyncio

from bleak import BleakClient

from .ble_common import ML_RESULTS_CHR_UUID, ML_ERRORS_CHR_UUID
from .client_buf import ClientBuffer

ML_ERROR_NAMES = {
    0: "NONE",
    1: "MODEL_LOAD",
    2: "UNSUPPORTED_OP",
    3: "TENSOR_ALLOC",
    4: "INVOKE",
    5: "INVALID_SHAPE",
}


class MlService:
    """Collects inference results notified by the firmware ML service.

    Each result is the [features | score] int8 stream framed across
    notifications: the first packet starts with a 1 byte followed by the
    4-byte little-endian sample sequence number, continuation packets start
    with a 0 byte.
    """

    def __init__(self, client: BleakClient, buffer: ClientBuffer,
                 features_len: int, score_len: int):
        self.client = client
        self.buffer = buffer
        self.features_len = features_len
        self.score_len = score_len
        self.results_chr = buffer.attrs[ML_RESULTS_CHR_UUID]
        self.errors_chr = buffer.attrs[ML_ERRORS_CHR_UUID]

    async def get_results(self, result_n: int) -> list:
        """Flip the model buffer to READY and collect up to result_n results.

        Returns a list of (sequence_n, features, score) tuples where features
        and score are the raw int8 byte strings for one sample.
        """
        expected_len = self.features_len + self.score_len
        results = []
        done = asyncio.Event()

        sequence_n = None
        reassembly = bytearray()

        def on_result(_, data: bytearray):
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
                features = bytes(reassembly[:self.features_len])
                score = bytes(reassembly[self.features_len:expected_len])
                results.append((sequence_n, features, score))
                print(f"Received result {len(results)}/{result_n} (sequence {sequence_n})", file=sys.stderr)
                sequence_n = None
                if len(results) >= result_n:
                    done.set()

        def on_error(_, data: bytearray):
            code = data[0]
            print(f"ML Error: {ML_ERROR_NAMES.get(code, 'UNKNOWN')} (code={code})", file=sys.stderr)

        await self.client.start_notify(self.results_chr, on_result)
        await self.client.start_notify(self.errors_chr, on_error)

        await self.buffer.ready()
        await done.wait()

        await self.client.stop_notify(self.results_chr)
        await self.client.stop_notify(self.errors_chr)

        return results
