#!/usr/bin/env python3
"""Stage the static Web installer for GitHub Pages."""

import argparse
import json
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"


def prepare_site(source: Path, output: Path, commit: str, date: str) -> None:
    output.mkdir(parents=True, exist_ok=True)

    for asset in source.iterdir():
        if asset.is_file() and asset.name != "README.md":
            shutil.copy2(asset, output / asset.name)

    for directory in ("firmware", "packs"):
        shutil.copytree(source / directory, output / directory, dirs_exist_ok=True)

    metadata = {"commit": commit, "date": date}
    (output / "build-info.json").write_text(
        json.dumps(metadata, separators=(",", ":")) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--date", required=True)
    args = parser.parse_args()
    prepare_site(WEB, args.output, args.commit, args.date)


if __name__ == "__main__":
    main()
