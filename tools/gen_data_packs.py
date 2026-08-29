#!/usr/bin/env python3
"""Build SD-card packs from the committed local JSON catalogues."""

from __future__ import annotations

import binascii
import argparse
import json
import re
import struct
import sys
import unicodedata
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
WEB_PACKS = ROOT / "web" / "packs"
ITEM_ICON_CACHE = HERE / "item_icon_cache"

KIND_UI = 1
KIND_REGION = 2
KIND_MOVE = 3
KIND_ITEM = 5
KIND_BATTLE = 6

sys.path.insert(0, str(HERE))
from pack_format import PACK_ABI, PACK_REVISION, pack, pack_content_version  # noqa: E402
from pmd_layout import pmd_display_scale, pmd_pair_display_scale  # noqa: E402
from quiz_pack import build_quiz_pack  # noqa: E402
from dex_presentation import rgb565  # noqa: E402

POKEMON_DATA = json.loads((HERE / "pokemon_data.json").read_text(encoding="utf-8"))
MOVE_DATA = json.loads((HERE / "move_data.json").read_text(encoding="utf-8"))
ITEM_DOCUMENT = json.loads((HERE / "item_data.json").read_text(encoding="utf-8"))
BATTLE_DATA = json.loads((HERE / "battle_data.json").read_text(encoding="utf-8"))
SPECIES = POKEMON_DATA["species"]
MOVES = MOVE_DATA["moves"]
GIGANTAMAX_MOVES = MOVE_DATA["gigantamaxMoves"]
GIGANTAMAX_REFS = POKEMON_DATA["gigantamaxMoveRefs"]
ITEM_DATA = ITEM_DOCUMENT["items"]
ABILITIES = BATTLE_DATA["abilities"]
TYPES = BATTLE_DATA["types"]
TYPE_ORDER = [row["slug"] for row in TYPES]
REGIONS = [
    (row["name"], row["range"][0], row["range"][1], row["starters"])
    for row in POKEMON_DATA["regions"]
]
REGION_LOCALES = json.loads((HERE / "region_locales.json").read_text(encoding="utf-8"))
SPECIES_DESCRIPTION_LOCALES = sorted(SPECIES[0]["descriptions"])
MEGA_DATA = [
    dict(form, species=species["id"])
    for species in SPECIES for form in species["megaForms"]
]
GIGANTAMAX_BY_SPECIES = {
    int(row["species"]): [int(move) for move in row["moveIds"]]
    for row in GIGANTAMAX_REFS
}

MOVE_TAG_BITS = MOVE_DATA["tagBits"]

TYPE_COLORS = [row["color"] for row in TYPES]
TYPE_LIGHT = [1 if row["light"] else 0 for row in TYPES]
TYPE_ZH = {row["slug"]: row["names"]["zh-CN"] for row in TYPES}
EGG_GROUP_COUNT = len(POKEMON_DATA["eggGroups"])

GMAX_EFFECT_NAMES = (
    "vine-lash", "wildfire", "cannonade", "befuddle", "volt-crash",
    "gold-rush", "chi-strike", "terror", "foam-burst", "resonance",
    "cuddle", "replenish", "malodor", "meltdown", "drum-solo",
    "fireball", "hydrosnipe", "wind-rage", "gravitas", "stonesurge",
    "volcalith", "tartness", "sweetness", "sandblast", "stun-shock",
    "centiferno", "smite", "snooze", "finale", "steelsurge",
    "depletion", "one-blow", "rapid-flow",
)
GMAX_EFFECT_IDS = {name: index + 1 for index, name in enumerate(GMAX_EFFECT_NAMES)}


def string_pool(values: list[str]) -> tuple[bytes, list[int]]:
    blob = bytearray()
    offsets = []
    for value in values:
        offsets.append(len(blob))
        blob.extend(value.encode("utf-8"))
        blob.append(0)
    return bytes(blob), offsets


def indexed_strings(values: list[str]) -> bytes:
    blob, offsets = string_pool(values)
    head = struct.pack("<H", len(values))
    table = struct.pack(f"<{len(offsets) + 1}I", *offsets, len(blob))
    return head + table + blob


def localized_strings(locale_values: dict[str, list[str]], item_count: int) -> bytes:
    locales = sorted(locale_values)
    header_size = 4 + len(locales) * 28
    blocks = bytearray()
    headers = []
    for locale in locales:
        values = locale_values[locale]
        if len(values) != item_count:
            raise ValueError(f"{locale}: expected {item_count} descriptions, got {len(values)}")
        blob, offsets = string_pool(values)
        index_at = header_size + len(blocks)
        blocks.extend(struct.pack(f"<{len(offsets) + 1}I", *offsets, len(blob)))
        blob_at = header_size + len(blocks)
        blocks.extend(blob)
        headers.append((locale, index_at, blob_at, len(blob)))
    out = bytearray(struct.pack("<HH", len(locales), item_count))
    for locale, index_at, blob_at, blob_size in headers:
        code = locale.encode("ascii")[:15].ljust(16, b"\0")
        out.extend(struct.pack("<16sIII", code, index_at, blob_at, blob_size))
    out.extend(blocks)
    return bytes(out)


def packed_item_icons(items: list[dict]) -> bytes:
    record = struct.Struct("<II")
    header_size = 4 + len(items) * record.size
    index = bytearray()
    payload = bytearray()
    packed_count = 0
    for item in items:
        slug = item.get("icon")
        path = ITEM_ICON_CACHE / f"{slug}.ticon" if slug else None
        if not path or not path.exists():
            index.extend(record.pack(0, 0))
            continue
        icon = path.read_bytes()
        if len(icon) < 8 or icon[:4] != b"TIC1":
            raise ValueError(f"{path}: invalid TIC1 item icon")
        width, height, palette_count, reserved = icon[4:8]
        expected_size = 8 + palette_count * 2 + width * height
        if not width or not height or width > 32 or height > 32 or not palette_count or \
                reserved or len(icon) != expected_size:
            raise ValueError(f"{path}: invalid TIC1 dimensions or size")
        pixels = icon[8 + palette_count * 2:]
        if any(pixel != 0xFF and pixel >= palette_count for pixel in pixels):
            raise ValueError(f"{path}: invalid TIC1 palette index")
        index.extend(record.pack(header_size + len(payload), len(icon)))
        payload.extend(icon)
        packed_count += 1
    if not packed_count:
        return b""
    return struct.pack("<HH", len(items), record.size) + bytes(index) + bytes(payload)


def alpha4_from_rows(rows: list[int]) -> bytes:
    pixels = []
    for row in range(16):
        bits = rows[row] if row < len(rows) else 0
        pixels.extend(15 if bits & (1 << col) else 0 for col in range(16))
    return bytes(pixels[index] | (pixels[index + 1] << 4)
                 for index in range(0, len(pixels), 2))


def parse_glcd_font() -> list[tuple[int, int, int, int, int, int, bytes]]:
    source = (HERE / "emu" / "font.cpp").read_text(encoding="utf-8")
    body = source[source.index("GLCD_FONT[1280]"):]
    values = [int(token, 16) for token in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    if len(values) != 1280:
        raise ValueError(f"expected 1280 GLCD bytes, got {len(values)}")
    glyphs = []
    for codepoint in range(32, 127):
        columns = values[codepoint * 5:(codepoint + 1) * 5]
        rows = []
        for row in range(8):
            bits = 0
            for col, column in enumerate(columns):
                if column & (1 << row):
                    bits |= 1 << col
            rows.append(bits)
        glyphs.append((codepoint, 5, 8, 0, 0, 6, alpha4_from_rows(rows)))
    return glyphs


def font_section(spec: str | dict) -> bytes:
    if isinstance(spec, dict):
        if spec.get("format") != "opentype":
            raise ValueError(f"unknown UI font format: {spec.get('format')}")
        relative = Path(spec["file"])
        path = (HERE / "assets" / relative).resolve()
        if (HERE / "assets").resolve() not in path.parents:
            raise ValueError("UI font must live under tools/assets")
        font = path.read_bytes()
        if font[:4] not in (b"OTTO", b"ttcf", b"\0\1\0\0"):
            raise ValueError(f"{path.name} is not a supported OpenType font")
        configured = spec.get("sizes", [])
        if not 0 < len(configured) <= 8 or any(not 0 < size <= 255 for size in configured) or \
                any(left >= right for left, right in zip(configured, configured[1:])):
            raise ValueError("OpenType UI sizes must be 1..8 increasing pixel heights")
        sizes = bytes(configured).ljust(8, b"\0")
        face = int(spec.get("face", 0))
        if not 0 <= face <= 255:
            raise ValueError("OpenType face index must fit one byte")
        return struct.pack("<4sBBBB8sI", b"FNT5", 1, face, len(configured), 0,
                           sizes, len(font)) + font
    if spec != "glcd-5x7":
        raise ValueError(f"unknown UI font: {spec}")
    glyphs = parse_glcd_font()
    line_height = 8
    out = bytearray(struct.pack("<4sBBH", b"FNT4", line_height, 7, len(glyphs)))
    glyph_record = struct.Struct("<IBBBbb128s")
    for codepoint, width, height, xoff, yoff, advance, alpha4 in glyphs:
        out.extend(glyph_record.pack(
            codepoint, width, height, advance, xoff, yoff, alpha4,
        ))
    return bytes(out)


def build_ui_packs(manifest: list[dict]) -> None:
    data = json.loads((HERE / "ui_data.json").read_text(encoding="utf-8"))
    for locale in data["locales"]:
        code = locale["locale"]
        strings = (
            locale["strings"] + locale["medalNames"] +
            locale["medalLabels"] + locale["medalDescriptions"] +
            locale["natureDescriptions"]
        )
        meta = struct.pack(
            "<16s8s32sBBBB",
            code.encode("ascii")[:15].ljust(16, b"\0"),
            locale["shortLabel"].encode("utf-8")[:7].ljust(8, b"\0"),
            locale["displayName"].encode("utf-8")[:31].ljust(32, b"\0"),
            1 if code == data["defaultLocale"] else 0,
            1 if locale.get("isCjk", False) else 0,
            0, 0,
        )
        layout = locale["layout"]
        layout_blob = b"".join([
            struct.pack("<Hh", 1, layout["compactTextHeight"]),
            struct.pack("<Hh", 2, layout["minimumTouchTarget"]),
            struct.pack("<Hh", 3, layout["minimumTouchGap"]),
            struct.pack("<Hh", 4, layout.get("statLabelX", 70)),
            struct.pack("<Hh", 5, layout.get("statBarX", 132)),
            struct.pack("<Hh", 6, layout.get("statBarWidth", 130)),
            struct.pack("<Hh", 7, layout.get("statValueX", 272)),
            struct.pack("<Hh", 8, layout.get("detailSpriteGround", 260)),
            struct.pack("<Hh", 9, layout.get("detailDescriptionY", 282)),
            struct.pack("<Hh", 10, layout.get("detailDescriptionLines", 5)),
            struct.pack("<Hh", 11, layout.get("detailBackY", 408)),
            struct.pack("<Hh", 12, layout.get("moveDescriptionY", 158)),
            struct.pack("<Hh", 13, layout.get("moveDescriptionLines", 8)),
            struct.pack("<Hh", 14, layout.get("moveChangeY", 320)),
            struct.pack("<Hh", 15, layout.get("detailDescriptionTextSize", 1)),
            struct.pack("<Hh", 16, layout.get("moveDescriptionTextSize", 1)),
            struct.pack("<Hh", 17, layout.get("detailDescriptionX", 78)),
            struct.pack("<Hh", 18, layout.get("detailDescriptionWidth", 310)),
            struct.pack("<Hh", 19, layout.get("moveDescriptionX", 78)),
            struct.pack("<Hh", 20, layout.get("moveDescriptionWidth", 310)),
        ])
        font_blob = font_section(locale["font"])
        font_count = 1 if font_blob[:4] == b"FNT5" else struct.unpack_from("<H", font_blob, 6)[0]
        blob = pack(KIND_UI, f"ui-{code.lower()}", 0, [
            ("META", meta, 1),
            ("STRS", indexed_strings(strings), len(strings)),
            ("FONT", font_blob, font_count),
            ("LAYT", layout_blob, 20),
        ])
        path = WEB_PACKS / f"ui-{code}.tui"
        path.write_bytes(blob)
        item = pack_manifest(path, "ui", f"ui-{code.lower()}", [code])
        item["label"] = locale["displayName"]
        item["default"] = code == data["defaultLocale"]
        manifest.append(item)


def bitmap_font_text(value: str) -> str:
    """Keep official Latin wording readable on the existing ASCII bitmap UI."""
    # GLUE: Latin UI packs still use the ASCII-only 5x7 face. Remove this
    # transliteration when those packs move to a Unicode font.
    punctuation = str.maketrans({
        "’": "'", "‘": "'", "“": '"', "”": '"', "–": "-", "—": "-",
        "…": "...", "¡": "!", "¿": "?", "♀": " female", "♂": " male",
        "œ": "oe", "Œ": "OE", "ß": "ss",
    })
    normalized = unicodedata.normalize("NFKD", value.translate(punctuation))
    return normalized.encode("ascii", "ignore").decode("ascii")


def species_descriptions(rows: list[dict]) -> dict[str, list[str]]:
    locales = sorted(SPECIES[0]["descriptions"])
    result = {locale: [] for locale in locales}
    for row in rows:
        descriptions = row["descriptions"]
        if set(descriptions) != set(locales):
            raise ValueError(f"species {row['id']} description catalogue is incomplete")
        for locale in locales:
            value = descriptions[locale]["text"]
            result[locale].append(value if locale == "zh-CN" else bitmap_font_text(value))
    return result


def species_names(rows: list[dict]) -> dict[str, list[str]]:
    locales = sorted(rows[0]["names"])
    return {locale: [row["names"][locale] for row in rows] for locale in locales}


def regional_names(region_name: str, battle: dict) -> dict[str, list[str]]:
    values = {
        "en-US": [region_name] + [
            value
            for trainer in battle["trainers"]
            for value in (trainer["name"], trainer["place"])
        ],
    }
    for locale, locale_data in REGION_LOCALES["locales"].items():
        localized = locale_data["regions"].get(region_name)
        if not localized:
            continue
        trainers = localized["trainers"]
        if len(trainers) != len(battle["trainers"]):
            raise ValueError(
                f"{locale} {region_name}: expected {len(battle['trainers'])} trainers, "
                f"got {len(trainers)}"
            )
        values[locale] = [localized["name"]] + [
            value
            for trainer in trainers
            for value in (trainer["name"], trainer["place"])
        ]
    return values


def append_region_manifest(manifest: list[dict], path: Path, region_name: str,
                           lo: int, hi: int, battle: dict) -> None:
    item = pack_manifest(
        path, "region", f"region-{region_name.lower()}", SPECIES_DESCRIPTION_LOCALES
    )
    item["label"] = region_name.title()
    item["region"] = region_name
    item["range"] = [lo, hi]
    required_regions = set()
    for trainer in battle["trainers"]:
        for member in trainer["team"]:
            for required_name, required_lo, required_hi, _starters in REGIONS:
                if required_lo <= member["species"] <= required_hi:
                    required_regions.add(required_name.lower())
                    break
    item["requires"] = ["battle-core", "moves-core"] + [
        f"region-{name}" for name in sorted(required_regions)
        if name != region_name.lower()
    ]
    manifest.append(item)


def build_region_packs(manifest: list[dict], sprite_dir: Path,
                       allow_empty_art: bool = False) -> None:
    region_battles = json.loads((HERE / "region_data.json").read_text(encoding="utf-8"))
    battle_by_name = {row["name"].upper(): row for row in region_battles["regions"]}
    type_ids = {name: index for index, name in enumerate(TYPE_ORDER)}
    spec_record = struct.Struct("<HBBH11BIB")
    ability_record = struct.Struct("<HHHH")
    evolution_record = struct.Struct("<HH")
    learn_record = struct.Struct("<HBB")
    breeding_record = struct.Struct("<HHHHIH")
    mega_record = struct.Struct("<HBBBBBBBBHH")
    sprite_record = struct.Struct("<HIIIIIIIIB")
    trainer_record = struct.Struct("<BBBBII" + "HB" * 6)
    badge_record = struct.Struct("<BBBBII")
    for region_id, (region_name, lo, hi, starters) in enumerate(REGIONS):
        battle = battle_by_name[region_name]
        path = WEB_PACKS / f"region-{region_name.lower()}.tregion"
        rows = [row for row in SPECIES if lo <= row["id"] <= hi]
        normal_sources = [sprite_dir / f"p{row['id']:03d}.bin" for row in rows]
        normal_count = sum(source.exists() for source in normal_sources)
        mega_source_count = sum(
            (sprite_dir / f"pm{int(form['species']):03d}-{form.get('form', 'standard')}.bin").exists()
            for form in MEGA_DATA
            if lo <= int(form["species"]) <= hi and
            form.get("art", {}).get("normal")
        )
        if normal_count == 0 and mega_source_count == 0 and not allow_empty_art:
            raise FileNotFoundError(
                f"{sprite_dir} has no regional sprite sources; "
                "run pack_pmd.py and make_thumbs.py first"
            )
        if normal_count != len(rows):
            print(f"{region_name}: packing {normal_count}/{len(rows)} species with art")
        if mega_source_count:
            print(f"{region_name}: packing {mega_source_count} Mega forms with art")
        thumbs_path = sprite_dir / "thumbs.bin"
        if not thumbs_path.exists() and not allow_empty_art:
            raise FileNotFoundError(f"{thumbs_path} is required to rebuild regional packs")
        names_blob, name_offsets = string_pool([row["names"]["en-US"] for row in rows])
        specs = bytearray()
        ability_slots = bytearray()
        evolutions = bytearray()
        learn_offsets = [0]
        learns = bytearray()
        breeding_records = bytearray()
        egg_moves = bytearray()
        for row, name_offset in zip(rows, name_offsets):
            number = row["id"]
            stats = row["stats"]
            hp, atk, defense, speed, spa, spd = (
                stats["hp"], stats["attack"], stats["defense"], stats["speed"],
                stats["specialAttack"], stats["specialDefense"],
            )
            t1 = row["types"][0]
            t2 = row["types"][1] if len(row["types"]) > 1 else None
            specs.extend(spec_record.pack(
                number, row["evolutionLevel"], row["rarity"],
                rgb565(row["accent"]),
                hp, atk, defense, speed, spa, spd,
                row["biome"],
                type_ids[t1], type_ids[t2] if t2 else 255, region_id,
                {"day": 1, "night": 2, "both": 3}[row["encounterPeriod"]],
                name_offset, 255 if row["femaleRate"] < 0 else row["femaleRate"],
            ))
            slots = row["abilitySlots"]
            if slots is None or len(slots) != 3 or not slots[0]:
                raise ValueError(f"species {number}: incomplete ability slots")
            ability_slots.extend(ability_record.pack(number, *(int(value) for value in slots)))
            targets = row["evolutions"]
            if len(targets) > 8:
                raise ValueError(f"species {number}: more than 8 evolution targets")
            for target in targets:
                evolutions.extend(evolution_record.pack(number, target))
            for learned in row["learnset"]:
                method = 1 if learned["method"] == "machine" else 0
                learns.extend(learn_record.pack(
                    learned["moveId"], learned["level"], method
                ))
            learn_offsets.append(len(learns) // learn_record.size)

            breeding = row["breeding"]
            group_ids = [int(value) for value in breeding["eggGroupIds"]]
            offspring = [int(value) for value in breeding["offspringSpecies"]]
            inherited_moves = sorted({int(value) for value in breeding["eggMoveIds"]})
            if not group_ids or len(group_ids) != len(set(group_ids)) or \
                    any(not 1 <= value <= EGG_GROUP_COUNT for value in group_ids) or \
                    not 1 <= len(offspring) <= 2 or \
                    any(not 0 < value <= len(SPECIES) for value in offspring) or \
                    any(not 0 < value <= len(MOVES) for value in inherited_moves):
                raise ValueError(f"species {number}: invalid breeding metadata")
            group_mask = sum(1 << (value - 1) for value in group_ids)
            egg_move_offset = len(egg_moves) // 2
            for move in inherited_moves:
                egg_moves.extend(struct.pack("<H", move))
            breeding_records.extend(breeding_record.pack(
                number, group_mask, offspring[0], offspring[-1],
                egg_move_offset, len(inherited_moves),
            ))

        learn_offset_blob = struct.pack(f"<{len(learn_offsets)}I", *learn_offsets)
        mega_forms = bytearray()
        form_ids = {"standard": 0, "x": 1, "y": 2, "z": 3}
        for row in rows:
            for form in row["megaForms"]:
                stats = form["stats"]
                types = form["types"]
                mega_forms.extend(mega_record.pack(
                    row["id"], form_ids[form["form"]], type_ids[types[0]],
                    type_ids[types[1]] if len(types) > 1 else 255,
                    stats["attack"], stats["defense"], stats["specialAttack"],
                    stats["specialDefense"], stats["speed"], form["abilityId"],
                    form["learnsetSpecies"],
                ))
        gigantamax = bytearray()
        for row in rows:
            move_ids = GIGANTAMAX_BY_SPECIES.get(row["id"], [])
            if row["gigantamax"] != bool(move_ids) or len(move_ids) > 2:
                raise ValueError(f"species {row['id']}: invalid Gigantamax move references")
            if move_ids:
                gigantamax.extend(struct.pack(
                    "<HBB", row["id"], *(move_ids + [0] * (2 - len(move_ids)))
                ))

        region = struct.pack(
            "<B16sHHB16H",
            region_id, region_name.encode("ascii")[:15].ljust(16, b"\0"),
            lo, hi, len(starters), *(list(starters) + [0] * (16 - len(starters))),
        )

        gym_strings, gym_offsets = string_pool([
            value
            for trainer in battle["trainers"]
            for value in (trainer["name"], trainer["place"])
        ])
        trainers = bytearray()
        for trainer_id, trainer in enumerate(battle["trainers"]):
            team_values = []
            for member in trainer["team"]:
                team_values.extend([member["species"], member["level"]])
            team_values.extend([0, 0] * (6 - len(trainer["team"])))
            trainers.extend(trainer_record.pack(
                trainer_id, trainer["type"], len(trainer["team"]), 0,
                gym_offsets[trainer_id * 2], gym_offsets[trainer_id * 2 + 1],
                *team_values,
            ))
        battle_meta = struct.pack(
            "<BBBBBBH", region_id, battle["trainerCount"], battle["gymCount"],
            battle["elite4Count"], battle["easyIv"], battle["hardIv"], 0,
        )

        badge_index = bytearray()
        badge_blob = bytearray()
        for badge_id, badge in enumerate(battle["badges"]):
            palette_at = len(badge_blob)
            badge_blob.extend(struct.pack(f"<{len(badge['palette'])}H", *badge["palette"]))
            pixels_at = len(badge_blob)
            badge_blob.extend(bytes(badge["indices"]))
            badge_index.extend(badge_record.pack(
                badge_id, badge["width"], badge["height"], len(badge["palette"]),
                palette_at, pixels_at,
            ))
        sprites = bytearray()
        sprite_index = bytearray()
        for row in rows:
            number = row["id"]
            normal_path = sprite_dir / f"p{number:03d}.bin"
            normal = normal_path.read_bytes() if normal_path.exists() else b""
            shiny_path = sprite_dir / f"ps{number:03d}.bin"
            shiny = shiny_path.read_bytes() if normal and shiny_path.exists() else b""
            female_path = sprite_dir / f"pf{number:03d}.bin"
            female = female_path.read_bytes() if normal and female_path.exists() else b""
            female_shiny_path = sprite_dir / f"pfs{number:03d}.bin"
            female_shiny = (female_shiny_path.read_bytes()
                            if female and female_shiny_path.exists() else b"")
            normal_at = len(sprites)
            sprites.extend(normal)
            shiny_at = len(sprites)
            sprites.extend(shiny)
            female_at = len(sprites)
            sprites.extend(female)
            female_shiny_at = len(sprites)
            sprites.extend(female_shiny)
            sprite_index.extend(sprite_record.pack(
                number, normal_at, len(normal), shiny_at, len(shiny),
                female_at, len(female), female_shiny_at, len(female_shiny),
                min((pmd_display_scale(blob)
                     for blob in (normal, shiny, female, female_shiny) if blob),
                    default=0),
            ))
        mega_sprites = bytearray()
        mega_sprite_index = bytearray()
        mega_sprite_record = struct.Struct("<HBBIIII")
        form_ids = {"standard": 0, "x": 1, "y": 2, "z": 3}
        for form in MEGA_DATA:
            species = int(form["species"])
            if not lo <= species <= hi or not form.get("art", {}).get("normal"):
                continue
            form_name = form.get("form", "standard")
            normal_path = sprite_dir / f"pm{species:03d}-{form_name}.bin"
            normal = normal_path.read_bytes() if normal_path.exists() else b""
            shiny_path = sprite_dir / f"pm{species:03d}-{form_name}-shiny.bin"
            shiny = shiny_path.read_bytes() if normal and shiny_path.exists() else b""
            if not normal:
                continue
            normal_at = len(mega_sprites)
            mega_sprites.extend(normal)
            shiny_at = len(mega_sprites)
            mega_sprites.extend(shiny)
            mega_sprite_index.extend(mega_sprite_record.pack(
                species, form_ids[form_name], pmd_pair_display_scale(normal, shiny),
                normal_at, len(normal), shiny_at, len(shiny),
            ))
        thumbs = thumbs_path.read_bytes() if thumbs_path.exists() else b""
        locales = localized_strings(species_descriptions(rows), len(rows))
        localized_names = localized_strings(species_names(rows), len(rows))
        localized_regional_names = localized_strings(
            regional_names(region_name, battle), 1 + len(battle["trainers"]) * 2,
        )
        mechanics_hash = binascii.crc32(
            specs + ability_slots + evolutions + learn_offset_blob + learns +
            breeding_records + egg_moves + mega_forms + gigantamax +
            region + battle_meta + trainers
        ) & 0xFFFFFFFF
        sections = [
            ("SPEC", bytes(specs), len(rows)),
            ("ASLT", bytes(ability_slots), len(rows)),
            ("EVOS", bytes(evolutions), len(evolutions) // evolution_record.size),
            ("LOFS", learn_offset_blob, len(learn_offsets)),
            ("LERN", bytes(learns), len(learns) // learn_record.size),
            ("BRSP", bytes(breeding_records), len(rows)),
            ("BEMV", bytes(egg_moves), len(egg_moves) // 2),
            ("MEGA", bytes(mega_forms), len(mega_forms) // mega_record.size),
            ("GMAX", bytes(gigantamax), len(gigantamax) // 4),
            ("NAME", names_blob, len(rows)),
            ("LNAM", localized_names, len(rows)),
            ("REGN", region, 1),
            ("RLNM", localized_regional_names, 1 + len(battle["trainers"]) * 2),
            ("SPRI", bytes(sprite_index), len(rows)),
            ("SBLB", bytes(sprites), len(rows)),
            ("THMB", thumbs, 1 if thumbs else 0),
            ("LOCL", locales, len(rows)),
            ("BTTL", battle_meta, 1),
            ("TRNR", bytes(trainers), len(battle["trainers"])),
            ("GSTR", gym_strings, len(battle["trainers"]) * 2),
            ("BADG", bytes(badge_index), len(battle["badges"])),
            ("BBLB", bytes(badge_blob), len(battle["badges"])),
        ]
        if mega_sprite_index:
            sections.extend([
                ("MFSP", bytes(mega_sprite_index),
                 len(mega_sprite_index) // mega_sprite_record.size),
                ("MFBL", bytes(mega_sprites),
                 len(mega_sprite_index) // mega_sprite_record.size),
            ])
        blob = pack(KIND_REGION, f"region-{region_name.lower()}", mechanics_hash, sections)
        path.write_bytes(blob)
        append_region_manifest(manifest, path, region_name, lo, hi, battle)


def move_names() -> dict[str, list[str]]:
    locales = sorted(MOVES[0]["names"])
    return {locale: ["-"] + [row["names"][locale] for row in MOVES]
            for locale in locales}


def move_descriptions() -> dict[str, list[str]]:
    locales = sorted(MOVES[0]["descriptions"])
    empty = {"en-US": "Empty move slot.", "zh-CN": "空招式槽。"}
    return {locale: [empty[locale]] + [row["descriptions"][locale] for row in MOVES]
            for locale in locales}


def localized_type_names() -> dict[str, list[str]]:
    return {"zh-CN": [row["names"]["zh-CN"] for row in TYPES]}


def required_ui_codepoints() -> set[int]:
    def text_values(value):
        if isinstance(value, str):
            yield value
        elif isinstance(value, list):
            for item in value:
                yield from text_values(item)
        elif isinstance(value, dict):
            for item in value.values():
                yield from text_values(item)

    sources = [
        json.loads((HERE / "ui_data.json").read_text(encoding="utf-8")),
        REGION_LOCALES, POKEMON_DATA, MOVE_DATA, ITEM_DOCUMENT, BATTLE_DATA,
    ]
    return set(range(32, 127)) | {
        ord(char)
        for source in sources
        for value in text_values(source)
        for char in value
        if ord(char) > 127
    }


def build_move_pack(manifest: list[dict]) -> None:
    type_ids = {name: index for index, name in enumerate(TYPE_ORDER)}
    if [row["id"] for row in MOVES] != list(range(1, len(MOVES) + 1)):
        raise ValueError("move IDs must remain contiguous and append-only")
    names_blob, name_offsets = string_pool(
        ["-"] + [row["names"]["en-US"] for row in MOVES]
    )
    move_record = struct.Struct("<HBBBBBbBbBBBI")
    move_blob = bytearray()
    field_flags = bytearray([0])
    move_tags = bytearray(struct.pack("<H", 0))
    sentinel = {
        "id": 0, "type": "normal", "category": 2, "power": 0, "accuracy": 0,
        "effect": 0, "param": 0, "statMask": 0, "stages": 0, "target": 0,
        "ailment": 0, "ailmentChance": 0, "fieldFlags": 0, "tags": [],
    }
    for row, name_offset in zip([sentinel] + MOVES, name_offsets):
        move_id = row["id"]
        move_blob.extend(move_record.pack(
            move_id, type_ids[row["type"]], row["category"], row["power"],
            row["accuracy"], row["effect"], row["param"], row["statMask"],
            row["stages"], row["target"], row["ailment"], row["ailmentChance"],
            name_offset,
        ))
        if move_id:
            field_flags.append(row["fieldFlags"])
            tag_names = row["tags"]
            if len(tag_names) != len(set(tag_names)) or any(
                    tag not in MOVE_TAG_BITS for tag in tag_names):
                raise ValueError(f"move {move_id}: invalid tags {tag_names}")
            tag_mask = sum(int(MOVE_TAG_BITS[tag]) for tag in tag_names)
            move_tags.extend(struct.pack("<H", tag_mask))
    move_count = len(MOVES) + 1
    locales = localized_strings(move_descriptions(), move_count)
    localized_names = localized_strings(move_names(), move_count)

    if [row["id"] for row in GIGANTAMAX_MOVES] != \
            list(range(1, len(GIGANTAMAX_MOVES) + 1)) or len(GIGANTAMAX_MOVES) > 255:
        raise ValueError("Gigantamax move IDs must be contiguous uint8 values")
    gmax_names, gmax_name_offsets = string_pool(
        [row["names"]["en-US"] for row in GIGANTAMAX_MOVES]
    )
    gmax_moves = bytearray()
    gmax_move_record = struct.Struct("<BBBBI")
    for row, name_offset in zip(GIGANTAMAX_MOVES, gmax_name_offsets):
        effect = GMAX_EFFECT_IDS.get(row["effect"], 0)
        source_type = type_ids.get(row["sourceType"], 255)
        power = int(row["power"])
        if source_type >= len(TYPE_ORDER) or not effect or not 0 <= power <= 255 or \
                set(row["names"]) != {"en-US", "zh-CN"} or \
                not all(row["names"].values()):
            raise ValueError(f"Gigantamax move {row['id']}: invalid definition")
        gmax_moves.extend(gmax_move_record.pack(
            row["id"], source_type, effect, power, name_offset,
        ))
    referenced_gmax_moves = {
        move for move_ids in GIGANTAMAX_BY_SPECIES.values() for move in move_ids
    }
    if referenced_gmax_moves != set(range(1, len(GIGANTAMAX_MOVES) + 1)):
        raise ValueError("Gigantamax move references must cover the move catalogue exactly")
    gmax_localized_names = localized_strings({
        "zh-CN": [row["names"]["zh-CN"] for row in GIGANTAMAX_MOVES]
    }, len(GIGANTAMAX_MOVES))
    max_move_names = MOVE_DATA["maxMoveNames"]
    if set(max_move_names) != {"en-US", "zh-CN"} or \
            any(len(values) != len(TYPE_ORDER) + 1 for values in max_move_names.values()):
        raise ValueError("Max Move names must cover every type plus Max Guard")
    max_names, _max_name_offsets = string_pool(max_move_names["en-US"])
    max_localized_names = localized_strings({
        "zh-CN": max_move_names["zh-CN"]
    }, len(TYPE_ORDER) + 1)

    mechanics_hash = binascii.crc32(
        move_blob + field_flags + move_tags + gmax_moves
    ) & 0xFFFFFFFF
    sections = [
        ("MOVE", bytes(move_blob), move_count),
        ("MFLG", bytes(field_flags), move_count),
        ("MTAG", bytes(move_tags), move_count),
        ("NAME", names_blob, move_count),
        ("LNAM", localized_names, move_count),
        ("LOCL", locales, move_count),
        ("GMOV", bytes(gmax_moves), len(GIGANTAMAX_MOVES)),
        ("GMNM", gmax_names, len(GIGANTAMAX_MOVES)),
        ("GMLN", gmax_localized_names, len(GIGANTAMAX_MOVES)),
        ("MXNM", max_names, len(TYPE_ORDER) + 1),
        ("MXLN", max_localized_names, len(TYPE_ORDER) + 1),
    ]
    path = WEB_PACKS / "moves-core.tmove"
    path.write_bytes(pack(KIND_MOVE, "moves-core", mechanics_hash, sections))
    item = pack_manifest(path, "move", "moves-core", ["en-US", "zh-CN"])
    item["label"] = "Moves"
    item["requires"] = ["battle-core"]
    manifest.append(item)


def build_item_pack(manifest: list[dict]) -> None:
    item_keys = [int(item["key"]) for item in ITEM_DATA]
    if len(item_keys) != len(set(item_keys)) or any(not 0 < key <= 0xFFFF for key in item_keys):
        raise ValueError("item keys must be unique non-zero uint16 values")
    item_names, item_name_offsets = string_pool([item["names"]["en-US"] for item in ITEM_DATA])
    item_record = struct.Struct("<HBBBBhHBBI")
    item_blob = bytearray()
    for item, name_offset in zip(ITEM_DATA, item_name_offsets):
        item_blob.extend(item_record.pack(
            item["key"], item["category"], item["effect"], item["rarity"],
            item.get("flags", 0), item.get("param", 0), item.get("dropWeight", 0),
            item.get("dailyMin", 0), 0, name_offset,
        ))
    item_localized_names = localized_strings({
        locale: [item["names"][locale] for item in ITEM_DATA]
        for locale in ("en-US", "zh-CN")
    }, len(ITEM_DATA))
    item_localized_descriptions = localized_strings({
        locale: [item["descriptions"][locale] for item in ITEM_DATA]
        for locale in ("en-US", "zh-CN")
    }, len(ITEM_DATA))
    item_icons = packed_item_icons(ITEM_DATA)
    mechanics_hash = binascii.crc32(item_blob) & 0xFFFFFFFF
    sections = [
        ("ITEM", bytes(item_blob), len(ITEM_DATA)),
        ("INAM", item_names, len(ITEM_DATA)),
        ("ILNM", item_localized_names, len(ITEM_DATA)),
        ("ILOC", item_localized_descriptions, len(ITEM_DATA)),
    ]
    if item_icons:
        sections.append(("IICO", item_icons, len(ITEM_DATA)))
    path = WEB_PACKS / "items-core.titem"
    path.write_bytes(pack(KIND_ITEM, "items-core", mechanics_hash, sections))
    item = pack_manifest(path, "item", "items-core", ["en-US", "zh-CN"])
    item["label"] = "Items"
    item["requires"] = ["moves-core"]
    manifest.append(item)


def build_battle_pack(manifest: list[dict]) -> None:
    ability_rows = ABILITIES
    if not ability_rows or len(ability_rows) > 512:
        raise ValueError("ability catalogue is empty or too large")
    ability_keys = [int(row["id"]) for row in ability_rows]
    if ability_keys != sorted(set(ability_keys)) or any(not key or key > 0xFFFF for key in ability_keys):
        raise ValueError("ability keys must be sorted unique non-zero uint16 values")
    ability_names, ability_name_offsets = string_pool(
        [row["names"]["en-US"] for row in ability_rows]
    )
    ability_blob = bytearray()
    ability_record = struct.Struct("<HI")
    for row, name_offset in zip(ability_rows, ability_name_offsets):
        ability_blob.extend(ability_record.pack(int(row["id"]), name_offset))
    ability_localized_names = localized_strings({
        locale: [row["names"][locale] for row in ability_rows]
        for locale in ("en-US", "zh-CN")
    }, len(ability_rows))
    ability_localized_descriptions = localized_strings({
        locale: [row["descriptions"][locale] for row in ability_rows]
        for locale in ("en-US", "zh-CN")
    }, len(ability_rows))
    type_names, type_name_offsets = string_pool([row["names"]["en-US"] for row in TYPES])
    type_blob = bytearray()
    for name_offset, row in zip(type_name_offsets, TYPES):
        type_blob.extend(struct.pack(
            "<IHBB", name_offset, row["color"], 1 if row["light"] else 0, 0
        ))
    chart_rows = BATTLE_DATA["typeChartTenth"]
    if len(chart_rows) != len(TYPES) or any(len(row) != len(TYPES) for row in chart_rows):
        raise ValueError("type chart dimensions do not match type catalogue")
    chart = bytes(value for row in chart_rows for value in row)
    type_locales = localized_strings(localized_type_names(), len(TYPES))
    mechanics_hash = binascii.crc32(chart + type_blob + ability_blob) & 0xFFFFFFFF
    sections = [
        ("TYPS", bytes(type_blob), len(TYPES)),
        ("TSTR", type_names, len(TYPES)),
        ("TLNM", type_locales, len(TYPES)),
        ("CHRT", bytes(chart), len(chart)),
        ("ABIL", bytes(ability_blob), len(ability_rows)),
        ("ANAM", ability_names, len(ability_rows)),
        ("ALNM", ability_localized_names, len(ability_rows)),
        ("ALOC", ability_localized_descriptions, len(ability_rows)),
    ]
    path = WEB_PACKS / "battle-core.tbattle"
    path.write_bytes(pack(KIND_BATTLE, "battle-core", mechanics_hash, sections))
    item = pack_manifest(path, "battle", "battle-core", ["en-US", "zh-CN"])
    item["label"] = "Battle catalogue"
    manifest.append(item)


def pack_manifest(path: Path, kind: str, pack_id: str, locales: list[str],
                  revision: int = PACK_REVISION) -> dict:
    blob = path.read_bytes()
    return {
        "id": pack_id,
        "kind": kind,
        "abi": PACK_ABI,
        "revision": revision,
        "contentVersion": f"{pack_content_version(blob):08x}",
        "file": path.name,
        "size": len(blob),
        "crc32": f"{binascii.crc32(blob) & 0xFFFFFFFF:08x}",
        "locales": locales,
        "requires": [],
    }


def build_quiz_packs(manifest: list[dict]) -> None:
    source_dir = HERE / "question_banks"
    for source in sorted(source_dir.glob("*.json")):
        document = json.loads(source.read_text(encoding="utf-8"))
        blob, metadata = build_quiz_pack(document)
        path = WEB_PACKS / f"{metadata['id']}.tquiz"
        path.write_bytes(blob)
        item = pack_manifest(path, "quiz", metadata["id"], metadata["locales"],
                             metadata["revision"])
        item["label"] = metadata["label"]
        item["questions"] = metadata["questions"]
        manifest.append(item)


def main() -> int:
    global WEB_PACKS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sprite-dir", type=Path, default=HERE / "sdcard" / "mons",
                        help="directory containing pNNN.bin, psNNN.bin and thumbs.bin sources")
    parser.add_argument("--core-only", action="store_true",
                        help="rebuild battle, move and item packs without regional art")
    parser.add_argument("--allow-empty-art", action="store_true",
                        help="build test packs without copyrighted sprite inputs")
    parser.add_argument("--output-dir", type=Path,
                        help="write generated packs outside web/packs")
    args = parser.parse_args()
    if args.output_dir:
        WEB_PACKS = args.output_dir.resolve()
    WEB_PACKS.mkdir(parents=True, exist_ok=True)
    if args.core_only:
        manifest: list[dict] = []
        build_battle_pack(manifest)
        build_move_pack(manifest)
        build_item_pack(manifest)
        index_path = WEB_PACKS / "index.json"
        if index_path.exists():
            index = json.loads(index_path.read_text(encoding="utf-8"))
            packages = [item for item in index.get("packages", [])
                        if item.get("id") not in {"battle-core", "moves-core", "items-core"}]
            packages.extend(manifest)
            index["packAbi"] = PACK_ABI
            index["packages"] = packages
            index_path.write_text(
                json.dumps(index, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
        print("wrote battle, move and item core packs")
        return 0
    for pattern in ("*.tui", "*.tmove", "*.titem", "*.tbattle", "*.tquiz"):
        for obsolete in WEB_PACKS.glob(pattern):
            obsolete.unlink()
    manifest: list[dict] = []
    build_ui_packs(manifest)
    build_battle_pack(manifest)
    build_move_pack(manifest)
    build_item_pack(manifest)
    build_region_packs(manifest, args.sprite_dir.resolve(), args.allow_empty_art)
    build_quiz_packs(manifest)
    expected_regions = {item["file"] for item in manifest if item["kind"] == "region"}
    for obsolete in WEB_PACKS.glob("*.tregion"):
        if obsolete.name not in expected_regions:
            obsolete.unlink()
    index = {
        "schema": 1,
        "packAbi": PACK_ABI,
        "packages": manifest,
    }
    (WEB_PACKS / "index.json").write_text(
        json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    for item in manifest:
        print(f"{item['file']}: {item['kind']} {item['size'] / 1048576:.2f} MiB")
    print(f"wrote {WEB_PACKS / 'index.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
