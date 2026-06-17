"""Client side of the firmware reconstruction layer (notif_transaction).

Reassembles arbitrary-length payloads ("transactions") fragmented across GATT
notifications. Each notification starts with a 3-byte header:

    flags          (1 byte): bit0 START, bit1 END (both set on a 1-packet txn)
    transaction_id (1 byte): equal across a transaction, for error checking
    sequence_n     (1 byte): 0 on the first packet, +1 per packet, wraps at 256

Service-specific code lives on top: feed every notification's bytes to
TransactionReassembler.feed() and parse the payload it returns on END.
"""

FLAG_START = 0x01
FLAG_END = 0x02
HEADER_LEN = 3


class TransactionReassembler:
    """Stateful reassembler for one notification stream.

    feed() returns the completed payload (bytes) when a transaction's END packet
    arrives, or None while a transaction is in progress, dropped, or ignored.
    Errors (id mismatch, sequence gap, stray continuation) are dropped silently
    per protocol; a START packet always begins a fresh transaction, discarding
    any incomplete one.
    """

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
            # Begin a fresh transaction, discarding any incomplete one.
            self._active = True
            self._txn_id = txn_id
            self._next_seq = (seq + 1) & 0xFF
            self._buf = bytearray(body)
        else:
            if not self._active:
                return None  # continuation with no start in progress
            if txn_id != self._txn_id or seq != self._next_seq:
                self._reset()  # wrong transaction or missing packet
                return None
            self._next_seq = (seq + 1) & 0xFF
            self._buf.extend(body)

        if end:
            payload = bytes(self._buf)
            self._reset()
            return payload
        return None
