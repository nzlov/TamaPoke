#!/usr/bin/env python3
"""Build the committed move-tag catalogue from a pinned PokeAPI revision."""

from __future__ import annotations

import argparse
import csv
import json
import ssl
import urllib.request
from collections import defaultdict
from pathlib import Path


HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "move_tag_data.json"
CACHE = HERE / "pokeapi_cache" / "move_tags"
POKEAPI_REVISION = "c40a25c6544b97334a1ae8b1965a378fa3317c28"
CSV_NAMES = ("moves", "move_flags", "move_flag_map")
CSV_URL = (
    "https://raw.githubusercontent.com/PokeAPI/pokeapi/"
    f"{POKEAPI_REVISION}/data/v2/csv/{{}}.csv"
)

TAG_BITS = {
    "contact": 1 << 0,
    "sound": 1 << 1,
    "punch": 1 << 2,
    "bite": 1 << 3,
    "pulse": 1 << 4,
    "ballistic": 1 << 5,
    "powder": 1 << 6,
    "dance": 1 << 7,
    "slicing": 1 << 8,
    "wind": 1 << 9,
    "reflectable": 1 << 10,
}
POKEAPI_FLAGS = {
    "contact", "sound", "punch", "bite", "pulse", "ballistics",
    "powder", "dance", "reflectable",
}
# PokeAPI's legacy flag table predates the Scarlet/Violet slicing and wind
# groupings. These are the members of those official groups present in the
# compact TamaPoke move catalogue.
SUPPLEMENTAL = {
    "slicing": {"razor-leaf", "x-scissor"},
    "wind": {"blizzard"},
}


try:
    import certifi

    SSL_CONTEXT = ssl.create_default_context(cafile=certifi.where())
except ImportError:
    SSL_CONTEXT = None


def csv_path(name: str, source_dir: Path | None) -> Path:
    if source_dir is not None:
        path = source_dir / f"{name}.csv"
        if not path.exists():
            raise FileNotFoundError(path)
        return path
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / f"{name}.csv"
    if path.exists():
        return path
    request = urllib.request.Request(
        CSV_URL.format(name), headers={"User-Agent": "TamaPoke data generator"}
    )
    with urllib.request.urlopen(request, timeout=60, context=SSL_CONTEXT) as response:
        path.write_bytes(response.read())
    return path


def rows(name: str, source_dir: Path | None) -> list[dict[str, str]]:
    with csv_path(name, source_dir).open(encoding="utf-8-sig", newline="") as source:
        return list(csv.DictReader(source))


def build(source_dir: Path | None) -> dict:
    from dex_moves import MOVES

    move_ids = {row["identifier"]: int(row["id"]) for row in rows("moves", source_dir)}
    flag_names = {
        int(row["id"]): row["identifier"] for row in rows("move_flags", source_dir)
    }
    upstream: dict[int, set[str]] = defaultdict(set)
    for row in rows("move_flag_map", source_dir):
        upstream[int(row["move_id"])].add(flag_names[int(row["move_flag_id"])])

    result = []
    for name, slug, *_rest in MOVES:
        key = slug or name.lower()
        move_id = move_ids.get(key)
        if move_id is None:
            raise ValueError(f"move {key}: absent from pinned PokeAPI move table")
        tags = {
            "ballistic" if flag == "ballistics" else flag
            for flag in upstream[move_id]
            if flag in POKEAPI_FLAGS
        }
        for tag, members in SUPPLEMENTAL.items():
            if key in members:
                tags.add(tag)
        result.append({"slug": key, "tags": sorted(tags, key=TAG_BITS.get)})

    return {
        "schema": 1,
        "source": {
            "repository": "https://github.com/PokeAPI/pokeapi",
            "revision": POKEAPI_REVISION,
            "path": "data/v2/csv",
            "supplemental": "Scarlet/Violet slicing and wind move groupings",
        },
        "tagBits": TAG_BITS,
        "moves": result,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv-dir", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    encoded = json.dumps(build(args.csv_dir), ensure_ascii=False, indent=2) + "\n"
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != encoded:
            raise SystemExit("move_tag_data.json is stale; run tools/fetch_move_tag_data.py")
        print("PASS move_tag_data.json matches the pinned move flags")
        return 0
    OUTPUT.write_text(encoded, encoding="utf-8")
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
