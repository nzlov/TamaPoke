#!/usr/bin/env python3
"""Refresh breeding fields in the committed Pokemon catalogue.

The runtime needs only egg-group masks, possible offspring species, and the
subset of canonical Egg Moves that exists in TamaPoke's compact move table.
Raw CSV files are cached locally and are not committed.
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from collections import defaultdict
from pathlib import Path


HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "pokemon_data.json"
MOVE_DATA = HERE / "move_data.json"
CACHE = HERE / "pokeapi_cache" / "breeding"
POKEAPI_REVISION = "c40a25c6544b97334a1ae8b1965a378fa3317c28"
CSV_NAMES = (
    "egg_groups",
    "moves",
    "pokemon",
    "pokemon_egg_groups",
    "pokemon_move_methods",
    "pokemon_moves",
    "pokemon_species",
    "version_groups",
)
CSV_URL = (
    "https://raw.githubusercontent.com/PokeAPI/pokeapi/"
    f"{POKEAPI_REVISION}/data/v2/csv/{{}}.csv"
)


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
    result = subprocess.run(
        ["curl", "-fsSL", "--retry", "5", "--retry-delay", "1",
         "--connect-timeout", "30", CSV_URL.format(name)],
        capture_output=True,
    )
    if result.returncode:
        raise RuntimeError(f"failed to download {name}.csv")
    path.write_bytes(result.stdout)
    return path


def rows(name: str, source_dir: Path | None) -> list[dict[str, str]]:
    with csv_path(name, source_dir).open(encoding="utf-8-sig", newline="") as source:
        return list(csv.DictReader(source))


def build(source_dir: Path | None) -> dict:
    pokemon = json.loads(OUTPUT.read_text(encoding="utf-8"))
    moves = json.loads(MOVE_DATA.read_text(encoding="utf-8"))["moves"]
    dex_count = len(pokemon["species"])
    move_slugs = {row["slug"] for row in moves}
    all_moves_by_id = {
        int(row["id"]): row["identifier"] for row in rows("moves", source_dir)
    }
    method_by_name = {
        row["identifier"]: int(row["id"])
        for row in rows("pokemon_move_methods", source_dir)
    }
    egg_method = method_by_name["egg"]

    egg_groups = {
        int(row["id"]): row["identifier"] for row in rows("egg_groups", source_dir)
    }
    if sorted(egg_groups) != list(range(1, 16)):
        raise ValueError("the breeding mask expects PokeAPI's 15 stable egg-group IDs")
    group_masks: dict[int, int] = defaultdict(int)
    for row in rows("pokemon_egg_groups", source_dir):
        species = int(row["species_id"])
        group = int(row["egg_group_id"])
        if species <= dex_count:
            group_masks[species] |= 1 << (group - 1)

    species_rows = {
        int(row["id"]): row
        for row in rows("pokemon_species", source_dir)
        if int(row["id"]) <= dex_count
    }
    if set(species_rows) != set(range(1, dex_count + 1)):
        raise ValueError("pinned PokeAPI species table does not cover the authored dex")

    default_pokemon = {
        int(row["id"]): int(row["species_id"])
        for row in rows("pokemon", source_dir)
        if row["is_default"] == "1" and int(row["species_id"]) <= dex_count
    }
    egg_moves_by_version: dict[tuple[int, int], set[str]] = defaultdict(set)
    for row in rows("pokemon_moves", source_dir):
        if int(row["pokemon_move_method_id"]) != egg_method:
            continue
        species = default_pokemon.get(int(row["pokemon_id"]))
        move = all_moves_by_id.get(int(row["move_id"]))
        if species is not None and move is not None:
            egg_moves_by_version[(species, int(row["version_group_id"]))].add(move)

    def root_species(species: int) -> int:
        seen = set()
        while species_rows[species]["evolves_from_species_id"]:
            if species in seen:
                raise ValueError(f"cyclic evolution ancestry at species {species}")
            seen.add(species)
            species = int(species_rows[species]["evolves_from_species_id"])
        return species

    special_offspring = {
        29: [29, 32], 32: [29, 32],
        313: [313, 314], 314: [313, 314],
        489: [489], 490: [489],
    }
    version_names = {
        int(row["id"]): row["identifier"] for row in rows("version_groups", source_dir)
    }
    catalogue = []
    used_versions = set()
    for species in range(1, dex_count + 1):
        if not group_masks[species]:
            raise ValueError(f"species {species}: missing egg group")
        root = root_species(species)
        offspring = special_offspring.get(root, [root])
        versions = [
            version for candidate, version in egg_moves_by_version if candidate == species
        ]
        chosen_version = max(versions, default=0)
        if chosen_version:
            used_versions.add(chosen_version)
        catalogue.append({
            "dex": species,
            "groups": group_masks[species],
            "offspring": offspring,
            "eggMoves": sorted(
                move for move in egg_moves_by_version.get((species, chosen_version), ())
                if move in move_slugs
            ),
        })

    return {
        "schema": 1,
        "source": {
            "repository": "https://github.com/PokeAPI/pokeapi",
            "revision": POKEAPI_REVISION,
            "path": "data/v2/csv",
            "eggMoveRule": "latest version group with Egg Move rows for each species",
            "versionGroups": [version_names[value] for value in sorted(used_versions)],
        },
        "eggGroups": [egg_groups[index] for index in range(1, 16)],
        "species": catalogue,
    }


def apply_catalogue(document: dict, catalogue: dict) -> dict:
    moves = json.loads(MOVE_DATA.read_text(encoding="utf-8"))["moves"]
    move_ids = {row["slug"]: int(row["id"]) for row in moves}
    rows = catalogue["species"]
    species = document["species"]
    if len(rows) != len(species):
        raise ValueError("breeding catalogue does not cover pokemon_data.json")
    document["breedingSource"] = catalogue["source"]
    document["eggGroups"] = catalogue["eggGroups"]
    for expected, (target, source) in enumerate(zip(species, rows), 1):
        if target["id"] != expected or source["dex"] != expected:
            raise ValueError(f"species {expected}: breeding catalogue is out of order")
        groups = int(source["groups"])
        target["breeding"] = {
            "eggGroupIds": [
                group for group in range(1, len(catalogue["eggGroups"]) + 1)
                if groups & (1 << (group - 1))
            ],
            "offspringSpecies": [int(value) for value in source["offspring"]],
            "eggMoveIds": sorted(move_ids[slug] for slug in source["eggMoves"]),
        }
    return document


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv-dir", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    document = json.loads(OUTPUT.read_text(encoding="utf-8"))
    encoded = json.dumps(
        apply_catalogue(document, build(args.csv_dir)), ensure_ascii=False, indent=2,
    ) + "\n"
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != encoded:
            raise SystemExit("pokemon_data.json breeding fields are stale; run tools/fetch_breeding_data.py")
        print("PASS pokemon_data.json breeding fields match the pinned data")
        return 0
    OUTPUT.write_text(encoded, encoding="utf-8")
    print(f"updated {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
