#!/usr/bin/env python3
"""Build the OpenType subset embedded in the Chinese UI data pack."""

from __future__ import annotations

from argparse import ArgumentParser
from pathlib import Path
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gen_data_packs import required_ui_codepoints  # noqa: E402


def main() -> int:
    parser = ArgumentParser()
    parser.add_argument("source", type=Path, help="source OTF/TTC/TTF")
    parser.add_argument("output", type=Path, help="subset OTF written here")
    parser.add_argument("--font-number", type=int,
                        help="optional face index for a TTC; Noto Sans CJK SC is 2")
    args = parser.parse_args()

    codepoints = sorted(required_ui_codepoints())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    options = [
        str(args.source),
        f"--output-file={args.output}",
        "--unicodes=" + ",".join(f"U+{codepoint:04X}" for codepoint in codepoints),
        "--layout-features=*",
        "--hinting",
        "--notdef-glyph",
        "--notdef-outline",
        "--recommended-glyphs",
        "--name-IDs=*",
        "--name-languages=*",
        "--no-ignore-missing-unicodes",
    ]
    if args.font_number is not None:
        options.insert(1, f"--font-number={args.font_number}")
    subset.main(options)

    face = TTFont(args.output)
    available = set(face.getBestCmap())
    missing = set(codepoints) - available
    if missing:
        sample = " ".join(f"U+{codepoint:04X}" for codepoint in sorted(missing)[:8])
        raise SystemExit(f"subset is missing {len(missing)} characters: {sample}")
    print(f"PASS {len(codepoints)} characters -> {args.output} ({args.output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
