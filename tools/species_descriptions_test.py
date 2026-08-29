#!/usr/bin/env python3
"""Selection and catalogue checks for official species flavor text."""

from __future__ import annotations

import json
from pathlib import Path

from fetch_species_descriptions import LOCALE_LANGUAGES, newest_entry, select_locales
from gen_data_packs import SPECIES, species_descriptions


HERE = Path(__file__).resolve().parent


def fixture_entry(language: str, version: str, number: int, text: str) -> dict:
    return {
        "flavor_text": text,
        "language": {"name": language},
        "version": {
            "name": version,
            "url": f"https://pokeapi.co/api/v2/version/{number}/",
        },
    }


def main() -> int:
    fixture = {
        "id": 25,
        "flavor_text_entries": [
            fixture_entry("en", "new", 20, "Newest\nEnglish\ftext"),
            fixture_entry("en", "old", 2, "Old English text"),
            fixture_entry("zh-hans", "middle", 12, "较新的 中文描述"),
        ],
    }
    assert newest_entry(fixture, "en") == ("Newest English text", "new")
    selected = select_locales(fixture, "官网中文描述")
    assert selected["zh-CN"]["text"] == "较新的中文描述"
    assert selected["zh-CN"]["version"] == "middle"
    assert selected["pt-PT"]["text"] == selected["en-US"]["text"]
    assert selected["pt-PT"]["language"] == "en"
    without_chinese = dict(fixture)
    without_chinese["flavor_text_entries"] = fixture["flavor_text_entries"][:2]
    selected = select_locales(without_chinese, "官网中文描述")
    assert selected["zh-CN"] == {
        "text": "官网中文描述",
        "version": "pokemon-cn",
        "language": "zh-hans",
        "source": "pokemon-cn",
    }

    assert [entry["id"] for entry in SPECIES] == list(range(1, 1026))
    assert all(set(entry["descriptions"]) == set(LOCALE_LANGUAGES)
               for entry in SPECIES)
    descriptions = [value for entry in SPECIES
                    for value in entry["descriptions"].values()]
    assert all(value["text"] and "\0" not in value["text"] and
               "\n" not in value["text"] and "\f" not in value["text"]
               for value in descriptions)
    assert all(set(value) == {"text", "version", "language", "source"}
               for value in descriptions)
    assert all(value["language"] == "zh-hans" and
               any("\u3400" <= char <= "\u9fff" for char in value["text"])
               for entry in SPECIES for value in [entry["descriptions"]["zh-CN"]])
    assert "尾巴" in SPECIES[24]["descriptions"]["zh-CN"]["text"]
    packed = species_descriptions(SPECIES)
    assert set(packed) == set(LOCALE_LANGUAGES)
    assert all(text.isascii() for locale, values in packed.items() if locale != "zh-CN"
               for text in values)
    print("PASS official species descriptions: 1025 species x 7 locales")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
