"""Generate the factory NVS partition definition (serial + ECDSA P-256 keys).

The resulting CSV is consumed by `nvs_create_partition_image` (see
main/CMakeLists.txt) to build the image flashed onto the `factory_data`
partition. It holds, under the `factory` namespace:

  serial    string   device serial number
  dev_priv  hex2bin  device private key, raw 32-byte P-256 scalar
  srv_pub   hex2bin  server public key, 65-byte uncompressed point (0x04|X|Y)

Keys are passed as filenames (PEM):
  - 2 args: <server_pub> <esp_priv>  use both as given.
  - 1 arg:  <server_pub>             the device key is generated and its
                                     private/public PEM are printed.
  - 0 args: generate a device key and reuse its public key as the server's.
            This is only meant for testing (sign and verify resolve to the same
            keypair) and prints a warning.
"""

import argparse
import secrets
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
NAMESPACE = "factory"


def load_private(path: Path) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise SystemExit(f"{path}: expected a P-256 private key")
    return key


def load_public(path: Path) -> ec.EllipticCurvePublicKey:
    key = serialization.load_pem_public_key(path.read_bytes())
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise SystemExit(f"{path}: expected a P-256 public key")
    return key


def priv_bytes(key: ec.EllipticCurvePrivateKey) -> bytes:
    return key.private_numbers().private_value.to_bytes(32, "big")


def pub_bytes(key: ec.EllipticCurvePublicKey) -> bytes:
    return key.public_bytes(
        serialization.Encoding.X962,
        serialization.PublicFormat.UncompressedPoint,
    )


def print_pem(key: ec.EllipticCurvePrivateKey) -> None:
    print(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        ).decode()
    )
    print(
        key.public_key()
        .public_bytes(
            serialization.Encoding.PEM,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
        .decode()
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server_pub", type=Path, nargs="?", help="server public key (PEM)")
    parser.add_argument("esp_priv", type=Path, nargs="?", help="device private key (PEM)")
    parser.add_argument(
        "--out",
        type=Path,
        default=FIRMWARE_DIR / "factory_nvs.csv",
        help="output CSV path (default: firmware/factory_nvs.csv)",
    )
    args = parser.parse_args()

    if args.esp_priv is not None:
        dev_key = load_private(args.esp_priv)
        srv_key = load_public(args.server_pub)
    elif args.server_pub is not None:
        dev_key = ec.generate_private_key(ec.SECP256R1())
        srv_key = load_public(args.server_pub)
        print("Generated device key:")
        print_pem(dev_key)
    else:
        dev_key = ec.generate_private_key(ec.SECP256R1())
        srv_key = dev_key.public_key()
        print(
            "WARNING: no keys given; the device key's public key is reused as the "
            "server key. Signatures will verify against the device itself. Use this "
            "for testing only."
        )
        print("Generated device key:")
        print_pem(dev_key)

    serial = "SN" + secrets.token_hex(6).upper()
    print(f"Serial number: {serial}")

    rows = [
        ("key", "type", "encoding", "value"),
        (NAMESPACE, "namespace", "", ""),
        ("serial", "data", "string", serial),
        ("dev_priv", "data", "hex2bin", priv_bytes(dev_key).hex()),
        ("srv_pub", "data", "hex2bin", pub_bytes(srv_key).hex()),
    ]
    args.out.write_text("\n".join(",".join(r) for r in rows) + "\n")
    print(f"Written {args.out}")


if __name__ == "__main__":
    main()
