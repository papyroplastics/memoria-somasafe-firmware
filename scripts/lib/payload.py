"""Build the signed model payload the firmware expects on the ML client buffer.

Mirror of ``backend/ml/payload.py`` and the parser in ``main/ml/infer.cc`` — keep the
three in sync. Layout (little-endian): u16 sig_len | sig | u16 payload_ver |
u16 signature_ver | norm_params (f32) | tflite. The ECDSA P-256 / SHA-256 signature
covers everything after it.
"""

import struct
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

PAYLOAD_VERSION = 1


def build_payload(tflite: bytes, signature_version: int, norm_bytes: bytes,
                  key_path: Path) -> bytes:
    body = struct.pack('<HH', PAYLOAD_VERSION, signature_version) + norm_bytes + tflite
    key = serialization.load_pem_private_key(Path(key_path).read_bytes(), password=None)
    signature = key.sign(body, ec.ECDSA(hashes.SHA256()))
    return struct.pack('<H', len(signature)) + signature + body
