#!/usr/bin/env python3
"""Validate generated packs without trusting their generator."""

from __future__ import annotations

import binascii
import json
import struct
from pathlib import Path

from pmd_layout import pmd_display_scale, pmd_pair_display_scale
from pack_format import PACK_ABI


ROOT = Path(__file__).resolve().parent.parent
PACKS = ROOT / "web" / "packs"
COMMON = struct.Struct("<4sHBBIIIIHH20s")
SECTION = struct.Struct("<4sIII")
EXT_KIND = {".tui": 1, ".tregion": 2, ".tmove": 3, ".tquiz": 4}
REQUIRED_SECTIONS = {
    1: {b"META", b"STRS", b"FONT", b"LAYT"},
    2: {b"SPEC", b"ASLT", b"EVOS", b"NAME", b"LNAM", b"REGN", b"RLNM", b"SPRI", b"SBLB", b"THMB",
        b"LOCL", b"BTTL", b"TRNR", b"GSTR", b"BADG", b"BBLB"},
    3: {b"MOVE", b"MFLG", b"MTAG", b"NAME", b"LNAM", b"LOFS", b"LERN", b"TYPS", b"TSTR", b"TLNM",
        b"CHRT", b"LOCL", b"ITEM", b"INAM", b"ILNM", b"ILOC", b"ABIL", b"ANAM", b"ALNM",
        b"ALOC", b"MEGA", b"GMAX"},
    4: {b"QLOC", b"QIDX", b"QDAT"},
}
OPTIONAL_SECTIONS = {2: {b"MFSP", b"MFBL"}, 3: {b"IICO"}}

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
    required = REQUIRED_SECTIONS[kind]
    extras = tags - required
    if not required.issubset(tags) or extras - OPTIONAL_SECTIONS.get(kind, set()):
        raise ValueError(f"{path.name}: unexpected section set")
    if kind == 1:
        validate_ui_font(sections[b"FONT"], section_counts[b"FONT"])
    elif kind == 2:
        validate_localized_strings(sections[b"RLNM"], section_counts[b"RLNM"])
        species_count = section_counts[b"SPEC"]
        ability_slot_record = struct.Struct("<HHHH")
        if not species_count or section_counts[b"ASLT"] != species_count or \
                len(sections[b"ASLT"]) != species_count * ability_slot_record.size:
            raise ValueError("invalid species ability slot table")
        previous_species = 0
        for offset in range(0, len(sections[b"ASLT"]), ability_slot_record.size):
            species, slot_one, slot_two, hidden = ability_slot_record.unpack_from(
                sections[b"ASLT"], offset)
            if species <= previous_species or not slot_one:
                raise ValueError("invalid species ability slot record")
            previous_species = species
        sprite_record = struct.Struct("<HIIIIIIIIB")
        sprite_count = section_counts[b"SPRI"]
        if not sprite_count or len(sections[b"SPRI"]) != sprite_count * sprite_record.size:
            raise ValueError("invalid regional sprite index")
        previous_species = 0
        sprite_blob = sections[b"SBLB"]
        for offset in range(0, len(sections[b"SPRI"]), sprite_record.size):
            (species, normal_at, normal_size, shiny_at, shiny_size,
             female_at, female_size, female_shiny_at, female_shiny_size,
             display_scale) = sprite_record.unpack_from(sections[b"SPRI"], offset)
            if species <= previous_species or normal_at > len(sprite_blob) or \
                    normal_size > len(sprite_blob) - normal_at or \
                    shiny_at > len(sprite_blob) or shiny_size > len(sprite_blob) - shiny_at or \
                    female_at > len(sprite_blob) or female_size > len(sprite_blob) - female_at or \
                    female_shiny_at > len(sprite_blob) or \
                    female_shiny_size > len(sprite_blob) - female_shiny_at or \
                    (not normal_size and
                     (shiny_size or female_size or female_shiny_size or display_scale)) or \
                    (female_shiny_size and not female_size):
                raise ValueError("invalid regional sprite record")
            normal = sprite_blob[normal_at:normal_at + normal_size]
            shiny = sprite_blob[shiny_at:shiny_at + shiny_size]
            female = sprite_blob[female_at:female_at + female_size]
            female_shiny = sprite_blob[female_shiny_at:female_shiny_at + female_shiny_size]
            expected_scale = min((pmd_display_scale(blob)
                                  for blob in (normal, shiny, female, female_shiny) if blob),
                                 default=0)
            if display_scale != expected_scale:
                raise ValueError("regional sprite display scale does not match Idle bounds")
            previous_species = species
    elif kind == 3:
        move_count = section_counts[b"MOVE"]
        if len(sections[b"MOVE"]) != move_count * 17 or \
                len(sections[b"MFLG"]) != move_count or \
                section_counts[b"MFLG"] != move_count or \
                any(flag & ~0x7F for flag in sections[b"MFLG"]):
            raise ValueError("invalid move field flags")
        if len(sections[b"MTAG"]) != move_count * 2 or \
                section_counts[b"MTAG"] != move_count:
            raise ValueError("invalid move tags")
        move_tags = struct.unpack(f"<{move_count}H", sections[b"MTAG"])
        if any(tags & ~0x07FF for tags in move_tags):
            raise ValueError("invalid move tags")
        item_record = struct.Struct("<HBBBBhHBBI")
        item_count = section_counts[b"ITEM"]
        if not item_count or len(sections[b"ITEM"]) != item_count * item_record.size:
            raise ValueError("invalid item table size")
        keys = []
        for offset in range(0, len(sections[b"ITEM"]), item_record.size):
            key, category, effect, rarity, flags, param, weight, daily, reserved, _name = \
                item_record.unpack_from(sections[b"ITEM"], offset)
            training_item = effect == 6
            battle_boost = effect == 7
            battle_mechanic = effect == 8
            catch_item = effect == 1
            move_stone = effect == 10
            if not key or not 1 <= category <= 9 or not 1 <= effect <= 10 or \
                    not 1 <= rarity <= 4 or daily > 99 or reserved:
                raise ValueError("invalid item record")
            if training_item and (category != 6 or flags not in (1, 2, 16) or param <= 0):
                raise ValueError("invalid training tonic")
            if catch_item and (category != 1 or (param <= 0 and param != -1)):
                raise ValueError("invalid capture item")
            if battle_boost and (category != 7 or flags not in (1, 2, 4, 8, 16) or
                                 not 1 <= param <= 6):
                raise ValueError("invalid battle booster")
            if battle_mechanic and (category != 8 or flags not in (1, 2, 3) or
                                    (flags != 3 and param) or not 0 <= param <= 3 or
                                    weight or daily):
                raise ValueError("invalid battle mechanic item")
            if effect == 9 and (category != 5 or flags or param or daily):
                raise ValueError("invalid Max Soup item")
            if move_stone and (category != 9 or flags or param or daily):
                raise ValueError("invalid move stone item")
            keys.append(key)
        if len(keys) != len(set(keys)):
            raise ValueError("duplicate item key")
        validate_localized_strings(sections[b"ILNM"], item_count)
        validate_localized_strings(sections[b"ILOC"], item_count)
        ability_record = struct.Struct("<HI")
        ability_count = section_counts[b"ABIL"]
        if not ability_count or len(sections[b"ABIL"]) != ability_count * ability_record.size:
            raise ValueError("invalid ability table size")
        ability_keys = []
        for offset in range(0, len(sections[b"ABIL"]), ability_record.size):
            key, _name = ability_record.unpack_from(sections[b"ABIL"], offset)
            ability_keys.append(key)
        if any(not key for key in ability_keys) or any(
                left >= right for left, right in zip(ability_keys, ability_keys[1:])):
            raise ValueError("invalid ability keys")
        validate_localized_strings(sections[b"ALNM"], ability_count)
        validate_localized_strings(sections[b"ALOC"], ability_count)
        if b"IICO" in sections:
            icon_section = sections[b"IICO"]
            icon_record = struct.Struct("<II")
            if section_counts[b"IICO"] != item_count or len(icon_section) < 4:
                raise ValueError("invalid item icon section count")
            icon_count, record_size = struct.unpack_from("<HH", icon_section)
            table_end = 4 + icon_count * icon_record.size
            if icon_count != item_count or record_size != icon_record.size or \
                    table_end > len(icon_section):
                raise ValueError("invalid item icon index")
            previous_end = table_end
            for number in range(icon_count):
                icon_at, icon_size = icon_record.unpack_from(
                    icon_section, 4 + number * icon_record.size)
                if not icon_at and not icon_size:
                    continue
                if icon_at < previous_end or not icon_size or \
                        icon_at + icon_size > len(icon_section):
                    raise ValueError("item icon is outside IICO")
                icon = icon_section[icon_at:icon_at + icon_size]
                if len(icon) < 8 or icon[:4] != b"TIC1":
                    raise ValueError("invalid TIC1 item icon")
                width, height, palette_count, reserved = icon[4:8]
                pixels_at = 8 + palette_count * 2
                if not width or not height or width > 32 or height > 32 or \
                        not palette_count or reserved or \
                        pixels_at + width * height != len(icon) or \
                        any(pixel != 0xFF and pixel >= palette_count
                            for pixel in icon[pixels_at:]):
                    raise ValueError("invalid TIC1 item icon data")
                previous_end = icon_at + icon_size
        mega_record = struct.Struct("<HBBBBBBBBH")
        mega_count = section_counts[b"MEGA"]
        if not mega_count or len(sections[b"MEGA"]) != mega_count * mega_record.size:
            raise ValueError("invalid mega form table size")
        previous_key = (0, -1)
        type_count = section_counts[b"TYPS"]
        for offset in range(0, len(sections[b"MEGA"]), mega_record.size):
            values = mega_record.unpack_from(sections[b"MEGA"], offset)
            species, form, type1, type2 = values[:4]
            stats, ability = values[4:9], values[9]
            if (species, form) <= previous_key or form > 3 or type1 >= type_count or \
                    (type2 != 255 and type2 >= type_count) or any(not value for value in stats) or \
                    (ability and ability not in ability_keys):
                raise ValueError("invalid mega form record")
            previous_key = (species, form)
        gmax_count = section_counts[b"GMAX"]
        if not gmax_count or len(sections[b"GMAX"]) != gmax_count * 2:
            raise ValueError("invalid Gigantamax species table size")
        species = struct.unpack(f"<{gmax_count}H", sections[b"GMAX"])
        if any(not value for value in species) or any(
                left >= right for left, right in zip(species, species[1:])):
            raise ValueError("invalid Gigantamax species table")
    if kind == 2 and ((b"MFSP" in sections) != (b"MFBL" in sections)):
        raise ValueError("incomplete regional Mega sprite sections")
    if kind == 2 and b"MFSP" in sections:
        sprite_record = struct.Struct("<HBBIIII")
        sprite_count = section_counts[b"MFSP"]
        if not sprite_count or len(sections[b"MFSP"]) != sprite_count * sprite_record.size:
            raise ValueError("invalid regional Mega sprite index")
        previous_key = (0, -1)
        for offset in range(0, len(sections[b"MFSP"]), sprite_record.size):
            species, form, display_scale, normal_at, normal_size, shiny_at, shiny_size = \
                sprite_record.unpack_from(sections[b"MFSP"], offset)
            if (species, form) <= previous_key or form > 3 or not normal_size or \
                    normal_at > len(sections[b"MFBL"]) or \
                    normal_size > len(sections[b"MFBL"]) - normal_at or \
                    shiny_at > len(sections[b"MFBL"]) or \
                    shiny_size > len(sections[b"MFBL"]) - shiny_at:
                raise ValueError("invalid regional Mega sprite record")
            normal = sections[b"MFBL"][normal_at:normal_at + normal_size]
            shiny = sections[b"MFBL"][shiny_at:shiny_at + shiny_size]
            if display_scale != pmd_pair_display_scale(normal, shiny):
                raise ValueError("regional Mega sprite display scale does not match Idle bounds")
            previous_key = (species, form)
    elif kind == 4:
        validate_quiz(sections, section_counts)
    if kind in (2, 3) and mechanics_hash == 0:
        raise ValueError(f"{path.name}: mechanics pack has no fingerprint")


def main() -> int:
    index = json.loads((PACKS / "index.json").read_text(encoding="utf-8"))
    if index.get("schema") != 1 or index.get("packAbi") != PACK_ABI:
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
