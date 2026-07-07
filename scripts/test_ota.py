"""Flash a firmware image over the BLE OTA service.

Signs the image with the server key (must match the device's factory srv_pub),
streams it through the OTA service state machine and, after the device verifies
and reboots, reconnects and reads the version characteristic to confirm the
running build changed. The image is the plain app binary produced by the build
(build/somasafe-firmware.bin).
"""

import sys
import asyncio
import argparse
from pathlib import Path

from bleak import BleakClient, BleakScanner
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

from .lib.ble_common import (
    OTA_VERSION_CHR_UUID, OTA_DATA_CHR_UUID, OTA_STATE_CHR_UUID,
    OTA_SIGNATURE_CHR_UUID,
    negotiate_mtu, discover_attributes, u8_bytes,
)

DEV_NAME = "SomaSafe Device"

OTA_STATE_IDLE      = 0
OTA_STATE_RECEIVING = 1
OTA_STATE_VERIFYING = 2
OTA_STATE_ERROR     = 0xFF

ATTR_UUIDS = (
    OTA_VERSION_CHR_UUID, OTA_DATA_CHR_UUID, OTA_STATE_CHR_UUID,
    OTA_SIGNATURE_CHR_UUID,
)

RECONNECT_DELAY = 5


def sign_image(image: bytes, key_path: Path) -> bytes:
    key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
    return key.sign(image, ec.ECDSA(hashes.SHA256()))


async def read_version(client, attrs) -> tuple[int, str]:
    data = await client.read_gatt_char(attrs[OTA_VERSION_CHR_UUID])
    return int.from_bytes(data[:2], 'little'), data[2:].decode()


async def write_chunked(client, attr, payload: bytes, label: str):
    chunk_size = client.mtu_size - 3
    written = 0
    for offset in range(0, len(payload), chunk_size):
        await client.write_gatt_char(attr, payload[offset:offset + chunk_size], response=True)
        written = offset + min(chunk_size, len(payload) - offset)
        print(f"\r{label}: {written}/{len(payload)} bytes", end='', file=sys.stderr)
    print(file=sys.stderr)


async def find_device():
    device = await BleakScanner.find_device_by_name(DEV_NAME)
    if device is None:
        print(f"Device \"{DEV_NAME}\" not found", file=sys.stderr)
        exit(1)
    return device


async def perform_update(image: bytes, signature: bytes) -> str:
    """Connect, run the OTA state machine and return the old version string."""
    device = await find_device()

    async with BleakClient(device) as client:
        print(f"Connected to {client.name} ({client.address})", file=sys.stderr)

        attrs = await discover_attributes(client, ATTR_UUIDS)
        await negotiate_mtu(client)

        interface, version = await read_version(client, attrs)
        print(f"Running firmware: version={version} interface={interface}", file=sys.stderr)

        states = asyncio.Queue()

        def on_state(_, data: bytearray):
            states.put_nowait(data[0])

        await client.start_notify(attrs[OTA_STATE_CHR_UUID], on_state)

        await client.write_gatt_char(attrs[OTA_STATE_CHR_UUID],
                                     u8_bytes(OTA_STATE_RECEIVING), response=True)

        await write_chunked(client, attrs[OTA_SIGNATURE_CHR_UUID], signature, "signature")
        await write_chunked(client, attrs[OTA_DATA_CHR_UUID], image, "image")

        await client.write_gatt_char(attrs[OTA_STATE_CHR_UUID],
                                     u8_bytes(OTA_STATE_VERIFYING), response=True)

        while True:
            state = await asyncio.wait_for(states.get(), timeout=30)
            if state == OTA_STATE_RECEIVING:
                continue
            if state == OTA_STATE_VERIFYING:
                print("Update verified, device is rebooting", file=sys.stderr)
                return version
            print(f"Update failed, device notified state {state:#x}", file=sys.stderr)
            exit(1)


async def confirm_update(old_version: str):
    print(f"Waiting {RECONNECT_DELAY}s for the device to come back up...", file=sys.stderr)
    await asyncio.sleep(RECONNECT_DELAY)
    device = await find_device()

    async with BleakClient(device) as client:
        attrs = await discover_attributes(client, (OTA_VERSION_CHR_UUID,))
        interface, version = await read_version(client, attrs)

    print(f"Rebooted firmware: version={version} interface={interface}")
    if version == old_version:
        print("WARNING: version string unchanged — same build re-flashed, or "
              "the update did not take", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description="Sign a firmware image, upload it over the BLE OTA service "
                    "and confirm the device reboots into it.")

    parser.add_argument('image', type=Path, nargs='?',
                        default=Path('build/somasafe-firmware.bin'),
                        help="app image to flash (default build/somasafe-firmware.bin)")
    parser.add_argument('--server-key', type=Path,
                        default=Path(__file__).resolve().parent.parent / 'shared' / 'gen' / 'server-private-key.pem',
                        help="ECDSA private key to sign the image with (must match the device's srv_pub)")
    parser.add_argument('--no-confirm', action='store_true',
                        help="skip reconnecting after the reboot to re-read the version")
    args = parser.parse_args()

    image = args.image.read_bytes()
    signature = sign_image(image, args.server_key)
    print(f"Image: {args.image} ({len(image)} bytes), signature: {len(signature)} bytes",
          file=sys.stderr)

    old_version = asyncio.run(perform_update(image, signature))

    if not args.no_confirm:
        asyncio.run(confirm_update(old_version))


if __name__ == '__main__':
    main()
