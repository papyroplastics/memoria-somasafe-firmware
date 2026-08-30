"""Build the model payload the firmware expects on the ML client buffer, mirroring the app's assembler (ModelPayload.kt) and the parser in ``main/ml/infer.cc`` (see shared/docs/model-signing.md)."""

import struct
from pathlib import Path

import numpy as np
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec


def norm_bytes(mean: np.ndarray, std: np.ndarray) -> bytes:
    """The client's z-score block: mean[n] then std[n], float32 LE."""
    if mean.shape != std.shape:
        raise ValueError(f"norm param shape mismatch: {mean.shape} vs {std.shape}")
    return np.concatenate([mean, std]).astype('<f4').tobytes()


def build_payload(tflite: bytes, contract_version: int, norm: bytes,
                  key_path: Path) -> bytes:
    body = struct.pack('<H', contract_version) + tflite
    key = serialization.load_pem_private_key(Path(key_path).read_bytes(), password=None)
    signature = key.sign(body, ec.ECDSA(hashes.SHA256()))
    return (struct.pack('<H', len(signature)) + signature
            + struct.pack('<H', len(norm)) + norm + body)
