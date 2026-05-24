import sys
import asyncio
import pathlib
from bleak import BleakClient, BleakScanner, BleakBackend

DEV_NAME = "SomaSafe Device"

MODEL_CHR_UUID = "8f04f3a6-1dcb-8a86-c04d-6fa38c87f25a"
MODEL_STATE_CHR_UUID = "794d330a-8c90-229b-b94c-d1025b1c7d19"
MODEL_SIZE_DSC_UUID = "237384c8-bf34-5587-ab48-ae4b0a7e2585"
MODEL_POS_DSC_UUID  = "f5565c9c-b6be-57af-484a-0f2079374bae"

BUFFER_STATE_NOT_READY = 0
BUFFER_STATE_READY = 1

if len(sys.argv) != 2:
    print(f"USE: python {sys.argv[0]} <model file>")
    exit(1)

model_file = pathlib.Path(sys.argv[1])
model_bytes = model_file.read_bytes()

async def main():
    device = await BleakScanner.find_device_by_name(DEV_NAME)

    if device is None:
        print(f"Device \"{DEV_NAME}\" not found", file=sys.stderr)
        return

    # Este bloque inicia la conección, cuando el bloque se termina la conección se cierra
    async with BleakClient(device) as client: 
        print(f"Connection stablised with {client.name} address {client.address}")

        model_chr = None
        model_state_chr = None
        model_size_dsc = None
        model_pos_dsc  = None

        # Imprimir los servicios del dispositivo con sus caracteristicas
        print("\nDevice Services: ")
        for svc in client.services:
            print(f"- Service \"{svc.description}\" - {svc.uuid} - {svc.handle}:")

            for chr in svc.characteristics:
                print(f"  - Characteristic \"{chr.description}\" - {chr.uuid} - {chr.handle}:")

                if chr.uuid == MODEL_CHR_UUID:
                    model_chr = chr
                elif chr.uuid == MODEL_STATE_CHR_UUID:
                    model_state_chr = chr

                for dsc in chr.descriptors:
                    print(f"    - Descriptor \"{dsc.description}\" - {dsc.uuid} - {dsc.handle}")

                    if dsc.uuid == MODEL_SIZE_DSC_UUID:
                        model_size_dsc = dsc
                    elif dsc.uuid == MODEL_POS_DSC_UUID:
                        model_pos_dsc = dsc
            print()

        # Check attributes exist
        if model_chr is None or \
            model_state_chr is None or \
            model_size_dsc is None or \
            model_pos_dsc is None:

            print("Unable to find model attributes", file=sys.stderr)
            exit(1)

        await client.write_gatt_char(model_state_chr, bytes([BUFFER_STATE_NOT_READY]), response=True)

        # Set model size descriptor
        await client.write_gatt_descriptor(model_size_dsc, int.to_bytes(len(model_bytes), length=4, byteorder='little'))
        model_size_od = int.from_bytes(await client.read_gatt_descriptor(model_size_dsc), byteorder='little')

        if model_size_od != len(model_bytes):
            print(f"Failed to set model size {len(model_bytes)}, read {model_size_od}")
            exit(1)

        print(f"Model size on-device: {model_size_od}")


        # Aquire MTU
        if client.backend_id == BleakBackend.BLUEZ_DBUS:
            await client._backend._acquire_mtu()  # type: ignore

            async with asyncio.timeout(5):  
                while client.mtu_size == 23:  
                    await asyncio.sleep(0.25)

        mtu = client.mtu_size
        print(f"MTU set to {mtu}")

        # Write the model
        pos = 0
        while pos < len(model_bytes):
            buf = model_bytes[pos: pos+model_chr.max_write_without_response_size]
            await client.write_gatt_char(model_chr, buf)

            pos += len(buf)

        print(f"Finished writing model")

        await client.write_gatt_descriptor(model_pos_dsc, int.to_bytes(0, length=4, byteorder='little'))
        read_data = bytearray()
        while len(read_data) < len(model_bytes):
            chunk = await client.read_gatt_char(model_chr)
            if not chunk:
                break
            read_data.extend(chunk)

        if read_data[:len(model_bytes)] == model_bytes:
            print("Model readback matches written data")
        else:
            breakpoint()
            print("Model readback mismatch", file=sys.stderr)

        await client.write_gatt_char(model_state_chr, bytes([BUFFER_STATE_READY]), response=True)

asyncio.run(main())
