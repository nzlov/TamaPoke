#!/usr/bin/env python3
"""Create a structurally valid pack for low-level reader tests."""

import binascii
import struct
import sys
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))
from pack_format import PACK_ABI, pack_content_version  # noqa: E402


# Large enough to cross several of the firmware reader's 4 KiB CRC chunks.
payload = bytes(range(256)) * 80
common = struct.Struct("<4sHBBIIIIHH20s")
section = struct.Struct("<4sIII")
header_size = common.size + section.size
raw = common.pack(
    b"TPPK", PACK_ABI, 2, 0, header_size + len(payload),
    binascii.crc32(payload) & 0xFFFFFFFF, 1, 1,
    header_size, 1, b"reader-test".ljust(20, b"\0"),
)
raw += section.pack(b"TEST", header_size, len(payload), 1) + payload
assert pack_content_version(raw) == 0x502E4C39
changed_revision = bytearray(raw)
changed_revision[16] ^= 1
assert pack_content_version(changed_revision) == 0x502E4C39
changed_mechanics = bytearray(raw)
changed_mechanics[20] ^= 1
assert pack_content_version(changed_mechanics) != 0x502E4C39
Path(sys.argv[1]).write_bytes(raw)
