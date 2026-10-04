"""Unpack the Oodle dependency from the official URL used by MHWNoChunk.

Input is the Warframe-distributed LZMA file. Verify the author's known MD5
before creating the DLL; never overwrite a differing existing library.
"""

import argparse
import hashlib
import lzma
from pathlib import Path


EXPECTED_MD5 = "3169b48a9a2086e53c4493c03579902c"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    data = lzma.decompress(args.source.read_bytes())
    actual = hashlib.md5(data).hexdigest()
    if actual != EXPECTED_MD5 or not data.startswith(b"MZ"):
        raise RuntimeError(f"Oodle binary does not match the author's known build: {actual}")
    if args.destination.exists():
        if args.destination.read_bytes() != data:
            raise RuntimeError(f"Existing library differs; refusing overwrite: {args.destination}")
    else:
        args.destination.parent.mkdir(parents=True, exist_ok=True)
        args.destination.write_bytes(data)
    print(f"OODLE_READY={args.destination}; MD5={actual}; bytes={len(data)}")


if __name__ == "__main__":
    main()
