# SomaSafe firmware components

`somasafe-firmware` is the embedded ESP-IDF component for the SomaSafe ecosystem.  
It runs on ESP32, exposes BLE services for sensor and model-transfer workflows, and runs
on-device inference with TensorFlow Lite Micro.

## Role in SomaSafe

See [`shared/docs/architecture.md`](shared/docs/architecture.md) for the full system
design. In short: a BLE peripheral for the Android app (`application/`), which is its
only external interface — the ESP32 never touches the internet directly. It streams
PPG/ACC, receives models over BLE, and runs int8 inference with TFLite Micro. The Android
app implements this BLE contract (see `application/README.md`); the Python scripts under
`scripts/` exercise the same services independently, useful for testing without the app
(`test_model.py` drives model-transfer + results end to end, `serial_write.py` feeds
sensor data over UART). Both replay the same `.ssds` subject export the backend writes to
`shared/gen/exports/` — see [Test harness](#test-harness).

## Project structure

Firmware code is organized by domain under `main/`:

```txt
main/
  main.c      App entrypoint (NVS init + task startup).
  ble/        NimBLE host/GAP/GATT setup, the generic client-writable buffer used by
              both the model-transfer and device-signing services, and the OTA
              firmware-update service.
  device/     Device service: ECDSA-signs a client-uploaded payload and notifies the
              signature back; read-only serial-number characteristic for attestation.
  utils/      Generic helpers shared across domains: worker task/queue, SHA-256
              (one-shot + streaming), ECDSA P-256 sign/verify, factory-NVS accessors,
              a slice ring buffer, and the notification reconstruction layer
              (fragments a payload across MTU-sized notifications).
  ppg/        Sensor task (UART reader, ring buffer) and the BLE PPG streaming service.
  ml/         Model feeding contract, 17-feature extraction (esp-dsp), and the TFLM
              inference task: verify+parse signed model payload -> extract -> normalize
              -> infer -> notify.
```

`scripts/` holds the Python test harness (`gen_factory_nvs.py`, `serial_write.py`,
`test_model.py`, `test_sign.py`, `test_ota.py`, and a `lib/` mirroring the on-device BLE contract
client-side, including the `.ssds` export reader) plus `export_image.py`, which publishes a
built image for backend OTA distribution — see the docstrings/`--help` of each for usage.
Other top-level files:
`managed_components/` (downloaded ESP-IDF dependencies), `dependencies.lock` (locked
versions), `sdkconfig.defaults` (baseline SDK config).

## Current capabilities

See [`shared/docs/ble-protocol.md`](shared/docs/ble-protocol.md) for the GATT service
overview and the notification reconstruction layer, and
[`shared/docs/model-signing.md`](shared/docs/model-signing.md) for the signed model
payload. Firmware-specific behavior:

- ESP-IDF + NimBLE initialization, GAP advertising, and LE Secure Connections link
  security (encrypted + MITM-authenticated via passkey, bonding) are all wired up.
- **PPG service**: reads raw float samples from UART (2 BVP @ 64 Hz + 1 ACC magnitude @
  32 Hz per cycle) and notifies them every second, fragmented to the MTU. The host stream
  stands in for the real sensor and is entirely unframed — no handshake, no ack — so the
  ESP must be restarted before each host run for its window sequence numbers to line up
  with the start of the recording. Each completed 8-second window (512 PPG + 256 ACC
  floats) is pushed into a ring buffer with a monotonically increasing sequence number for
  downstream consumers.
- **Model-transfer / client buffer**: size/position descriptors, chunked writes, and a
  READY/NOT_READY state characteristic that gates writes and notifies on change; a
  consumer that finishes reading a readied buffer resets it immediately, releasing the
  lock.
- **ML service**: one inference per 8-second window via TFLM FeatureMLP. On-device
  17-feature extraction (esp-dsp: time-domain stats + FFT-based spectral features, Hann
  window, hardware-accelerated `dsps_fft2r_fc32`) — see
  [`shared/docs/model-types.md`](shared/docs/model-types.md) for the feature list, shared
  verbatim with the backend and the app. Features are z-scored with the per-wearer mean/std
  the client supplies alongside the model (unsigned — the server has no say in them), then
  quantized to feed the int8 model; the float32 feature vector and the int8 score are
  notified back over BLE **raw**, so the phone receives features it can reproduce itself.
  With `ENABLE_INSTRUMENTATION` (`main/common.h`) the task also reports the TFLM arena
  high-water mark once per model load and a rolling mean/max of the extraction and
  inference latencies every 16 windows.
- **Device service**: signs an arbitrary-length uploaded payload with the factory device
  key and notifies the DER signature back; a read-only characteristic returns the device
  serial. See [`shared/docs/device-attestation.md`](shared/docs/device-attestation.md).
- **OTA service**: firmware updates over BLE (`ble/ota.c`). The client drives a
  state characteristic (start / abort / finalize, notified on change), streaming
  the app image and its ECDSA signature through two write-only characteristics.
  The image is hashed incrementally as it lands in the inactive OTA slot and
  verified against the factory-provisioned `srv_pub` (ECDSA P-256/SHA-256, same
  key that signs models — no secure boot, no rollback); on success the device
  switches the boot partition and restarts. The phone is authoritative: the
  device performs no version checks. A read-only version characteristic returns
  `BLE_INTERFACE_VERSION` plus the running app version string. The image is the
  plain `build/somasafe-firmware.bin` the build produces; `scripts/test_ota.py`
  signs and flashes it end to end, and `make export-image` publishes it (plus a
  metadata JSON: version string from `version.txt`, interface version, supported
  model-contract versions) to `shared/gen/firmware/{version}/`, where the backend
  seed script picks it up for distribution through the `/ota` endpoints.
- Model verification (`ml/infer.cc`): the uploaded payload is rejected
  (`ML_ERR_PAYLOAD`, buffer invalidated) unless its normalization block is the length its
  contract fixes, its ECDSA P-256/SHA-256 signature verifies against the
  factory-provisioned `srv_pub`, and the signed contract version matches `ml/contract.h`
  (which fixes that norm-param length and the model's input shape). The signature covers
  `contract_version ‖ tflite` only, and those sit last in the payload so the device
  verifies them in place — the norm params ahead of them are the wearer's own and are not
  signed.

## Build and run

`Makefile` helpers:

- `make shared` → links `../shared` (monorepo layout) or, when absent, clones
  [`memoria-somasafe-shared`](https://github.com/papyroplastics/memoria-somasafe-shared.git)
  into `shared/` (gitignored) so the project builds standalone. It also runs the shared
  makefile, which generates the `.ssds` protobuf bindings the harness imports as
  `shared.gen.code.dataset_pb2` (needs `protoc` on the PATH).
- `make` / `make build` → `idf.py build`
- `make run` → `idf.py flash monitor`
- `make debug`, `make gdb`, `make qemu`
- `make export-image` → publish the built image to `shared/gen/firmware/` for
  backend OTA distribution
- `make serial-write` / `make test-model` → the two halves of the test harness below

## Test harness

Both halves replay one subject export written by the backend
(`cd backend && uv run -m scripts.system.export_subject_data 1` →
`shared/gen/exports/S1.ssds`), which must be gapless — no `--missing-*`, or the device's
window sequence numbers stop indexing the file.

```bash
# 1. restart the ESP (the stream is unframed; this is what aligns sequence number 0)
# 2. stand in for the sensor
uv run -m scripts.serial_write /dev/serial/by-id/usb-1a86_USB_Single_Serial_... \
    shared/gen/exports/S1.ssds --rate=900
# 3. stand in for the phone: sign + upload the model, score what comes back
uv run -m scripts.test_model shared/gen/models/feature-mlp/quantized.tflite \
    shared/gen/exports/S1.ssds --results=60
```

`--rate` is the cycle rate in Hz; 32 is real time and ~900 is as fast as 115200 baud
carries. `test_model.py` prints, per window, the label from the export against the
device's own prediction, plus the MSE between the device's feature vector and the exported
one — that MSE is the check that the on-device extractor still matches the backend's.

Link security applies to the harness as much as to the phone, so the host has to pair with
the passkey the device logs to its console before any of this works. For host-driven runs
it is usually simpler to set `SMP_SECURITY_LEVEL` to 0 in `main/ble/host.h` and rebuild,
which drops the requirement entirely.

## Factory identity provisioning

Device identity lives in a dedicated `factory_data` NVS partition (`partitions.csv`), kept
separate from the default `nvs` so a full/erased default partition never touches the
provisioned identity. See [`shared/docs/device-attestation.md`](shared/docs/device-attestation.md)
for what each stored key is used for; under the `factory` namespace it holds `serial`,
`dev_priv`, `esp_pub` and `srv_pub`.

`scripts/gen_factory_nvs.py` produces the `factory_nvs.csv` definition consumed by
`nvs_create_partition_image` (wired in `main/CMakeLists.txt`, flashed with `idf.py
flash`). Pass the server public key and device private key as PEM filenames; with only
the server key the device key is generated and printed; with no arguments it generates a
device key and reuses its public key as the server's (testing only — sign/verify resolve
to the same keypair). The build auto-generates a throwaway test identity if no
`factory_nvs.csv` exists yet. A serial can be pinned with `--serial SN<12 hex>` instead of
the random default. The device signing service (`device/`) reads `dev_priv` from this
partition and signs payloads with `utils/ecdsa_utils`; `scripts/test_sign.py` exercises it
end to end.

