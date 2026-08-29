#!/usr/bin/env python3
"""Import the PMD source art referenced by the local Pokemon catalogue."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


HERE = Path(__file__).resolve().parent
CATALOGUE = HERE / "pokemon_data.json"
OUTPUT = HERE / "pokemon_art" / "pmd"
EXPECTED_REVISION = "1408504143965ec1ea9c5adc78e39db5a5f43360"
ACTIONS = {
    "Idle", "Walk", "Sleep", "Eat", "Hurt", "Attack", "Pose", "Hop",
    "Nod", "DeepBreath", "Sit",
}


def animation_sheets(folder: Path) -> list[Path]:
    metadata = folder / "AnimData.xml"
    if not metadata.is_file():
        return []
    root = ET.parse(metadata).getroot().find("Anims")
    if root is None:
        return []
    records = {}
    for animation in root:
        name = animation.findtext("Name")
        if not name:
            continue
        copy_of = animation.findtext("CopyOf")
        records[name] = copy_of or name

    def source_name(name: str) -> str | None:
        seen = set()
        while name in records and records[name] != name:
            if name in seen:
                return None
            seen.add(name)
            name = records[name]
        return name if name in records else None

    if not source_name("Idle"):
        return []
    names = {source_name(name) for name in ACTIONS}
    paths = [metadata]
    for name in sorted(name for name in names if name):
        sheet = folder / f"{name}-Anim.png"
        if not sheet.is_file():
            raise FileNotFoundError(sheet)
        paths.append(sheet)
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path,
                        help="local checkout of PMDCollab/SpriteCollab")
    args = parser.parse_args()
    source = args.source.resolve()
    revision = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if revision != EXPECTED_REVISION:
        raise ValueError(f"expected {EXPECTED_REVISION}, got {revision}")

    document = json.loads(CATALOGUE.read_text(encoding="utf-8"))
    sprite_source = source / "sprite"
    sprite_output = OUTPUT / "sprite"
    copied_files = 0
    copied_bytes = 0

    def import_ref(ref: str) -> bool:
        nonlocal copied_files, copied_bytes
        files = animation_sheets(sprite_source / ref)
        if not files:
            return False
        destination = sprite_output / ref
        destination.mkdir(parents=True, exist_ok=True)
        for path in files:
            target = destination / path.name
            shutil.copy2(path, target)
            copied_files += 1
            copied_bytes += target.stat().st_size
        return True

    for species in document["species"]:
        number = int(species["id"])
        base = f"{number:04d}"
        refs = {
            "normal": base,
            "shiny": f"{base}/0000/0001",
            "female": f"{base}/0000/0000/0002",
            "femaleShiny": f"{base}/0000/0001/0002",
        }
        art = {variant: ref for variant, ref in refs.items() if import_ref(ref)}
        species["art"] = art
        for form in species["megaForms"]:
            current_art = form.get("art", {})
            legacy_path = form.pop("spritePath", None)
            legacy_shiny = form.pop("shiny", False)
            normal = (f"{base}/{legacy_path}" if legacy_path
                      else current_art.get("normal"))
            if not normal:
                continue
            form_art = {}
            if import_ref(normal):
                form_art["normal"] = normal
            shiny = (f"{normal}/0001" if legacy_shiny
                     else current_art.get("shiny"))
            if shiny and import_ref(shiny):
                form_art["shiny"] = shiny
            if form_art:
                form["art"] = form_art

    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name in ("LICENSE.md", "credit_names.txt", "tracker.json"):
        shutil.copy2(source / name, OUTPUT / name)
    document["artSource"] = {
        "provider": "PMDCollab/SpriteCollab",
        "revision": EXPECTED_REVISION,
        "root": "pokemon_art/pmd/sprite",
        "license": "pokemon_art/pmd/LICENSE.md",
        "credits": "pokemon_art/pmd/tracker.json",
    }
    CATALOGUE.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"imported {copied_files} source files ({copied_bytes / 1048576:.1f} MiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
