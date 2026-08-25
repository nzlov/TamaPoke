#!/usr/bin/env python3
"""Create a small indexed question pack for firmware reader tests."""

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from quiz_pack import build_quiz_pack  # noqa: E402


document = {
    "schema": 1,
    "id": "quiz-reader",
    "label": "Reader fixture",
    "revision": 7,
    "questions": [
        {"id": "zh-2", "locale": "zh-CN", "stem": "第二题", "options": ["甲", "乙"], "answer": 1},
        {"id": "en-1", "locale": "en", "stem": "Pick one", "options": ["One", "Two"], "answer": 0},
        {"id": "zh-1", "locale": "zh-CN", "stem": "第一题", "options": ["A", "B", "C"], "answer": 2},
    ],
}
raw, _metadata = build_quiz_pack(document)
Path(sys.argv[1]).write_bytes(raw)
