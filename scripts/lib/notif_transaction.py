"""Client side of the firmware reconstruction layer (notif_transaction): reassembles arbitrary-length payloads fragmented across GATT notifications."""

FLAG_START = 0x01
FLAG_END = 0x02
HEADER_LEN = 3


class TransactionReassembler:
    """Stateful reassembler for one notification stream."""

    def __init__(self):
        self._reset()

    def _reset(self):
        self._active = False
        self._txn_id = None
        self._next_seq = 0
        self._buf = bytearray()

    def feed(self, data: bytes):
        if len(data) < HEADER_LEN:
            self._reset()
            return None

        flags, txn_id, seq = data[0], data[1], data[2]
        body = data[HEADER_LEN:]
        start = bool(flags & FLAG_START)
        end = bool(flags & FLAG_END)

        if start:
            self._active = True
            self._txn_id = txn_id
            self._next_seq = (seq + 1) & 0xFF
            self._buf = bytearray(body)
        else:
            if not self._active:
                return None
            if txn_id != self._txn_id or seq != self._next_seq:
                self._reset()
                return None
            self._next_seq = (seq + 1) & 0xFF
            self._buf.extend(body)

        if end:
            payload = bytes(self._buf)
            self._reset()
            return payload
        return None
