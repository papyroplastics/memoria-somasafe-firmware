import sys
import asyncio
import secrets
import argparse
from pathlib import Path

from bleak import BleakClient, BleakScanner

from .lib.ble_common import (
    BUF_CHR_UUID, BUF_STATE_CHR_UUID, BUF_SIZE_DSC_UUID, BUF_POS_DSC_UUID,
    DEVICE_SVC_UUID, DEVICE_SIGN_CHR_UUID,
    negotiate_mtu,
)
from .lib.client_buf import ClientBuffer, BUFFER_STATE_NOT_READY
from .lib.notif_transaction import TransactionReassembler

DEV_NAME = "SomaSafe Device"

# The buffer characteristics share generic UUIDs across services, so the device
# service's copies must be picked out by walking that service specifically.
BUFFER_UUIDS = (BUF_CHR_UUID, BUF_STATE_CHR_UUID, BUF_SIZE_DSC_UUID, BUF_POS_DSC_UUID)


def device_attrs(client: BleakClient) -> dict:
    """Map the device service's buffer + signature attributes by UUID."""
    for svc in client.services:
        if svc.uuid != DEVICE_SVC_UUID:
            continue
        attrs = {}
        for chr in svc.characteristics:
            if chr.uuid in BUFFER_UUIDS or chr.uuid == DEVICE_SIGN_CHR_UUID:
                attrs[chr.uuid] = chr
            for dsc in chr.descriptors:
                if dsc.uuid in BUFFER_UUIDS:
                    attrs[dsc.uuid] = dsc

        missing = set(BUFFER_UUIDS + (DEVICE_SIGN_CHR_UUID,)) - attrs.keys()
        if missing:
            print(f"Device service missing attributes: {', '.join(missing)}", file=sys.stderr)
            exit(1)
        return attrs

    print(f"Device signing service {DEVICE_SVC_UUID} not found", file=sys.stderr)
    exit(1)


async def sign_payload(payload: bytes) -> bytes:
    """Connect, upload the payload, ready the buffer and return the signature."""
    device = await BleakScanner.find_device_by_name(DEV_NAME)
    if device is None:
        print(f"Device \"{DEV_NAME}\" not found", file=sys.stderr)
        exit(1)

    async with BleakClient(device) as client:
        print(f"Connected to {client.name} ({client.address})", file=sys.stderr)
        await negotiate_mtu(client)

        attrs = device_attrs(client)
        buffer = ClientBuffer(client, attrs)
        await buffer.start()

        reasm = TransactionReassembler()
        signature = None
        done = asyncio.Event()

        def on_signature(_, data: bytearray):
            nonlocal signature
            if done.is_set():
                return
            payload_bytes = reasm.feed(bytes(data))
            if payload_bytes is None:
                return
            signature = payload_bytes
            done.set()

        await client.start_notify(attrs[DEVICE_SIGN_CHR_UUID], on_signature)

        await buffer.upload(payload)
        await buffer.ready()

        await done.wait()
        # The task resets the buffer to NOT_READY after reading it; confirm the
        # state notification reflecting that has reached us.
        await buffer.wait_state(BUFFER_STATE_NOT_READY)
        print("Buffer reset to NOT_READY by the signing task", file=sys.stderr)

        await client.stop_notify(attrs[DEVICE_SIGN_CHR_UUID])
        return signature


def verify(pubkey_path: Path, payload: bytes, signature: bytes):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.exceptions import InvalidSignature

    pub = serialization.load_pem_public_key(pubkey_path.read_bytes())
    if not isinstance(pub, ec.EllipticCurvePublicKey):
        print(f"{pubkey_path}: expected an EC public key", file=sys.stderr)
        exit(1)

    try:
        pub.verify(signature, payload, ec.ECDSA(hashes.SHA256()))
    except InvalidSignature:
        print("Signature INVALID", file=sys.stderr)
        exit(1)
    print("Signature VALID")


def main():
    parser = argparse.ArgumentParser(
        description="Upload an arbitrary payload over BLE, have the device sign "
                    "it with its ECDSA private key, and optionally verify the "
                    "returned signature.")
    parser.add_argument('--size', type=int, default=256,
                        help="random payload size in bytes (default 256)")
    parser.add_argument('--payload', type=Path,
                        help="file to sign instead of a random payload")
    parser.add_argument('--pubkey', type=Path,
                        help="device public key (PEM) to verify the signature against")
    args = parser.parse_args()

    payload = args.payload.read_bytes() if args.payload else secrets.token_bytes(args.size)

    signature = asyncio.run(sign_payload(payload))
    print(f"Signed {len(payload)} bytes, signature ({len(signature)} bytes): {signature.hex()}")

    if args.pubkey:
        verify(args.pubkey, payload, signature)


if __name__ == '__main__':
    main()
