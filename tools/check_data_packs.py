#!/usr/bin/env python3
"""Validate generated packs without trusting their generator."""

from __future__ import annotations

import binascii
import json
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PACKS = ROOT / "web" / "packs"
COMMON = struct.Struct("<4sHBBIIIIHH20s")
SECTION = struct.Struct("<4sIII")
EXT_KIND = {".tui": 1, ".tregion": 2, ".tmove": 3, ".tquiz": 4}
REQUIRED_SECTIONS = {
    1: {b"META", b"STRS", b"FONT", b"LAYT"},
    2: {b"SPEC", b"EVOS", b"NAME", b"LNAM", b"REGN", b"RLNM", b"SPRI", b"SBLB", b"THMB",
        b"LOCL", b"BTTL", b"TRNR", b"GSTR", b"BADG", b"BBLB"},
    3: {b"MOVE", b"NAME", b"LNAM", b"LOFS", b"LERN", b"TYPS", b"TSTR", b"TLNM",
        b"CHRT", b"LOCL"},
    4: {b"QLOC", b"QIDX", b"QDAT"},
}

QLOC = struct.Struct("<16sII")
QIDX = struct.Struct("<III")
QREC = struct.Struct("<BBHHHHHH")


def validate_ui_font(data: bytes, expected_count: int) -> None:
    if data[:4] == b"FNT5":
        header = struct.Struct("<4sBBBB8sI")
        if len(data) < header.size:
            raise ValueError("truncated FNT5 section")
        magic, font_format, _face, size_count, _flags, sizes, font_size = header.unpack_from(data)
        active_sizes = sizes[:size_count]
        if magic != b"FNT5" or font_format != 1 or expected_count != 1 or \
                not 0 < size_count <= len(sizes) or \
                not all(active_sizes) or any(left >= right for left, right in zip(active_sizes, active_sizes[1:])):
            raise ValueError("invalid FNT5 header")
        if font_size != len(data) - header.size or data[header.size:header.size + 4] not in \
                (b"OTTO", b"ttcf", b"\0\1\0\0"):
            raise ValueError("invalid FNT5 OpenType payload")
        return
    header = struct.Struct("<4sBBH")
    record = struct.Struct("<IBBBbb128s")
    if len(data) < header.size:
        raise ValueError("truncated FONT section")
    magic, line_height, design_height, count = header.unpack_from(data)
    if (magic, line_height > 0, design_height > 0) != (b"FNT4", True, True):
        raise ValueError("invalid FNT4 header")
    if len(data) != header.size + count * record.size:
        raise ValueError("invalid FNT4 size")
    if count != expected_count:
        raise ValueError("FNT4 directory count mismatch")
    previous = -1
    for index in range(count):
        codepoint, width, height, advance, _xoff, _yoff, alpha4 = record.unpack_from(
            data, header.size + index * record.size)
        if codepoint <= previous or not 0 < width <= 16 or not 0 < height <= 16 or not advance:
            raise ValueError("invalid FNT4 glyph")
        previous = codepoint
        if any((byte & 0x0F) not in (0, 15) or (byte >> 4) not in (0, 15)
               for byte in alpha4):
            raise ValueError("FNT4 bitmap font contains unexpected gray coverage")


def validate_localized_strings(data: bytes, expected_items: int) -> None:
    if len(data) < 4:
        raise ValueError("truncated localized string table")
    locale_count, item_count = struct.unpack_from("<HH", data)
    header_size = 4 + locale_count * 28
    if not locale_count or item_count != expected_items or header_size > len(data):
        raise ValueError("invalid localized string header")
    locales = set()
    for locale_index in range(locale_count):
        row = 4 + locale_index * 28
        code, index_at, blob_at, blob_size = struct.unpack_from("<16sIII", data, row)
        locale = code.split(b"\0", 1)[0]
        table_size = (item_count + 1) * 4
        if not locale or locale in locales or index_at + table_size > len(data) or \
                blob_at > len(data) or blob_size > len(data) - blob_at:
            raise ValueError("invalid localized string locale")
        locales.add(locale)
        offsets = struct.unpack_from(f"<{item_count + 1}I", data, index_at)
        if offsets[0] != 0 or offsets[-1] != blob_size or \
                any(left > right for left, right in zip(offsets, offsets[1:])):
            raise ValueError("invalid localized string offsets")
        blob = data[blob_at:blob_at + blob_size]
        for left, right in zip(offsets, offsets[1:]):
            if left == right or blob[right - 1] != 0 or b"\0" in blob[left:right - 1]:
                raise ValueError("invalid localized string value")


def validate_quiz(sections: dict[bytes, bytes], counts: dict[bytes, int]) -> None:
    locales, index, data = sections[b"QLOC"], sections[b"QIDX"], sections[b"QDAT"]
    if not counts[b"QLOC"] or not counts[b"QIDX"] or counts[b"QDAT"] != counts[b"QIDX"] or \
            len(locales) != counts[b"QLOC"] * QLOC.size or \
            len(index) != counts[b"QIDX"] * QIDX.size:
        raise ValueError("invalid question-pack index sizes")
    covered = 0
    for offset in range(0, len(locales), QLOC.size):
        code, first, count = QLOC.unpack_from(locales, offset)
        locale = code.split(b"\0", 1)[0]
        if not locale or first != covered or not count or first + count > counts[b"QIDX"]:
            raise ValueError("invalid question-pack locale span")
        covered += count
    if covered != counts[b"QIDX"]:
        raise ValueError("question-pack locale spans do not cover the index")
    previous_end = 0
    for number in range(counts[b"QIDX"]):
        _identity, start, size = QIDX.unpack_from(index, number * QIDX.size)
        if start < previous_end or size < QREC.size or start + size > len(data):
            raise ValueError("question-pack record is outside QDAT")
        record = data[start:start + size]
        option_count, answer, id_size, stem_size, *option_sizes = QREC.unpack_from(record)
        if not 2 <= option_count <= 4 or answer >= option_count or \
                not 0 < id_size <= 40 or not 0 < stem_size <= 768 or \
                any(not 0 < length <= 192 for length in option_sizes[:option_count]) or \
                any(option_sizes[option_count:]) or \
                QREC.size + id_size + stem_size + sum(option_sizes) != size:
            raise ValueError("invalid question-pack record")
        strings = record[QREC.size:]
        if b"\0" in strings:
            raise ValueError("question-pack records must not contain NUL")
        previous_end = start + size


def validate(path: Path, expected: dict) -> None:
    raw = path.read_bytes()
    if len(raw) < COMMON.size:
        raise ValueError(f"{path.name}: truncated common header")
    (magic, abi, kind, _flags, file_size, payload_crc, revision,
     mechanics_hash, header_size, section_count, ident) = COMMON.unpack_from(raw)
    pack_id = ident.split(b"\0", 1)[0].decode("ascii")
    if magic != b"TPPK" or abi != expected["abi"] or kind != EXT_KIND[path.suffix]:
        raise ValueError(f"{path.name}: incompatible common header")
    if file_size != len(raw) or header_size != COMMON.size + section_count * SECTION.size:
        raise ValueError(f"{path.name}: inconsistent size")
    if pack_id != expected["id"] or revision != expected["revision"]:
        raise ValueError(f"{path.name}: manifest identity mismatch")
    if payload_crc != binascii.crc32(raw[header_size:]) & 0xFFFFFFFF:
        raise ValueError(f"{path.name}: payload CRC mismatch")
    if expected["crc32"] != f"{binascii.crc32(raw) & 0xFFFFFFFF:08x}":
        raise ValueError(f"{path.name}: download CRC mismatch")
    ranges = []
    tags = set()
    sections = {}
    section_counts = {}
    for index in range(section_count):
        tag, offset, size, count = SECTION.unpack_from(raw, COMMON.size + index * SECTION.size)
        if tag in tags:
            raise ValueError(f"{path.name}: duplicate section {tag!r}")
        tags.add(tag)
        if offset < header_size or offset + size > len(raw):
            raise ValueError(f"{path.name}: section outside file")
        ranges.append((offset, offset + size))
        sections[tag] = raw[offset:offset + size]
        section_counts[tag] = count
    ranges.sort()
    if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
        raise ValueError(f"{path.name}: overlapping sections")
    if tags != REQUIRED_SECTIONS[kind]:
        raise ValueError(f"{path.name}: unexpected section set")
    if kind == 1:
        validate_ui_font(sections[b"FONT"], section_counts[b"FONT"])
    elif kind == 2:
        validate_localized_strings(sections[b"RLNM"], section_counts[b"RLNM"])
    elif kind == 4:
        validate_quiz(sections, section_counts)
    if kind in (2, 3) and mechanics_hash == 0:
        raise ValueError(f"{path.name}: mechanics pack has no fingerprint")


def main() -> int:
    index = json.loads((PACKS / "index.json").read_text(encoding="utf-8"))
    if index.get("schema") != 1 or index.get("packAbi") != 2:
        raise SystemExit("unsupported index schema")
    packages = index.get("packages", [])
    ids = {item["id"] for item in packages}
    if len(ids) != len(packages):
        raise SystemExit("duplicate package id")
    for item in packages:
        missing = set(item.get("requires", [])) - ids
        if missing:
            raise SystemExit(f"{item['id']}: missing dependencies {sorted(missing)}")
        path = PACKS / item["file"]
        validate(path, item)
        print(f"PASS {item['id']:<16} {item['kind']:<4} {item['size']:>9} bytes")
    print(f"all {len(packages)} packs are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
