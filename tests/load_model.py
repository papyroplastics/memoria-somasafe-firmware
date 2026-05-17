import sys
import asyncio
import random
import socket
import hashlib
from bleak import BleakClient, BleakScanner, BleakBackend

MODEL_SIZE = 4096

DEV_NAME = "SomaSafe Device"

MODEL_CHR_UUID = "8f04f3a6-1dcb-8a86-c04d-6fa38c87f25a"
MODEL_SIZE_DSC_UUID = "237384c8-bf34-5587-ab48-ae4b0a7e2585"
MODEL_POS_DSC_UUID  = "f5565c9c-b6be-57af-484a-0f2079374bae"
MODEL_SHA_DSC_UUID  = "717e878d-f2ea-8fa0-9545-c64859bc54a0"

async def main():
    device = await BleakScanner.find_device_by_name(DEV_NAME)

    if device is None:
        print(f"Device \"{DEV_NAME}\" not found", file=sys.stderr)
        return

    # Este bloque inicia la conección, cuando el bloque se termina la conección se cierra
    async with BleakClient(device) as client: 
        print(f"Connection stablised with {client.name} address {client.address}")

        model_chr = None
        model_size_dsc = None
        model_pos_dsc  = None
        model_sha_dsc  = None

        # Imprimir los servicios del dispositivo con sus caracteristicas
        print("\nDevice Services: ")
        for svc in client.services:
            print(f"- Service \"{svc.description}\" - {svc.uuid} - {svc.handle}:")

            for chr in svc.characteristics:
                print(f"  - Characteristic \"{chr.description}\" - {chr.uuid} - {chr.handle}:")

                if chr.uuid == MODEL_CHR_UUID:
                    model_chr = chr

                for dsc in chr.descriptors:
                    print(f"    - Descriptor \"{dsc.description}\" - {dsc.uuid} - {dsc.handle}")

                    if dsc.uuid == MODEL_SIZE_DSC_UUID:
                        model_size_dsc = dsc
                    elif dsc.uuid == MODEL_POS_DSC_UUID:
                        model_pos_dsc = dsc
                    elif dsc.uuid == MODEL_SHA_DSC_UUID:
                        model_sha_dsc = dsc
            print()

        # Check attributes exist
        if model_chr is None or \
            model_size_dsc is None or \
            model_pos_dsc is None or \
            model_sha_dsc is None:

            print("Unable to find model attributes", file=sys.stderr)
            exit(1)


        # Set model size descriptor
        await client.write_gatt_descriptor(model_size_dsc, int.to_bytes(MODEL_SIZE, length=4, byteorder='little'))
        model_size_od = int.from_bytes(await client.read_gatt_descriptor(model_size_dsc), byteorder='little')

        if model_size_od != MODEL_SIZE:
            print(f"Failed to set model size {MODEL_SIZE}, read {model_size_od}")
            exit(1)

        print(f"Model size on-device: {model_size_od}")


        # Aquire MTU
        if client.backend_id == BleakBackend.BLUEZ_DBUS:
            await client._backend._acquire_mtu()  # type: ignore

            async with asyncio.timeout(5):  
                while client.mtu_size == 23:  
                    await asyncio.sleep(0.25)

        mtu = client.mtu_size
        print(f"Set MTU to {mtu}")

        # Write the model
        model_data = random.randbytes(MODEL_SIZE)
        pos = 0
        while pos < len(model_data):
            buf = model_data[pos: pos+model_chr.max_write_without_response_size]
            await client.write_gatt_char(model_chr, buf)

            pos += len(buf)

        # Check model SHA-256
        model_hash = hashlib.sha256(model_data)
        print(f"Data hash: {model_hash.hexdigest()}")

        model_hash_od = await client.read_gatt_descriptor(model_sha_dsc)
        print(f"Retrieved hash: {model_hash_od.hex()}")


asyncio.run(main())
