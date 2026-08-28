#!/usr/bin/env python3
"""Require every implemented battle ability to appear in a host behavior test."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ABILITY_RE = re.compile(r"\bABILITY_[A-Z0-9_]+\b")


def implemented_abilities(header: Path) -> set[str]:
    text = header.read_text(encoding="utf-8")
    match = re.search(
        r"enum\s+BattleAbility\s*:\s*AbilityKey\s*\{(?P<body>.*?)\n\};",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"could not find BattleAbility enum in {header}")
    return set(ABILITY_RE.findall(match.group("body")))


def tested_abilities(tests_dir: Path) -> set[str]:
    covered: set[str] = set()
    for source in tests_dir.glob("*_test.cpp"):
        text = source.read_text(encoding="utf-8")
        text = re.sub(r"//.*?$|/\*.*?\*/", "", text, flags=re.MULTILINE | re.DOTALL)
        covered.update(ABILITY_RE.findall(text))
    covered.discard("ABILITY_NONE")
    covered = {name for name in covered if not name.startswith("ABILITY_SLOT_")}
    return covered


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent.parent
    parser.add_argument("--header", type=Path, default=root / "abilities.h")
    parser.add_argument(
        "--tests-dir", type=Path, default=root / "tools" / "emu" / "tests"
    )
    args = parser.parse_args()

    implemented = implemented_abilities(args.header)
    tested = tested_abilities(args.tests_dir)
    missing = sorted(implemented - tested)
    unknown = sorted(tested - implemented)
    if missing:
        print("implemented abilities missing from behavior tests:")
        print("  " + "\n  ".join(missing))
    if unknown:
        print("test references not present in BattleAbility:")
        print("  " + "\n  ".join(unknown))
    if missing or unknown:
        return 1
    print(f"ability behavior coverage: {len(implemented)}/{len(implemented)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
