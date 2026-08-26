#!/usr/bin/env python3
"""Build the SD-card UI, regional content and move packs used by the firmware.

Authoring data remains readable Python/JSON under tools/.  Firmware consumes
only the indexed binary packs, and the web installer consumes index.json.
"""

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

KIND_UI = 1
KIND_REGION = 2
KIND_MOVE = 3

sys.path.insert(0, str(HERE))
from pack_format import PACK_ABI, PACK_REVISION, pack  # noqa: E402
from quiz_pack import build_quiz_pack  # noqa: E402
from dex_data import (  # noqa: E402
    DEX, TYPE_ACCENTS, RARE, LEGENDARY, REGIONS, EVOLUTION_BRANCHES,
)
from dex_stats import BASE_STATS  # noqa: E402
from dex_types import TYPES, TYPE_ORDER, CHART  # noqa: E402
from dex_learnsets import LEARNSETS  # noqa: E402
from dex_moves import (  # noqa: E402
    MOVES, AIL_NONE, AIL_PARA, AIL_BURN, AIL_POISON, AIL_SLEEP, AIL_FREEZE,
    AIL_CONFUSE, EF_NONE, EF_STAGE, EF_RECOIL, EF_DRAIN, EF_FIXED_LVL,
    EF_FIXED, EF_PRIORITY, EF_NEVER_MISS, EF_MULTI, EF_HEAL, EF_RECHARGE,
    EF_CHARGE, EF_SET_WEATHER, EF_SET_TERRAIN, BWEATHER_SUN, BWEATHER_RAIN,
    BWEATHER_SAND, BWEATHER_SNOW, BTERRAIN_ELECTRIC, BTERRAIN_GRASSY,
    BTERRAIN_MISTY, BTERRAIN_PSYCHIC, FIELD_MOVE_FLAGS, MF_RAIN_ACCURATE,
    MF_SNOW_ACCURATE, MF_SOLAR_CHARGE, MF_GRASSY_WEAKENED, MC_PHYS, MC_SPEC,
    MC_STATUS, ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE, TG_SELF,
)
from dex_presentation import BIOME_OVERRIDE, TYPE_BIOME, rgb565  # noqa: E402

NAME_LOCALES = json.loads((HERE / "name_locales.json").read_text(encoding="utf-8"))
REGION_LOCALES = json.loads((HERE / "region_locales.json").read_text(encoding="utf-8"))
ITEM_DATA = json.loads((HERE / "item_data.json").read_text(encoding="utf-8"))
SPECIES_DESCRIPTION_DATA = json.loads(
    (HERE / "species_descriptions.json").read_text(encoding="utf-8")
)
SPECIES_DESCRIPTION_LOCALES = sorted(
    SPECIES_DESCRIPTION_DATA.get("species", [{}])[0].get("descriptions", {})
)
MEGA_DATA = json.loads((HERE / "mega_data.json").read_text(encoding="utf-8"))

TYPE_COLORS = [
    0xAD4F, 0xF406, 0x6C9E, 0xFE86, 0x7E4A, 0x9EDB,
    0xC185, 0xA214, 0xE60D, 0xAC9E, 0xFAD1, 0xADC4,
    0xBD07, 0x72D3, 0x71DF, 0x72C9, 0xBDDA, 0xECD5,
]
TYPE_LIGHT = [1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1]
TYPE_ZH = {
    "normal": "一般", "fire": "火", "water": "水", "electric": "电",
    "grass": "草", "ice": "冰", "fighting": "格斗", "poison": "毒",
    "ground": "地面", "flying": "飞行", "psychic": "超能力",
    "bug": "虫", "rock": "岩石", "ghost": "幽灵", "dragon": "龙",
    "dark": "恶", "steel": "钢", "fairy": "妖精",
}


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
            locale["medalLabels"] + locale["medalDescriptions"]
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


def rarity_rows() -> dict[int, int]:
    evolved = {
        target
        for row in DEX
        for target in EVOLUTION_BRANCHES.get(row[0], [row[4]] if row[4] else [])
    }
    result = {}
    for number, *_ in DEX:
        if number in evolved:
            result[number] = 0
        elif number in LEGENDARY:
            result[number] = 3
        elif number in RARE:
            result[number] = 2
        else:
            result[number] = 1
    return result


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


def species_descriptions(rows: list[tuple]) -> dict[str, list[str]]:
    species = SPECIES_DESCRIPTION_DATA.get("species", [])
    if SPECIES_DESCRIPTION_DATA.get("schema") != 2 or not species:
        raise ValueError("unsupported species description catalogue")
    by_dex = {entry["dex"]: entry["descriptions"] for entry in species}
    locales = sorted(species[0]["descriptions"])
    result = {locale: [] for locale in locales}
    for row in rows:
        descriptions = by_dex.get(row[0])
        if descriptions is None or set(descriptions) != set(locales):
            raise ValueError(f"species {row[0]} description catalogue is incomplete")
        for locale in locales:
            value = descriptions[locale]["text"]
            result[locale].append(value if locale == "zh-CN" else bitmap_font_text(value))
    return result


def species_names(rows: list[tuple]) -> dict[str, list[str]]:
    result = {}
    for locale, values in NAME_LOCALES.items():
        catalogue = values["species"]
        if len(catalogue) < max(row[0] for row in DEX):
            raise ValueError(f"{locale}: species name catalogue is incomplete")
        result[locale] = [catalogue[row[0] - 1] for row in rows]
    return result


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
    item["requires"] = ["moves-core"] + [
        f"region-{name}" for name in sorted(required_regions)
        if name != region_name.lower()
    ]
    manifest.append(item)


def build_region_packs(manifest: list[dict], sprite_dir: Path) -> None:
    region_battles = json.loads((HERE / "region_data.json").read_text(encoding="utf-8"))
    battle_by_name = {row["name"].upper(): row for row in region_battles["regions"]}
    rarities = rarity_rows()
    type_ids = {name: index for index, name in enumerate(TYPE_ORDER)}
    spec_record = struct.Struct("<HBBH10BI")
    evolution_record = struct.Struct("<HH")
    sprite_record = struct.Struct("<HIIII")
    trainer_record = struct.Struct("<BBBBII" + "HB" * 6)
    badge_record = struct.Struct("<BBBBII")
    for region_id, (region_name, lo, hi, starters) in enumerate(REGIONS):
        battle = battle_by_name[region_name]
        path = WEB_PACKS / f"region-{region_name.lower()}.tregion"
        rows = [row for row in DEX if lo <= row[0] <= hi]
        normal_sources = [sprite_dir / f"p{row[0]:03d}.bin" for row in rows]
        normal_count = sum(source.exists() for source in normal_sources)
        if normal_count == 0:
            if not path.exists():
                raise FileNotFoundError(
                    f"{sprite_dir} has no regional sprite sources and there is no existing "
                    f"{path.name}; run pack_pmd.py and make_thumbs.py first"
                )
            append_region_manifest(manifest, path, region_name, lo, hi, battle)
            continue
        if normal_count != len(rows):
            print(f"{region_name}: packing {normal_count}/{len(rows)} species with art")
        thumbs_path = sprite_dir / "thumbs.bin"
        if not thumbs_path.exists():
            raise FileNotFoundError(f"{thumbs_path} is required to rebuild regional packs")
        names_blob, name_offsets = string_pool([row[2] for row in rows])
        specs = bytearray()
        evolutions = bytearray()
        for row, name_offset in zip(rows, name_offsets):
            number, _slug, _display, accent_type, evolves_to, evolve_level = row
            hp, atk, defense, speed, spa, spd = BASE_STATS[number]
            t1, t2 = TYPES[number]
            specs.extend(spec_record.pack(
                number, evolve_level, rarities[number],
                rgb565(TYPE_ACCENTS[accent_type]),
                hp, atk, defense, speed, spa, spd,
                BIOME_OVERRIDE.get(number, TYPE_BIOME[accent_type]),
                type_ids[t1], type_ids[t2] if t2 else 255, region_id,
                name_offset,
            ))
            targets = EVOLUTION_BRANCHES.get(number, [evolves_to] if evolves_to else [])
            if len(targets) > 8:
                raise ValueError(f"species {number}: more than 8 evolution targets")
            for target in targets:
                evolutions.extend(evolution_record.pack(number, target))

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
        for number, *_ in rows:
            normal_path = sprite_dir / f"p{number:03d}.bin"
            normal = normal_path.read_bytes() if normal_path.exists() else b""
            shiny_path = sprite_dir / f"ps{number:03d}.bin"
            shiny = shiny_path.read_bytes() if normal and shiny_path.exists() else b""
            normal_at = len(sprites)
            sprites.extend(normal)
            shiny_at = len(sprites)
            sprites.extend(shiny)
            sprite_index.extend(sprite_record.pack(
                number, normal_at, len(normal), shiny_at, len(shiny),
            ))
        thumbs = thumbs_path.read_bytes()
        locales = localized_strings(species_descriptions(rows), len(rows))
        localized_names = localized_strings(species_names(rows), len(rows))
        localized_regional_names = localized_strings(
            regional_names(region_name, battle), 1 + len(battle["trainers"]) * 2,
        )
        mechanics_hash = binascii.crc32(
            specs + evolutions + region + battle_meta + trainers
        ) & 0xFFFFFFFF
        blob = pack(KIND_REGION, f"region-{region_name.lower()}", mechanics_hash, [
            ("SPEC", bytes(specs), len(rows)),
            ("EVOS", bytes(evolutions), len(evolutions) // evolution_record.size),
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
        ])
        path.write_bytes(blob)
        append_region_manifest(manifest, path, region_name, lo, hi, battle)


def unpack_move(row: tuple) -> tuple:
    return tuple(row) + ((AIL_NONE, 0) if len(row) == 11 else ())


STAT_LABELS = {
    "en-US": ((ST_ATK, "Attack"), (ST_DEF, "Defense"), (ST_SPA, "Sp. Atk"),
              (ST_SPD, "Sp. Def"), (ST_SPE, "Speed")),
    "zh-CN": ((ST_ATK, "攻击"), (ST_DEF, "防御"), (ST_SPA, "特攻"),
              (ST_SPD, "特防"), (ST_SPE, "速度")),
}

AILMENT_LABELS = {
    "en-US": {AIL_PARA: "paralysis", AIL_BURN: "a burn", AIL_POISON: "poison",
              AIL_SLEEP: "sleep", AIL_FREEZE: "freezing", AIL_CONFUSE: "confusion"},
    "zh-CN": {AIL_PARA: "麻痹", AIL_BURN: "灼伤", AIL_POISON: "中毒",
              AIL_SLEEP: "睡眠", AIL_FREEZE: "冰冻", AIL_CONFUSE: "混乱"},
}


def stat_names(mask: int, locale: str) -> str:
    names = [name for bit, name in STAT_LABELS[locale] if mask & bit]
    return (" and " if locale == "en-US" else "和").join(names)


def move_effect_text(category: int, effect: int, param: int, mask: int,
                     stages: int, target: int, locale: str) -> str:
    if effect == EF_NONE or effect == EF_NEVER_MISS:
        return ""
    if effect == EF_STAGE:
        stats = stat_names(mask, locale)
        if locale == "en-US":
            owner = "user's" if target == TG_SELF else "foe's"
            verb = "Raises" if stages > 0 else "Lowers"
            prefix = "Also " if category != MC_STATUS else ""
            return f"{prefix}{verb.lower() if prefix else verb} {owner} {stats} by {abs(stages)} stage{'s' if abs(stages) != 1 else ''}."
        owner = "自身" if target == TG_SELF else "对手的"
        prefix = "同时" if category != MC_STATUS else ""
        if stages > 0:
            return f"{prefix}使{owner}{stats}提高{stages}级。"
        return f"{prefix}使{owner}{stats}-{abs(stages)}级。"
    if effect == EF_SET_WEATHER:
        names = {
            "en-US": {BWEATHER_SUN: "sun", BWEATHER_RAIN: "rain",
                      BWEATHER_SAND: "a sandstorm", BWEATHER_SNOW: "snow"},
            "zh-CN": {BWEATHER_SUN: "晴天", BWEATHER_RAIN: "雨天",
                      BWEATHER_SAND: "沙暴", BWEATHER_SNOW: "雪"},
        }
        return (f"Creates {names[locale][param]} for 5 turns."
                if locale == "en-US" else f"使天气变为{names[locale][param]}，持续5回合。")
    if effect == EF_SET_TERRAIN:
        names = {
            "en-US": {BTERRAIN_ELECTRIC: "Electric Terrain",
                      BTERRAIN_GRASSY: "Grassy Terrain",
                      BTERRAIN_MISTY: "Misty Terrain",
                      BTERRAIN_PSYCHIC: "Psychic Terrain"},
            "zh-CN": {BTERRAIN_ELECTRIC: "电气场地", BTERRAIN_GRASSY: "青草场地",
                      BTERRAIN_MISTY: "薄雾场地", BTERRAIN_PSYCHIC: "精神场地"},
        }
        return (f"Creates {names[locale][param]} for 5 turns."
                if locale == "en-US" else f"形成{names[locale][param]}，持续5回合。")
    if locale == "en-US":
        return {
            EF_RECOIL: f"User takes 1/{param} of damage dealt as recoil.",
            EF_DRAIN: f"Restores HP equal to {param}% of damage dealt.",
            EF_FIXED_LVL: "Damage equals the user's level.",
            EF_FIXED: f"Deals exactly {param} damage.",
            EF_PRIORITY: "Usually moves first.",
            EF_MULTI: "Hits 2-5 times.",
            EF_HEAL: f"Restores {param}% of the user's maximum HP.",
            EF_RECHARGE: "User must recharge on the next turn.",
            EF_CHARGE: "Charges on the first turn and attacks on the next.",
        }[effect]
    return {
        EF_RECOIL: f"使用者的生命也会变少，数值为伤害的1/{param}。",
        EF_DRAIN: f"回复造成伤害{param}%的生命。",
        EF_FIXED_LVL: "造成等同于使用者等级的伤害。",
        EF_FIXED: f"造成{param}点伤害。",
        EF_PRIORITY: "通常会提前行动。",
        EF_MULTI: "连续攻击2-5次。",
        EF_HEAL: f"回复使用者最大生命的{param}%。",
        EF_RECHARGE: "下一回合无法行动。",
        EF_CHARGE: "首回合准备，下一回合攻击。",
    }[effect]


def move_descriptions(rows: list[tuple]) -> dict[str, list[str]]:
    category_en = {MC_PHYS: "Physical", MC_SPEC: "Special", MC_STATUS: "Status"}
    category_zh = {MC_PHYS: "物理", MC_SPEC: "特殊", MC_STATUS: "变化"}
    english = ["Empty move slot."]
    chinese = ["空招式槽。"]
    for row in rows:
        _name, _slug, typ, category, power, accuracy, effect, param, mask, stages, target, ailment, chance = unpack_move(row)
        accuracy_text = "never misses" if accuracy == 0 else f"accuracy {accuracy}%"
        en = f"{typ.title()} {category_en[category]} move; power {power}, {accuracy_text}."
        zh_acc = "必中" if accuracy == 0 else f"命中{accuracy}%"
        zh = f"{TYPE_ZH[typ]}属性{category_zh[category]}招式；威力{power}，{zh_acc}。"
        if category == MC_STATUS:
            en = f"{typ.title()} status move; {accuracy_text}."
            zh = f"{TYPE_ZH[typ]}属性变化招式；{zh_acc}。"
        en_effect = move_effect_text(category, effect, param, mask, stages, target, "en-US")
        zh_effect = move_effect_text(category, effect, param, mask, stages, target, "zh-CN")
        if en_effect:
            en += f" {en_effect}"
            zh += f" {zh_effect}"
        field_flag = FIELD_MOVE_FLAGS.get(_slug, 0)
        if field_flag & MF_RAIN_ACCURATE:
            en += " Always hits in rain; accuracy is 50% in harsh sun."
            zh += " 雨天必中；晴天命中率为50%。"
        if field_flag & MF_SNOW_ACCURATE:
            en += " Always hits in snow."
            zh += " 雪天必中。"
        if field_flag & MF_SOLAR_CHARGE:
            en += " Attacks immediately in harsh sun; power is halved in other weather."
            zh += " 晴天立即攻击；其他天气下威力减半。"
        if field_flag & MF_GRASSY_WEAKENED:
            en += " Power is halved against grounded targets on Grassy Terrain."
            zh += " 对青草场地上的地面目标威力减半。"
        if ailment:
            en += f" Has a {chance}% chance to cause {AILMENT_LABELS['en-US'][ailment]}."
            zh += f" 有{chance}%概率使对手陷入{AILMENT_LABELS['zh-CN'][ailment]}。"
        english.append(en)
        chinese.append(zh)
    return {"en-US": english, "zh-CN": chinese}


def move_names() -> dict[str, list[str]]:
    result = {}
    for locale, values in NAME_LOCALES.items():
        names = values["moves"]
        if len(names) != len(MOVES):
            raise ValueError(f"{locale}: expected {len(MOVES)} move names, got {len(names)}")
        result[locale] = ["-"] + names
    return result


def localized_type_names() -> dict[str, list[str]]:
    return {"zh-CN": [TYPE_ZH[name] for name in TYPE_ORDER]}


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
        NAME_LOCALES,
        REGION_LOCALES,
        species_descriptions(DEX),
        move_descriptions(list(MOVES)),
        localized_type_names(),
        ITEM_DATA,
    ]
    return set(range(32, 127)) | {
        ord(char)
        for source in sources
        for value in text_values(source)
        for char in value
        if ord(char) > 127
    }


def build_move_pack(manifest: list[dict], sprite_dir: Path) -> None:
    type_ids = {name: index for index, name in enumerate(TYPE_ORDER)}
    move_rows = [("-", None, "normal", MC_STATUS, 0, 0, 0, 0, 0, 0, 0)] + list(MOVES)
    names_blob, name_offsets = string_pool([row[0] for row in move_rows])
    move_record = struct.Struct("<HBBBBBbBbBBBI")
    move_blob = bytearray()
    field_flags = bytearray()
    for move_id, (row, name_offset) in enumerate(zip(move_rows, name_offsets)):
        name, _slug, typ, category, power, accuracy, effect, param, mask, stages, target, ailment, chance = unpack_move(row)
        move_blob.extend(move_record.pack(
            move_id, type_ids[typ], category, power, accuracy, effect, param,
            mask, stages, target, ailment, chance, name_offset,
        ))
        field_flags.append(FIELD_MOVE_FLAGS.get(_slug, 0))

    by_slug = {slug: index + 1 for index, (_name, slug, *_rest) in enumerate(MOVES) if slug}
    offsets = [0]
    learn = bytearray()
    for species_id in range(0, len(DEX) + 1):
        for slug, level in (LEARNSETS.get(species_id, []) if species_id else []):
            learn.extend(struct.pack("<HBB", by_slug[slug], min(level, 255), 1 if level == 0 else 0))
        offsets.append(len(learn) // 4)
    offset_blob = struct.pack(f"<{len(offsets)}I", *offsets)

    chart = bytearray()
    for attack in TYPE_ORDER:
        for defense in TYPE_ORDER:
            chart.append(int(CHART.get(attack, {}).get(defense, 1) * 10))
    type_names, type_name_offsets = string_pool([name.upper() for name in TYPE_ORDER])
    type_blob = bytearray()
    for name_offset, color, light in zip(type_name_offsets, TYPE_COLORS, TYPE_LIGHT):
        type_blob.extend(struct.pack("<IHBB", name_offset, color, light, 0))
    locales = localized_strings(move_descriptions(list(MOVES)), len(move_rows))
    localized_names = localized_strings(move_names(), len(move_rows))
    type_locales = localized_strings(localized_type_names(), len(TYPE_ORDER))

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

    mega_record = struct.Struct("<HBBBBBBB")
    mega_blob = bytearray()
    mega_sprite_record = struct.Struct("<HII")
    mega_sprite_index = bytearray()
    mega_sprites = bytearray()
    previous_species = 0
    for form in MEGA_DATA:
        species = int(form["species"])
        types = form["types"]
        stats = form["stats"]
        if species <= previous_species or species > 0xFFFF:
            raise ValueError("mega species IDs must be unique and sorted")
        if not 1 <= len(types) <= 2 or any(name not in type_ids for name in types):
            raise ValueError(f"invalid mega types for species {species}")
        if len(stats) != 5 or any(not 0 < int(value) <= 255 for value in stats):
            raise ValueError(f"invalid mega stats for species {species}")
        mega_blob.extend(mega_record.pack(
            species, type_ids[types[0]], type_ids[types[1]] if len(types) == 2 else 255,
            *(int(value) for value in stats),
        ))
        sprite_path = sprite_dir / f"pm{species:03d}.bin"
        if form.get("spritePath") and sprite_path.exists():
            sprite = sprite_path.read_bytes()
            sprite_at = len(mega_sprites)
            mega_sprites.extend(sprite)
            mega_sprite_index.extend(mega_sprite_record.pack(
                species, sprite_at, len(sprite),
            ))
        previous_species = species

    mechanics_hash = binascii.crc32(
        move_blob + field_flags + learn + offset_blob + chart + item_blob + mega_blob
    ) & 0xFFFFFFFF
    sections = [
        ("MOVE", bytes(move_blob), len(move_rows)),
        ("MFLG", bytes(field_flags), len(move_rows)),
        ("NAME", names_blob, len(move_rows)),
        ("LNAM", localized_names, len(move_rows)),
        ("LOFS", offset_blob, len(offsets)),
        ("LERN", bytes(learn), len(learn) // 4),
        ("TYPS", bytes(type_blob), len(TYPE_ORDER)),
        ("TSTR", type_names, len(TYPE_ORDER)),
        ("TLNM", type_locales, len(TYPE_ORDER)),
        ("CHRT", bytes(chart), len(chart)),
        ("LOCL", locales, len(move_rows)),
        ("ITEM", bytes(item_blob), len(ITEM_DATA)),
        ("INAM", item_names, len(ITEM_DATA)),
        ("ILNM", item_localized_names, len(ITEM_DATA)),
        ("ILOC", item_localized_descriptions, len(ITEM_DATA)),
        ("MEGA", bytes(mega_blob), len(MEGA_DATA)),
    ]
    if mega_sprite_index:
        sections.extend([
            ("MSPI", bytes(mega_sprite_index),
             len(mega_sprite_index) // mega_sprite_record.size),
            ("MSBL", bytes(mega_sprites),
             len(mega_sprite_index) // mega_sprite_record.size),
        ])
    blob = pack(KIND_MOVE, "moves-core", mechanics_hash, sections)
    path = WEB_PACKS / "moves-core.tmove"
    path.write_bytes(blob)
    item = pack_manifest(path, "move", "moves-core", ["en-US", "zh-CN"])
    item["label"] = "Core moves"
    manifest.append(item)


def pack_manifest(path: Path, kind: str, pack_id: str, locales: list[str],
                  revision: int = PACK_REVISION) -> dict:
    blob = path.read_bytes()
    return {
        "id": pack_id,
        "kind": kind,
        "abi": PACK_ABI,
        "revision": revision,
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sprite-dir", type=Path, default=HERE / "sdcard" / "mons",
                        help="directory containing pNNN.bin, psNNN.bin and thumbs.bin sources")
    parser.add_argument("--move-only", action="store_true",
                        help="rebuild only moves-core.tmove without requiring regional art")
    args = parser.parse_args()
    WEB_PACKS.mkdir(parents=True, exist_ok=True)
    if args.move_only:
        manifest: list[dict] = []
        build_move_pack(manifest, args.sprite_dir.resolve())
        index_path = WEB_PACKS / "index.json"
        if index_path.exists():
            index = json.loads(index_path.read_text(encoding="utf-8"))
            packages = [item for item in index.get("packages", [])
                        if item.get("id") != "moves-core"]
            packages.extend(manifest)
            index["packAbi"] = PACK_ABI
            index["packages"] = packages
            index_path.write_text(
                json.dumps(index, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
        print(f"wrote {WEB_PACKS / 'moves-core.tmove'}")
        return 0
    for pattern in ("*.tui", "*.tmove", "*.tpet", "*.tquiz"):
        for obsolete in WEB_PACKS.glob(pattern):
            obsolete.unlink()
    manifest: list[dict] = []
    build_ui_packs(manifest)
    build_move_pack(manifest, args.sprite_dir.resolve())
    build_region_packs(manifest, args.sprite_dir.resolve())
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
