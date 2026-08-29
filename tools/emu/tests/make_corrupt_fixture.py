#!/usr/bin/env python3
"""Create a CRC-valid but semantically invalid region pack beside valid packs."""

import binascii
import struct
import sys
from pathlib import Path


source, destination = map(Path, sys.argv[1:3])
destination.mkdir(parents=True, exist_ok=True)
for path in source.iterdir():
    if path.suffix in {".tui", ".tmove", ".titem", ".tbattle", ".tregion"}:
        (destination / path.name).symlink_to(path.resolve())

# Interrupted uploads and rollback files are not deployable packs. Keeping
# valid bytes behind those suffixes proves discovery filters by filename.
(destination / "ignored.tui.part").symlink_to((source / "ui-en-US.tui").resolve())
(destination / "ignored.tui.bak").symlink_to((source / "ui-en-US.tui").resolve())

raw = bytearray((source / "region-kanto.tregion").read_bytes())
section_count = struct.unpack_from("<H", raw, 26)[0]
header_size = struct.unpack_from("<H", raw, 24)[0]
spec_offset = None
for index in range(section_count):
    tag, offset, _size, _count = struct.unpack_from("<4sIII", raw, 48 + index * 16)
    if tag == b"SPEC":
        spec_offset = offset
        break
if spec_offset is None:
    raise SystemExit("SPEC section missing")

raw[spec_offset + 13] = 254  # invalid primary type, beyond the engine ABI
raw[28:48] = b"aaa-corrupt".ljust(20, b"\0")
struct.pack_into("<I", raw, 12, binascii.crc32(raw[header_size:]) & 0xFFFFFFFF)
(destination / "aaa-corrupt.tregion").write_bytes(raw)
