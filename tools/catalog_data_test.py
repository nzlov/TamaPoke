#!/usr/bin/env python3
"""Validate the committed offline catalogues and their cross-resource IDs."""

from __future__ import annotations

import json
from pathlib import Path


HERE = Path(__file__).resolve().parent


def load(name: str) -> dict:
    document = json.loads((HERE / name).read_text(encoding="utf-8"))
    assert document["schema"] == 1, f"{name}: unsupported schema"
    return document


def main() -> int:
    pokemon = load("pokemon_data.json")
    move_document = load("move_data.json")
    item_document = load("item_data.json")
    battle = load("battle_data.json")

    species = pokemon["species"]
    moves = move_document["moves"]
    gmax_moves = move_document["gigantamaxMoves"]
    gmax_refs = pokemon["gigantamaxMoveRefs"]
    items = item_document["items"]
    abilities = battle["abilities"]
    types = battle["types"]

    species_ids = {row["id"] for row in species}
    move_ids = {row["id"] for row in moves}
    ability_ids = {row["id"] for row in abilities}
    type_slugs = {row["slug"] for row in types}
    egg_groups = pokemon["eggGroups"]
    art_source = pokemon["artSource"]
    art_root = HERE / art_source["root"]

    assert [row["id"] for row in species] == list(range(1, 1026))
    assert [row["id"] for row in moves] == list(range(1, 123))
    assert [row["id"] for row in gmax_moves] == list(range(1, 34))
    assert move_document["appendOnlyIds"] is True
    assert len({row["slug"] for row in species}) == len(species)
    assert len({row["slug"] for row in moves}) == len(moves)
    assert len({row["slug"] for row in gmax_moves}) == len(gmax_moves)
    assert all(row["sourceType"] in type_slugs and 0 <= row["power"] <= 255
               for row in gmax_moves)
    assert set(move_document["maxMoveNames"]) == {"en-US", "zh-CN"}
    assert all(len(names) == 19 for names in move_document["maxMoveNames"].values())
    assert len({row["key"] for row in items}) == len(items)
    assert len(abilities) == len(ability_ids)
    assert len(types) == 18 and len(type_slugs) == 18
    assert len(egg_groups) == 15 and len(set(egg_groups)) == 15
    assert len(battle["typeChartTenth"]) == 18
    assert all(len(row) == 18 for row in battle["typeChartTenth"])
    assert art_source["provider"] == "PMDCollab/SpriteCollab"
    assert len(art_source["revision"]) == 40
    assert (HERE / art_source["license"]).is_file()
    assert (HERE / art_source["credits"]).is_file()

    referenced_art = set()

    def validate_art(art: dict, allowed: set[str]) -> None:
        assert set(art).issubset(allowed)
        for ref in art.values():
            path = Path(ref)
            assert not path.is_absolute() and ".." not in path.parts
            folder = art_root / path
            assert (folder / "AnimData.xml").is_file()
            assert any(folder.glob("*-Anim.png"))
            referenced_art.add(path.as_posix())

    covered: list[int] = []
    region_names = {region["name"] for region in pokemon["regions"]}
    for region_id, region in enumerate(pokemon["regions"]):
        lo, hi = region["range"]
        assert region["id"] == region_id
        assert lo <= hi
        assert all(starter in range(lo, hi + 1) for starter in region["starters"])
        covered.extend(range(lo, hi + 1))
    assert covered == list(range(1, 1026))

    assert [row["species"] for row in gmax_refs] == sorted(
        row["species"] for row in gmax_refs)
    assert len({row["species"] for row in gmax_refs}) == len(gmax_refs)
    assert all(row["species"] in species_ids and 1 <= len(row["moveIds"]) <= 2
               for row in gmax_refs)
    referenced_gmax_moves = [move for row in gmax_refs for move in row["moveIds"]]
    assert referenced_gmax_moves == list(range(1, 34))
    gmax_species = {row["species"] for row in gmax_refs}

    mega_count = 0
    gmax_count = 0
    encounter_period_counts = {"day": 0, "night": 0, "both": 0}
    for row in species:
        validate_art(row["art"], {"normal", "shiny", "female", "femaleShiny"})
        assert set(row["types"]).issubset(type_slugs) and 1 <= len(row["types"]) <= 2
        assert row["region"] in region_names
        assert len(row["abilitySlots"]) == 3
        assert all(ability == 0 or ability in ability_ids for ability in row["abilitySlots"])
        assert all(target in species_ids for target in row["evolutions"])
        assert all(entry["moveId"] in move_ids for entry in row["learnset"])
        assert all(entry["method"] in {"level-up", "machine"} for entry in row["learnset"])
        assert len({entry["moveId"] for entry in row["learnset"]}) == len(row["learnset"])
        assert row["encounterPeriod"] in encounter_period_counts
        encounter_period_counts[row["encounterPeriod"]] += 1
        breeding = row["breeding"]
        assert breeding["eggGroupIds"]
        assert len(breeding["eggGroupIds"]) == len(set(breeding["eggGroupIds"]))
        assert all(1 <= value <= len(egg_groups) for value in breeding["eggGroupIds"])
        assert 1 <= len(breeding["offspringSpecies"]) <= 2
        assert all(value in species_ids for value in breeding["offspringSpecies"])
        assert breeding["eggMoveIds"] == sorted(set(breeding["eggMoveIds"]))
        assert all(value in move_ids for value in breeding["eggMoveIds"])
        gmax_count += int(row["gigantamax"])
        assert row["gigantamax"] == (row["id"] in gmax_species)
        for form in row["megaForms"]:
            mega_count += 1
            assert set(form["types"]).issubset(type_slugs) and 1 <= len(form["types"]) <= 2
            assert form["abilityId"] == 0 or form["abilityId"] in ability_ids
            assert form["learnsetSpecies"] in species_ids
            assert all(move_id in move_ids for move_id in form["additionalMoveIds"])
            assert form["learnsetSpecies"] == row["id"]
            assert form["additionalMoveIds"] == []
            validate_art(form.get("art", {}), {"normal", "shiny"})

    stored_art = {
        path.parent.relative_to(art_root).as_posix()
        for path in art_root.rglob("AnimData.xml")
    }
    assert stored_art == referenced_art

    assert mega_count == 93
    assert gmax_count == 32
    assert encounter_period_counts == {"day": 118, "night": 134, "both": 773}
    generator = (HERE / "gen_data_packs.py").read_text(encoding="utf-8")
    for source in ("pokemon_data.json", "move_data.json", "item_data.json", "battle_data.json"):
        assert source in generator
    for legacy in (
        "dex_data", "dex_moves", "mega_data.json", "ability_data.json",
        "breeding_data.json",
    ):
        assert legacy not in generator

    print(
        "PASS offline catalogues: "
        f"{len(species)} species, {mega_count} Mega forms, {gmax_count} Gigantamax species, "
        f"{len(moves)} moves, {len(items)} items, {len(abilities)} abilities, "
        f"{len(referenced_art)} local art sources"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
