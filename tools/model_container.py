#!/usr/bin/env python3
"""Create the fixed external-Flash container used by camera_diag M10."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


FLASH_SIZE = 0x400000
CONTAINER_OFFSET = 0xE0000
MANIFEST_SIZE = 0x1000
MODEL_OFFSET = MANIFEST_SIZE
MAGIC = b"OVM10\0\0\0"
VERSION = 1
HEADER_FORMAT = "<8sIIII32s"


def make_container(model: bytes) -> bytes:
    if len(model) < 8 or model[4:8] != b"TFL3":
        raise ValueError("input is not a TFLite FlatBuffer (missing TFL3 at byte 4)")

    available = FLASH_SIZE - CONTAINER_OFFSET - MODEL_OFFSET
    if len(model) > available:
        raise ValueError(f"model is {len(model)} bytes; only {available} fit in the container")

    manifest = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        MANIFEST_SIZE,
        MODEL_OFFSET,
        len(model),
        hashlib.sha256(model).digest(),
    )
    return manifest.ljust(MANIFEST_SIZE, b"\0") + model


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Package one TFLite FlatBuffer for ESP32-P4 external Flash at 0xE0000."
    )
    parser.add_argument("input", type=Path, help="source .tflite")
    parser.add_argument("output", type=Path, help="container binary to write at Flash offset 0xE0000")
    args = parser.parse_args()

    model = args.input.read_bytes()
    container = make_container(model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(container)

    print(f"input={args.input}")
    print(f"model_bytes={len(model)}")
    print(f"model_sha256={hashlib.sha256(model).hexdigest()}")
    print(f"container={args.output}")
    print(f"container_bytes={len(container)}")
    print(f"flash_offset=0x{CONTAINER_OFFSET:X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
