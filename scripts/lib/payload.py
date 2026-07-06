"""Build the signed model payload the firmware expects on the ML client buffer.

Mirror of the app's assembler (ModelPayload.kt) and the parser in
``main/ml/infer.cc`` — keep the three in sync (shared/docs/model-signing.md).
Layout (little-endian, BLE interface v1): u16 sig_len | sig |
u16 contract_version | norm_params (f32) | tflite. The ECDSA P-256 / SHA-256
signature covers everything after it — the server's canonical signed bytes.
"""

import struct
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


def build_payload(tflite: bytes, contract_version: int, norm_bytes: bytes,
                  key_path: Path) -> bytes:
    body = struct.pack('<H', contract_version) + norm_bytes + tflite
    key = serialization.load_pem_private_key(Path(key_path).read_bytes(), password=None)
    signature = key.sign(body, ec.ECDSA(hashes.SHA256()))
    return struct.pack('<H', len(signature)) + signature + body
