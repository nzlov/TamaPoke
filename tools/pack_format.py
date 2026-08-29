"""Shared TamaPoke resource-pack envelope writer."""

from __future__ import annotations

import binascii
import struct


PACK_ABI = 7
PACK_REVISION = 1
COMMON = struct.Struct("<4sHBBIIIIHH20s")
SECTION = struct.Struct("<4sIII")


def fourcc(tag: str) -> bytes:
    raw = tag.encode("ascii")
    if len(raw) != 4:
        raise ValueError(f"section tag must be four bytes: {tag}")
    return raw


def pack_content_version(blob: bytes) -> int:
    """Return a stable version for the runtime-relevant bytes of a pack."""
    if len(blob) < COMMON.size:
        raise ValueError("pack is smaller than its common header")
    (_magic, _abi, _kind, _flags, file_size, _payload_crc, _revision,
     _mechanics_hash, header_size, section_count, _ident) = COMMON.unpack_from(blob)
    if file_size != len(blob) or header_size != COMMON.size + section_count * SECTION.size \
            or header_size > len(blob):
        raise ValueError("pack has an inconsistent header")
    # GLUE: Mirror contentReadPackInfo's wire-format identity until a future pack
    # ABI stores a content version directly. Revision is deliberately excluded;
    # the stored payload CRC represents the large payload.
    version = binascii.crc32(blob[:16])
    return binascii.crc32(blob[20:header_size], version) & 0xFFFFFFFF


def pack(kind: int, pack_id: str, mechanics_hash: int,
         sections: list[tuple[str, bytes, int]], revision: int = PACK_REVISION) -> bytes:
    encoded_id = pack_id.encode("ascii")
    if not encoded_id or len(encoded_id) > 19:
        raise ValueError("pack id must be 1..19 ASCII bytes")
    if not 0 < revision <= 0xFFFFFFFF:
        raise ValueError("pack revision must be 1..4294967295")
    header_size = COMMON.size + SECTION.size * len(sections)
    payload = bytearray()
    directory = []
    for tag, data, count in sections:
        while (header_size + len(payload)) % 4:
            payload.append(0)
        directory.append((tag, header_size + len(payload), len(data), count))
        payload.extend(data)
    crc = binascii.crc32(payload) & 0xFFFFFFFF
    file_size = header_size + len(payload)
    ident = encoded_id.ljust(20, b"\0")
    out = bytearray(COMMON.pack(
        b"TPPK", PACK_ABI, kind, 0, file_size, crc, revision,
        mechanics_hash, header_size, len(directory), ident,
    ))
    for tag, offset, size, count in directory:
        out.extend(SECTION.pack(fourcc(tag), offset, size, count))
    out.extend(payload)
    return bytes(out)
