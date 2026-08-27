#!/usr/bin/env python3
"""Create the smallest content set needed by gender and persistence tests."""

import struct
import sys
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))

import gen_data_packs  # noqa: E402
from pack_format import pack  # noqa: E402


output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)

# Use the real move-pack builder so stored move ids remain valid in migration
# tests without requiring the release packs to be present in the worktree.
gen_data_packs.WEB_PACKS = output
gen_data_packs.build_ui_packs([])
gen_data_packs.build_move_pack([], output)

names = [b"Fixture", b"Charizard", b"Blastoise", b"Alakazam"]
name = b"".join(value + b"\0" for value in names)
name_offsets = []
offset = 0
for value in names:
    name_offsets.append(offset)
    offset += len(value) + 1
spec_record = struct.Struct("<HBBH10BIB")
spec = b"".join([
    spec_record.pack(1, 0, 1, 0xFFFF, 50, 50, 50, 50, 50, 50,
                     0, 0, 255, 0, name_offsets[0], 4),
    spec_record.pack(6, 0, 1, 0xFFFF, 78, 84, 78, 100, 109, 85,
                     0, 1, 9, 0, name_offsets[1], 1),
    spec_record.pack(9, 0, 1, 0xFFFF, 79, 83, 100, 78, 85, 105,
                     0, 2, 255, 0, name_offsets[2], 1),
    spec_record.pack(65, 0, 1, 0xFFFF, 55, 50, 45, 120, 135, 95,
                     0, 10, 255, 0, name_offsets[3], 2),
])
region = struct.pack(
    "<B16sHHB16H",
    0, b"FIXTURE".ljust(16, b"\0"), 1, 1025, 1,
    1, *([0] * 15),
)

def sprite_blob(body_color):
    pixels = bytearray([0xFF] * (16 * 16))
    for y in range(3, 14):
        half_width = 3 + (y >= 7)
        for x in range(8 - half_width, 9 + half_width):
            pixels[y * 16 + x] = 0
    for x, y in ((5, 2), (10, 2), (6, 6), (10, 6)):
        pixels[y * 16 + x] = 0
    return (b"TPK2" + bytes([1]) + struct.pack("<H", 1) +
            struct.pack("<H", body_color) + bytes([0, 16, 16, 1]) +
            struct.pack("<H", 500) + bytes(pixels))


variant_blobs = [
    sprite_blob(0x2D7F), sprite_blob(0xF7A0),
    sprite_blob(0xF94A), sprite_blob(0xFE59),
]
sprites = b"".join(variant_blobs)
variant_offsets = []
offset = 0
for blob in variant_blobs:
    variant_offsets.append(offset)
    offset += len(blob)
sprite_index = struct.pack(
    "<HIIIIIIIIB",
    1,
    variant_offsets[0], len(variant_blobs[0]),
    variant_offsets[1], len(variant_blobs[1]),
    variant_offsets[2], len(variant_blobs[2]),
    variant_offsets[3], len(variant_blobs[3]),
    4,
)
sprite_index += b"".join(
    struct.pack("<HIIIIIIIIB", dex, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for dex in (6, 9, 65)
)

gym_strings = b"A\0B\0"
trainer = struct.pack(
    "<BBBBII" + "HB" * 6,
    0, 0, 1, 0, 0, 2,
    1, 1, *([0, 0] * 5),
)
battle = struct.pack("<BBBBBBH", 0, 1, 1, 0, 0, 0, 0)
badge_index = struct.pack("<BBBBII", 0, 1, 1, 1, 0, 2)
badge_blob = struct.pack("<H", 0xFFFF) + b"\0"
thumb_pixels = variant_blobs[0][-16 * 16:]
thumb_blob = (bytes([16, 16, 1]) + struct.pack("<H", 0x2D7F) + thumb_pixels)
thumbs = b"TPTH" + struct.pack("<HI", 1, 10) + thumb_blob

blob = pack(2, "gender-fixture", 1, [
    ("SPEC", spec, 4),
    ("EVOS", b"", 0),
    ("NAME", name, 1),
    ("REGN", region, 1),
    ("SPRI", sprite_index, 4),
    ("SBLB", sprites, 1),
    ("THMB", thumbs, 1),
    ("BTTL", battle, 1),
    ("TRNR", trainer, 1),
    ("GSTR", gym_strings, 2),
    ("BADG", badge_index, 1),
    ("BBLB", badge_blob, 1),
])
(output / "gender-fixture.tregion").write_bytes(blob)
