#!/usr/bin/env python3
"""Build the committed ability catalogue from a pinned PokeAPI revision.

The generated JSON is the single authoring source consumed by the pack builder.
Raw CSV files are cached locally and are not committed.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import ssl
import urllib.request
from collections import defaultdict
from pathlib import Path


HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "ability_data.json"
CACHE = HERE / "pokeapi_cache" / "abilities"
POKEAPI_REVISION = "c40a25c6544b97334a1ae8b1965a378fa3317c28"
CSV_NAMES = (
    "abilities",
    "ability_names",
    "ability_prose",
    "ability_flavor_text",
    "pokemon",
    "pokemon_abilities",
)
CSV_URL = (
    "https://raw.githubusercontent.com/PokeAPI/pokeapi/"
    f"{POKEAPI_REVISION}/data/v2/csv/{{}}.csv"
)
LANG_EN = 9
LANG_ZH = 12


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


def clean_text(value: str) -> str:
    value = re.sub(r"\[([^]]+)]\{[^}]+}", r"\1", value)
    value = "".join(
        chr(ord(char) - 0xFEE0)
        if ("０" <= char <= "９" or "Ａ" <= char <= "Ｚ" or "ａ" <= char <= "ｚ")
        else char
        for char in value
    )
    value = re.sub(r"\s+", " ", value).strip()
    return value


def build(source_dir: Path | None) -> dict:
    from dex_data import DEX

    dex_count = max(row[0] for row in DEX)
    translation_data = json.loads(
        (HERE / "ability_description_zh.json").read_text(encoding="utf-8")
    )
    if translation_data.get("schema") != 1:
        raise ValueError("unsupported Chinese ability description override schema")
    translated = {
        int(key): clean_text(value)
        for key, value in translation_data.get("descriptions", {}).items()
    }
    translated_names = {
        int(key): clean_text(value)
        for key, value in translation_data.get("names", {}).items()
    }
    pokemon = {int(row["id"]): row for row in rows("pokemon", source_dir)}
    pokemon_abilities: dict[int, list[dict[str, str]]] = defaultdict(list)
    for row in rows("pokemon_abilities", source_dir):
        pokemon_abilities[int(row["pokemon_id"])].append(row)
    species_slots: dict[int, dict[int, int]] = defaultdict(dict)
    for pokemon_id, ability_rows in pokemon_abilities.items():
        pokemon_row = pokemon.get(pokemon_id)
        if not pokemon_row or pokemon_row["is_default"] != "1":
            continue
        species = int(pokemon_row["species_id"])
        if species > dex_count:
            continue
        for row in ability_rows:
            slot = 3 if row["is_hidden"] == "1" else int(row["slot"])
            if slot not in (1, 2, 3) or slot in species_slots[species]:
                raise ValueError(f"species {species}: duplicate or invalid ability slot {slot}")
            species_slots[species][slot] = int(row["ability_id"])

    missing_species = set(range(1, dex_count + 1)) - set(species_slots)
    if missing_species:
        raise ValueError(f"species without abilities: {sorted(missing_species)}")

    identifiers = {
        int(row["id"]): row["identifier"]
        for row in rows("abilities", source_dir)
        if row["is_main_series"] == "1"
    }
    names: dict[int, dict[int, str]] = defaultdict(dict)
    for row in rows("ability_names", source_dir):
        language = int(row["local_language_id"])
        if language in (LANG_EN, LANG_ZH):
            names[int(row["ability_id"])][language] = clean_text(row["name"])

    prose: dict[int, str] = {}
    for row in rows("ability_prose", source_dir):
        if int(row["local_language_id"]) == LANG_EN:
            prose[int(row["ability_id"])] = clean_text(row["short_effect"])

    flavor: dict[int, tuple[int, str]] = {}
    for row in rows("ability_flavor_text", source_dir):
        if int(row["language_id"]) != LANG_ZH:
            continue
        ability = int(row["ability_id"])
        version = int(row["version_group_id"])
        if ability not in flavor or version > flavor[ability][0]:
            flavor[ability] = (version, clean_text(row["flavor_text"]))

    defaults = {
        int(row["species_id"]): row
        for row in pokemon.values()
        if row["is_default"] == "1" and int(row["species_id"]) <= dex_count
    }
    mega_rows = []
    for form in json.loads((HERE / "mega_data.json").read_text(encoding="utf-8")):
        species = int(form["species"])
        form_name = form.get("form", "standard")
        identifier = defaults[species]["identifier"] + "-mega"
        if form_name != "standard":
            identifier += f"-{form_name}"
        candidates = [
            row for row in pokemon.values()
            if int(row["species_id"]) == species and row["identifier"] == identifier
        ]
        if not candidates and form_name == "standard":
            candidates = [
                row for row in pokemon.values()
                if int(row["species_id"]) == species and
                row["identifier"] == defaults[species]["identifier"].split("-")[0] + "-mega"
            ]
        if len(candidates) != 1:
            raise ValueError(f"Mega {species}/{form_name}: expected PokeAPI form {identifier}")
        abilities = pokemon_abilities[int(candidates[0]["id"])]
        if len(abilities) > 1 or (abilities and abilities[0]["is_hidden"] != "0"):
            raise ValueError(f"Mega {species}/{form_name}: expected one normal ability")
        mega_rows.append({
            "dex": species,
            "form": form_name,
            "ability": int(abilities[0]["ability_id"]) if abilities else 0,
            "status": "canonical" if abilities else "not-published-in-source",
        })

    used = sorted(
        {ability for slots in species_slots.values() for ability in slots.values()} |
        {row["ability"] for row in mega_rows if row["ability"]}
    )
    catalogue = []
    for ability in used:
        english_name = names.get(ability, {}).get(LANG_EN)
        chinese_name = names.get(ability, {}).get(LANG_ZH) or translated_names.get(ability)
        english_description = prose.get(ability)
        if not identifiers.get(ability) or not english_name or not chinese_name or not english_description:
            raise ValueError(f"ability {ability}: incomplete canonical catalogue")
        chinese_description = flavor.get(ability, (0, translated.get(ability, "")))[1]
        if not chinese_description:
            raise ValueError(f"ability {ability}: missing Chinese description")
        catalogue.append({
            "id": ability,
            "slug": identifiers[ability],
            "names": {"en-US": english_name, "zh-CN": chinese_name},
            "descriptions": {
                "en-US": english_description,
                "zh-CN": chinese_description,
            },
        })

    return {
        "schema": 1,
        "source": {
            "repository": "https://github.com/PokeAPI/pokeapi",
            "revision": POKEAPI_REVISION,
            "path": "data/v2/csv",
        },
        "abilities": catalogue,
        "mega": mega_rows,
        "species": [
            {
                "dex": species,
                "slots": [
                    species_slots[species].get(1, 0),
                    species_slots[species].get(2, 0),
                    species_slots[species].get(3, 0),
                ],
            }
            for species in range(1, dex_count + 1)
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--csv-dir", type=Path,
        help="directory containing pinned PokeAPI CSV files (uses cache/network if omitted)",
    )
    parser.add_argument("--check", action="store_true", help="verify committed output")
    args = parser.parse_args()
    encoded = json.dumps(build(args.csv_dir), ensure_ascii=False, indent=2) + "\n"
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != encoded:
            raise SystemExit("ability_data.json is stale; run tools/fetch_ability_data.py")
        print("PASS ability_data.json matches the pinned PokeAPI data")
        return 0
    OUTPUT.write_text(encoded, encoding="utf-8")
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
