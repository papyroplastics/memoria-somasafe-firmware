import sys
import asyncio

from bleak import BleakClient, BleakBackend

# Characteristic / descriptor UUIDs exposed by the firmware
# (see main/ble/client_buffer.c and main/ml/service.c).
BUF_CHR_UUID        = "8f04f3a6-1dcb-8a86-c04d-6fa38c87f25a"
BUF_STATE_CHR_UUID  = "794d330a-8c90-229b-b94c-d1025b1c7d19"
BUF_SIZE_DSC_UUID   = "237384c8-bf34-5587-ab48-ae4b0a7e2585"
BUF_POS_DSC_UUID    = "f5565c9c-b6be-57af-484a-0f2079374bae"
ML_RESULTS_CHR_UUID = "7228d086-4fc1-4b9c-fa4d-1f715ac23c54"
ML_ERRORS_CHR_UUID  = "9c8b8c42-5a25-6ea5-c642-8a7acf1f330e"


def u8_bytes(num: int) -> bytes:
    return int.to_bytes(num, length=1, byteorder='little')


def u32_bytes(num: int) -> bytes:
    return int.to_bytes(num, length=4, byteorder='little')


async def negotiate_mtu(client: BleakClient):
    if client.backend_id == BleakBackend.BLUEZ_DBUS:
        await client._backend._acquire_mtu()  # type: ignore
        async with asyncio.timeout(5):
            while client.mtu_size == 23:
                await asyncio.sleep(0.25)

    print(f"MTU set to {client.mtu_size}", file=sys.stderr)


async def discover_attributes(client: BleakClient, uuids) -> dict:
    """Walk the GATT table and return a {uuid: attribute} dict.

    uuids is an iterable of characteristic/descriptor UUIDs to locate; the
    returned dict maps each one to its BleakGATTCharacteristic/Descriptor.
    Exits if any requested UUID is missing on the connected device.
    """
    wanted = set(uuids)
    found = {}

    print("\nDevice services:", file=sys.stderr)
    for svc in client.services:
        print(f"- Service \"{svc.description}\" - {svc.uuid} - {svc.handle}", file=sys.stderr)
        for chr in svc.characteristics:
            print(f"  - Characteristic \"{chr.description}\" - {chr.uuid} - {chr.handle}", file=sys.stderr)
            if chr.uuid in wanted:
                found[chr.uuid] = chr
            for dsc in chr.descriptors:
                print(f"    - Descriptor \"{dsc.description}\" - {dsc.uuid} - {dsc.handle}", file=sys.stderr)
                if dsc.uuid in wanted:
                    found[dsc.uuid] = dsc

    missing = wanted - found.keys()
    if missing:
        print(f"Unable to find attributes: {', '.join(missing)}", file=sys.stderr)
        exit(1)

    return found
