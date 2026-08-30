"""Export a built firmware image and its build metadata into shared/gen/firmware/{version}/ for the backend seed script to publish."""

import argparse
import json
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

VERSION_MAX_BYTES = 32
IMAGE_MAX_BYTES = 1024 * 1024

DEFAULT_OUT_DIR = Path(__file__).resolve().parent.parent / 'shared' / 'gen' / 'firmware'


def read_version(path: Path) -> str:
    version = path.read_text().strip()
    if not version:
        sys.exit(f"{path}: empty version string")
    if len(version.encode()) > VERSION_MAX_BYTES:
        sys.exit(f"{path}: version string exceeds {VERSION_MAX_BYTES} bytes")
    return version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path, nargs='?',
                        default=Path('build/somasafe-firmware.bin'),
                        help="App image to export.")
    parser.add_argument('--interface', type=int, required=True,
                        help="BLE_INTERFACE_VERSION the image was built with.")
    parser.add_argument('--contracts', required=True,
                        help="Comma-separated ML_CONTRACT_VERSIONs the image supports.")
    parser.add_argument('--version-file', type=Path, default=Path('version.txt'),
                        help="File holding the version string baked into the image.")
    parser.add_argument('--out-dir', type=Path, default=DEFAULT_OUT_DIR,
                        help="Export root.")
    args = parser.parse_args()

    contracts = [int(c) for c in args.contracts.split(',')]
    version = read_version(args.version_file)

    if args.image.stat().st_size > IMAGE_MAX_BYTES:
        sys.exit(f"{args.image}: image exceeds the {IMAGE_MAX_BYTES}-byte OTA partition")

    out = args.out_dir / version
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.image, out / 'firmware.bin')
    (out / 'metadata.json').write_text(json.dumps({
        'version': version,
        'interface_version': args.interface,
        'supported_contracts': contracts,
        'created_at': datetime.now(timezone.utc).isoformat(),
    }, indent=2) + '\n')

    print(f"Exported {args.image} ({args.image.stat().st_size} bytes) -> {out}")


if __name__ == '__main__':
    main()
