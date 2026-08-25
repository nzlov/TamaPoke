#!/usr/bin/env python3
"""Round-trip and corruption checks for the indexed question-pack format."""

from __future__ import annotations

from quiz_pack import QIDX, QLOC, build_quiz_pack, read_quiz_pack


DOCUMENT = {
    "schema": 1,
    "id": "quiz-test",
    "label": "Indexed test bank",
    "revision": 7,
    "questions": [
        {"id": "zh-2", "locale": "zh-CN", "stem": "第二题", "options": ["甲", "乙"], "answer": 1},
        {"id": "en-1", "locale": "en-US", "stem": "First?", "options": ["Yes", "No"], "answer": 0},
        {"id": "zh-1", "locale": "zh-CN", "stem": "第一题", "options": ["一", "二", "三"], "answer": 2},
    ],
}


def main() -> int:
    raw, metadata = build_quiz_pack(DOCUMENT)
    decoded = read_quiz_pack(raw)
    assert metadata == {
        "id": "quiz-test", "label": "Indexed test bank", "revision": 7,
        "locales": ["en-US", "zh-CN"], "questions": 3,
    }
    assert [(item["locale"], item["id"]) for item in decoded["questions"]] == [
        ("en-US", "en-1"), ("zh-CN", "zh-1"), ("zh-CN", "zh-2")
    ]
    assert QLOC.size == 24 and QIDX.size == 12

    corrupt = bytearray(raw)
    corrupt[-1] ^= 1
    try:
        read_quiz_pack(bytes(corrupt))
    except ValueError as error:
        assert "checksum" in str(error)
    else:
        raise AssertionError("corrupt question pack was accepted")

    duplicate = dict(DOCUMENT)
    duplicate["questions"] = list(DOCUMENT["questions"]) + [dict(DOCUMENT["questions"][0])]
    try:
        build_quiz_pack(duplicate)
    except ValueError as error:
        assert "duplicate" in str(error)
    else:
        raise AssertionError("duplicate question identity was accepted")

    nul = dict(DOCUMENT)
    nul["questions"] = [dict(DOCUMENT["questions"][0], stem="bad\0stem")]
    try:
        build_quiz_pack(nul)
    except ValueError as error:
        assert "NUL" in str(error)
    else:
        raise AssertionError("NUL question text was accepted")
    print("PASS indexed question-pack round trip")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
