#!/usr/bin/env python3
"""Stage the static Web installer for GitHub Pages."""

import argparse
import json
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"

STABLE_ALIAS = """<!doctype html>
<meta charset="utf-8">
<meta http-equiv="refresh" content="0; url=../">
<title>TamaPoke stable release</title>
<p><a href="../">Open the TamaPoke stable release installer</a></p>
"""


def prepare_site(
    source: Path, output: Path, commit: str, date: str, channel: str = "stable"
) -> None:
    output.mkdir(parents=True, exist_ok=True)

    for asset in source.iterdir():
        if asset.is_file() and asset.name != "README.md":
            shutil.copy2(asset, output / asset.name)

    for directory in ("firmware", "packs"):
        shutil.copytree(source / directory, output / directory, dirs_exist_ok=True)

    metadata = {"commit": commit, "date": date, "channel": channel}
    (output / "build-info.json").write_text(
        json.dumps(metadata, separators=(",", ":")) + "\n", encoding="utf-8"
    )
    if channel == "stable":
        # GLUE: preserve the former documented /web/ URL while Pages serves the
        # installer at its artifact root. Remove after external links migrate.
        alias = output / "web"
        alias.mkdir(exist_ok=True)
        (alias / "index.html").write_text(STABLE_ALIAS, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source", type=Path, default=WEB)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--date", required=True)
    parser.add_argument("--channel", choices=("stable", "latest"), default="stable")
    args = parser.parse_args()
    prepare_site(args.source, args.output, args.commit, args.date, args.channel)


if __name__ == "__main__":
    main()
