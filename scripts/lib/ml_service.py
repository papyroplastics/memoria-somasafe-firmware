import sys
import asyncio

from bleak import BleakClient

from .ble_common import ML_RESULTS_CHR_UUID, ML_ERRORS_CHR_UUID
from .client_buf import ClientBuffer
from .notif_transaction import TransactionReassembler

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

    Each result is one reconstruction-layer transaction (see notif_transaction)
    whose reassembled payload is the service stream: a 4-byte little-endian
    sample sequence number followed by the [features | score] int8 data.
    """

    SEQUENCE_LEN = 4

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
        data_len = self.SEQUENCE_LEN + self.features_len + self.score_len
        results = []
        done = asyncio.Event()

        reasm = TransactionReassembler()

        def on_result(_, data: bytearray):
            if done.is_set():
                return

            payload = reasm.feed(bytes(data))
            if payload is None:
                return

            if len(payload) != data_len:
                print(f"WARNING: dropped result with unexpected length {len(payload)} (expected {data_len})", file=sys.stderr)
                return

            sequence_n = int.from_bytes(payload[:self.SEQUENCE_LEN], byteorder='little')
            body = payload[self.SEQUENCE_LEN:]
            features = bytes(body[:self.features_len])
            score = bytes(body[self.features_len:])
            results.append((sequence_n, features, score))
            print(f"Received result {len(results)}/{result_n} (sequence {sequence_n})", file=sys.stderr)
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
