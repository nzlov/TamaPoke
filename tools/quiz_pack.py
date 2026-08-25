#!/usr/bin/env python3
"""Build or inspect an indexed TamaPoke choice-question pack."""

from __future__ import annotations

import argparse
import binascii
import json
import re
import struct
from pathlib import Path

from pack_format import COMMON, PACK_ABI, SECTION, pack


KIND_QUIZ = 4
QLOC = struct.Struct("<16sII")
QIDX = struct.Struct("<III")
QREC = struct.Struct("<BBHHHHHH")
PACK_ID = re.compile(r"^[a-z0-9][a-z0-9._-]{0,18}$")
MAX_ID_BYTES = 40
MAX_STEM_BYTES = 768
MAX_OPTION_BYTES = 192


def _encoded(value: object, field: str, maximum: int) -> bytes:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{field} must be a non-empty string")
    if "\0" in value:
        raise ValueError(f"{field} must not contain NUL")
    raw = value.encode("utf-8")
    if len(raw) > maximum:
        raise ValueError(f"{field} exceeds {maximum} UTF-8 bytes")
    return raw


def normalize_document(document: object) -> dict:
    if not isinstance(document, dict) or document.get("schema") != 1:
        raise ValueError("question bank schema must be 1")
    pack_id = document.get("id")
    if not isinstance(pack_id, str) or not PACK_ID.fullmatch(pack_id):
        raise ValueError("pack id must match [a-z0-9][a-z0-9._-]{0,18}")
    revision = document.get("revision", 1)
    if not isinstance(revision, int) or not 0 < revision <= 0xFFFFFFFF:
        raise ValueError("revision must be a positive 32-bit integer")
    label = document.get("label", pack_id)
    if not isinstance(label, str) or not label.strip():
        raise ValueError("label must be a non-empty string")
    source = document.get("questions")
    if not isinstance(source, list) or not source:
        raise ValueError("questions must be a non-empty array")

    seen = set()
    questions = []
    for number, item in enumerate(source, 1):
        if not isinstance(item, dict):
            raise ValueError(f"question {number} must be an object")
        question_id = _encoded(item.get("id"), f"question {number} id", MAX_ID_BYTES)
        locale_value = item.get("locale")
        if not isinstance(locale_value, str) or not re.fullmatch(r"[A-Za-z0-9-]{2,15}", locale_value):
            raise ValueError(f"question {number} has an invalid locale")
        identity = (locale_value, question_id)
        if identity in seen:
            raise ValueError(f"duplicate question id {item['id']!r} in {locale_value}")
        seen.add(identity)
        stem = _encoded(item.get("stem"), f"question {number} stem", MAX_STEM_BYTES)
        options_value = item.get("options")
        if not isinstance(options_value, list) or not 2 <= len(options_value) <= 4:
            raise ValueError(f"question {number} must have 2..4 options")
        options = [
            _encoded(value, f"question {number} option {index + 1}", MAX_OPTION_BYTES)
            for index, value in enumerate(options_value)
        ]
        answer = item.get("answer")
        if not isinstance(answer, int) or not 0 <= answer < len(options):
            raise ValueError(f"question {number} answer must index one option")
        questions.append({
            "id": question_id,
            "id_text": item["id"],
            "locale": locale_value,
            "stem": stem,
            "stem_text": item["stem"],
            "options": options,
            "option_texts": list(options_value),
            "answer": answer,
        })
    questions.sort(key=lambda item: (item["locale"], item["id_text"]))
    return {"schema": 1, "id": pack_id, "label": label.strip(),
            "revision": revision, "questions": questions}


def build_quiz_pack(document: object) -> tuple[bytes, dict]:
    normalized = normalize_document(document)
    data = bytearray()
    index = bytearray()
    locale_rows = []
    first = 0
    for locale in sorted({item["locale"] for item in normalized["questions"]}):
        rows = [item for item in normalized["questions"] if item["locale"] == locale]
        locale_rows.append((locale, first, len(rows)))
        first += len(rows)
        for item in rows:
            option_lengths = [len(value) for value in item["options"]] + [0] * (4 - len(item["options"]))
            record = bytearray(QREC.pack(
                len(item["options"]), item["answer"], len(item["id"]), len(item["stem"]),
                *option_lengths,
            ))
            record.extend(item["id"])
            record.extend(item["stem"])
            for option in item["options"]:
                record.extend(option)
            identity = f"{locale}\0{item['id_text']}".encode("utf-8")
            index.extend(QIDX.pack(
                binascii.crc32(identity) & 0xFFFFFFFF, len(data), len(record)))
            data.extend(record)
    locales = bytearray()
    for locale, start, count in locale_rows:
        locales.extend(QLOC.pack(locale.encode("ascii").ljust(16, b"\0"), start, count))
    blob = pack(KIND_QUIZ, normalized["id"], 0, [
        ("QLOC", bytes(locales), len(locale_rows)),
        ("QIDX", bytes(index), len(normalized["questions"])),
        ("QDAT", bytes(data), len(normalized["questions"])),
    ], revision=normalized["revision"])
    metadata = {
        "id": normalized["id"],
        "label": normalized["label"],
        "revision": normalized["revision"],
        "locales": [row[0] for row in locale_rows],
        "questions": len(normalized["questions"]),
    }
    return blob, metadata


def read_quiz_pack(raw: bytes) -> dict:
    if len(raw) < COMMON.size:
        raise ValueError("truncated pack")
    (magic, abi, kind, _flags, size, payload_crc, revision, _mechanics,
     header_size, section_count, ident) = COMMON.unpack_from(raw)
    if magic != b"TPPK" or abi != PACK_ABI or kind != KIND_QUIZ or size != len(raw):
        raise ValueError("incompatible question pack")
    if header_size != COMMON.size + section_count * SECTION.size or header_size > len(raw):
        raise ValueError("invalid question-pack directory")
    if payload_crc != binascii.crc32(raw[header_size:]) & 0xFFFFFFFF:
        raise ValueError("question-pack payload checksum mismatch")
    sections = {}
    counts = {}
    for offset in range(COMMON.size, header_size, SECTION.size):
        tag, start, length, count = SECTION.unpack_from(raw, offset)
        if tag in sections or start < header_size or start + length > len(raw):
            raise ValueError("invalid question-pack section")
        sections[tag] = raw[start:start + length]
        counts[tag] = count
    if set(sections) != {b"QLOC", b"QIDX", b"QDAT"}:
        raise ValueError("unexpected question-pack sections")
    if len(sections[b"QLOC"]) != counts[b"QLOC"] * QLOC.size or \
            len(sections[b"QIDX"]) != counts[b"QIDX"] * QIDX.size or \
            counts[b"QDAT"] != counts[b"QIDX"]:
        raise ValueError("invalid question-pack indexes")
    locales = []
    covered = 0
    for offset in range(0, len(sections[b"QLOC"]), QLOC.size):
        code, first, count = QLOC.unpack_from(sections[b"QLOC"], offset)
        locale = code.split(b"\0", 1)[0].decode("ascii")
        if not locale or first != covered or first + count > counts[b"QIDX"]:
            raise ValueError("invalid locale span")
        locales.extend([locale] * count)
        covered += count
    if covered != counts[b"QIDX"]:
        raise ValueError("question indexes are not fully covered")
    questions = []
    for number in range(counts[b"QIDX"]):
        _hash, start, length = QIDX.unpack_from(sections[b"QIDX"], number * QIDX.size)
        if length < QREC.size or start + length > len(sections[b"QDAT"]):
            raise ValueError("question record outside data section")
        record = sections[b"QDAT"][start:start + length]
        option_count, answer, id_size, stem_size, *option_sizes = QREC.unpack_from(record)
        if not 2 <= option_count <= 4 or answer >= option_count or any(option_sizes[option_count:]) or \
                not id_size or id_size > MAX_ID_BYTES or \
                not stem_size or stem_size > MAX_STEM_BYTES or \
                any(not size or size > MAX_OPTION_BYTES for size in option_sizes[:option_count]):
            raise ValueError("invalid question record header")
        total = QREC.size + id_size + stem_size + sum(option_sizes[:option_count])
        if total != len(record):
            raise ValueError("invalid question record size")
        cursor = QREC.size
        question_id = record[cursor:cursor + id_size].decode("utf-8"); cursor += id_size
        stem = record[cursor:cursor + stem_size].decode("utf-8"); cursor += stem_size
        if "\0" in question_id or "\0" in stem:
            raise ValueError("invalid NUL in question record")
        options = []
        for option_size in option_sizes[:option_count]:
            options.append(record[cursor:cursor + option_size].decode("utf-8"))
            cursor += option_size
        if any("\0" in option for option in options):
            raise ValueError("invalid NUL in question option")
        questions.append({"id": question_id, "locale": locales[number], "stem": stem,
                          "options": options, "answer": answer})
    return {
        "schema": 1,
        "id": ident.split(b"\0", 1)[0].decode("ascii"),
        "revision": revision,
        "label": ident.split(b"\0", 1)[0].decode("ascii"),
        "questions": questions,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="authoring JSON")
    parser.add_argument("output", type=Path, nargs="?", help="output .tquiz")
    parser.add_argument("--inspect", action="store_true", help="read a .tquiz as JSON")
    args = parser.parse_args()
    if args.inspect:
        print(json.dumps(read_quiz_pack(args.input.read_bytes()), ensure_ascii=False, indent=2))
        return 0
    document = json.loads(args.input.read_text(encoding="utf-8"))
    blob, metadata = build_quiz_pack(document)
    output = args.output or args.input.with_suffix(".tquiz")
    output.write_bytes(blob)
    print(f"{output}: {metadata['questions']} questions, {len(blob)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
