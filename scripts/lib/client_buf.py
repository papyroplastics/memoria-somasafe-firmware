import sys
import asyncio

from bleak import BleakClient

from .ble_common import (
    BUF_CHR_UUID,
    BUF_STATE_CHR_UUID,
    BUF_SIZE_DSC_UUID,
    BUF_POS_DSC_UUID,
    u8_bytes,
    u32_bytes,
)

BUFFER_STATE_NOT_READY = 0
BUFFER_STATE_READY = 1


class ClientBuffer:
    """Client side of the firmware's client_buffer GATT service."""

    def __init__(self, client: BleakClient, attrs: dict):
        self.client = client
        self.attrs = attrs
        self.buf_chr = attrs[BUF_CHR_UUID]
        self.state_chr = attrs[BUF_STATE_CHR_UUID]
        self.size_dsc = attrs[BUF_SIZE_DSC_UUID]
        self.pos_dsc = attrs[BUF_POS_DSC_UUID]
        self.state = None
        self._state_changed = asyncio.Event()

    def _on_state(self, _, data: bytearray):
        self.state = int.from_bytes(data, byteorder='little')
        self._state_changed.set()

    async def start(self):
        """Subscribe to state notifications and read the current buffer state."""
        await self.client.start_notify(self.state_chr, self._on_state)
        data = await self.client.read_gatt_char(self.state_chr)
        self.state = int.from_bytes(data, byteorder='little')
        print(f"Buffer state on-device: {self.state}", file=sys.stderr)

    async def wait_state(self, target: int):
        while self.state != target:
            self._state_changed.clear()
            await self._state_changed.wait()

    async def set_state(self, state: int):
        if state == BUFFER_STATE_NOT_READY:
            await self.client.write_gatt_char(self.state_chr, u8_bytes(BUFFER_STATE_NOT_READY))
            await self.wait_state(BUFFER_STATE_NOT_READY)
            print("Buffer state set to NOT_READY", file=sys.stderr)

        elif state == BUFFER_STATE_READY:
            await self.client.write_gatt_char(self.state_chr, u8_bytes(BUFFER_STATE_READY))
            self.state = BUFFER_STATE_READY
            print("Buffer state set to READY", file=sys.stderr)

        else:
            raise ValueError(f"invalid buffer state {state}")

    async def set_size(self, size: int):
        if self.state != BUFFER_STATE_NOT_READY:
            raise RuntimeError(f"invalid buffer state {self.state}")

        await self.client.write_gatt_descriptor(self.size_dsc, u32_bytes(size))
        read = int.from_bytes(await self.client.read_gatt_descriptor(self.size_dsc), byteorder='little')
        if read != size:
            print(f"Failed to set buffer size {size}, read {read}", file=sys.stderr)
            exit(1)
        print(f"Buffer size on-device: {read}", file=sys.stderr)

    async def write(self, data: bytes):
        if self.state != BUFFER_STATE_NOT_READY:
            raise RuntimeError(f"invalid buffer state {self.state}")

        print("Writing buffer... ", file=sys.stderr, end="", flush=True)
        pos = 0
        while pos < len(data):
            chunk = data[pos: pos + self.buf_chr.max_write_without_response_size]
            await self.client.write_gatt_char(self.buf_chr, chunk)
            pos += len(chunk)
        print("finished", file=sys.stderr)

    async def verify(self, data: bytes) -> bool:
        await self.client.write_gatt_descriptor(self.pos_dsc, u32_bytes(0))
        read = bytearray()
        while len(read) < len(data):
            chunk = await self.client.read_gatt_char(self.buf_chr)
            if not chunk:
                break
            read.extend(chunk)

        if read[:len(data)] == data:
            print("Buffer readback matches written data", file=sys.stderr)
            return True
        print("Buffer readback mismatch", file=sys.stderr)
        return False

    async def upload(self, data: bytes):
        """Reset the buffer to NOT_READY, size it, and write the data."""
        await self.set_state(BUFFER_STATE_NOT_READY)
        await self.set_size(len(data))
        await self.write(data)

    async def ready(self):
        await self.set_state(BUFFER_STATE_READY)
