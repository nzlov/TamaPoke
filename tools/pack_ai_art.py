#!/usr/bin/env python3
"""Convert committed AI sprite sources into the firmware's TPK2 format.

The source image is a 128x128 RGBA PNG with front, left, back and right views
in a 2x2 grid. Optional four-by-four action atlases replace the deterministic
fallback motions with authored frames. Rebuilding packs never calls an image
service.
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path

from gen_backs import read_png


HERE = Path(__file__).resolve().parent
CATALOGUE = HERE / "pokemon_data.json"
SOURCE = HERE / "pokemon_art" / "ai" / "turnarounds"
ACTION_SOURCE = HERE / "pokemon_art" / "ai" / "actions"
OUTPUT = HERE / "sdcard" / "mons"
FRAME = 48
TRANSPARENT = -1

ACTION_ATLASES = {
    "front-care": (0, 3, 4, 5),
    "front-action": (6, 7, 8, 9),
    "front-motion": (10, 11, 1, 2),
    "rear": (12, 17, 18, None),
}

ACTION_DURATIONS = {
    0: [500, 120, 120, 120],
    1: [120, 120, 120, 120],
    2: [120, 120, 120, 120],
    3: [350, 350, 500, 500],
    4: [160, 160, 160, 160],
    5: [80, 120, 180, 220],
    6: [100, 120, 160, 200],
    7: [180, 220, 350, 180],
    8: [100, 140, 140, 120],
    9: [180, 180, 180, 180],
    10: [300, 350, 350, 300],
    11: [180, 180, 500, 500],
    12: [500, 120, 120, 120],
    17: [80, 120, 180, 220],
    18: [100, 120, 160, 200],
}


def rgb565(r: int, g: int, b: int) -> int:
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


def load_views(path: Path) -> list[list[int]]:
    width, height, channels, rows = read_png(str(path))
    if (width, height, channels) != (128, 128, 4):
        raise ValueError(f"{path.name}: expected 128x128 RGBA")
    views = []
    for quadrant_x, quadrant_y in ((0, 0), (1, 0), (0, 1), (1, 1)):
        pixels = []
        for y in range(FRAME):
            source_y = quadrant_y * 64 + y * 64 // FRAME
            row = rows[source_y]
            for x in range(FRAME):
                source_x = quadrant_x * 64 + x * 64 // FRAME
                at = source_x * 4
                r, g, b, alpha = row[at:at + 4]
                pixels.append(rgb565(r, g, b) if alpha >= 128 else TRANSPARENT)
        if sum(pixel != TRANSPARENT for pixel in pixels) < 24:
            raise ValueError(f"{path.name}: empty turnaround quadrant")
        views.append(pixels)
    return views


def _pixel(rows: list[bytes], channels: int, x: int, y: int) -> tuple[int, int, int]:
    at = x * channels
    return tuple(rows[y][at:at + 3])


def _background_mask(rows: list[bytes], channels: int,
                     left: int, top: int, right: int, bottom: int) -> set[tuple[int, int]]:
    """Find the baked low-saturation checkerboard, including enclosed gaps."""
    background = set()
    for y in range(top, bottom):
        for x in range(left, right):
            color = _pixel(rows, channels, x, y)
            if min(color) >= 220 and max(color) - min(color) <= 32:
                background.add((x, y))
    return background


def load_action_atlas(path: Path) -> list[list[int]]:
    width, height, channels, rows = read_png(str(path))
    if channels not in (3, 4) or width < FRAME * 4 or height < FRAME * 4:
        raise ValueError(f"{path.name}: expected a four-by-four RGB/RGBA atlas")

    frames = []
    for row in range(4):
        top, bottom = row * height // 4, (row + 1) * height // 4
        for column in range(4):
            left, right = column * width // 4, (column + 1) * width // 4
            background = _background_mask(rows, channels, left, top, right, bottom)
            frame = []
            for y in range(FRAME):
                source_y = top + (2 * y + 1) * (bottom - top) // (2 * FRAME)
                for x in range(FRAME):
                    source_x = left + (2 * x + 1) * (right - left) // (2 * FRAME)
                    if (source_x, source_y) in background:
                        frame.append(TRANSPARENT)
                        continue
                    r, g, b = _pixel(rows, channels, source_x, source_y)
                    alpha = rows[source_y][source_x * channels + 3] if channels == 4 else 255
                    frame.append(rgb565(r, g, b) if alpha >= 128 else TRANSPARENT)
            if sum(pixel != TRANSPARENT for pixel in frame) < 24:
                raise ValueError(f"{path.name}: empty atlas cell {row + 1},{column + 1}")
            frames.append(frame)
    return frames


def load_action_frames(turnaround: Path) -> dict[int, list[list[int]]]:
    paths = {
        suffix: ACTION_SOURCE / f"{turnaround.stem}-{suffix}.png"
        for suffix in ACTION_ATLASES
    }
    present = {suffix for suffix, path in paths.items() if path.is_file()}
    if not present:
        return {}
    if present != set(paths):
        missing = sorted(set(paths) - present)
        raise ValueError(f"{turnaround.name}: incomplete action atlases: {missing}")

    actions = {}
    for suffix, action_ids in ACTION_ATLASES.items():
        atlas = load_action_atlas(paths[suffix])
        for row, action_id in enumerate(action_ids):
            if action_id is not None:
                actions[action_id] = atlas[row * 4:(row + 1) * 4]
    return actions


def shift(frame: list[int], dx: int = 0, dy: int = 0) -> list[int]:
    out = [TRANSPARENT] * (FRAME * FRAME)
    for y in range(FRAME):
        target_y = y + dy
        if not 0 <= target_y < FRAME:
            continue
        for x in range(FRAME):
            target_x = x + dx
            if 0 <= target_x < FRAME:
                out[target_y * FRAME + target_x] = frame[y * FRAME + x]
    return out


def squash(frame: list[int], numerator: int, denominator: int) -> list[int]:
    target_height = FRAME * numerator // denominator
    out = [TRANSPARENT] * (FRAME * FRAME)
    top = FRAME - target_height
    for y in range(target_height):
        source_y = y * FRAME // target_height
        out[(top + y) * FRAME:(top + y + 1) * FRAME] = \
            frame[source_y * FRAME:(source_y + 1) * FRAME]
    return out


def animation_frames(views: list[list[int]],
                     authored: dict[int, list[list[int]]] | None = None
                     ) -> list[tuple[int, list[list[int]], list[int]]]:
    front, left, back, right = views
    derived = [
        (0, [shift(front), shift(front, dy=-1)], [600, 120]),
        (1, [shift(left), shift(left, -1, 1), shift(left), shift(left, 1, 1)], [120] * 4),
        (2, [shift(right), shift(right, 1, 1), shift(right), shift(right, -1, 1)], [120] * 4),
        (3, [squash(front, 2, 3), shift(squash(front, 2, 3), dx=1)], [700, 700]),
        (5, [shift(front, dx=-2), shift(front, dx=2)], [90, 250]),
        (6, [shift(front), shift(front, dy=-2), shift(front)], [90, 120, 180]),
        (8, [shift(front), shift(front, dy=-5), shift(front, dy=-8), shift(front, dy=-3)], [90] * 4),
        (9, [shift(front), shift(front, dy=1)], [180, 180]),
        (11, [squash(front, 5, 6), shift(squash(front, 5, 6), dy=-1)], [450, 180]),
        (12, [shift(back), shift(back, dy=-1)], [600, 120]),
        (17, [shift(back, dx=-2), shift(back, dx=2)], [90, 250]),
        (18, [shift(back), shift(back, dy=-2), shift(back)], [90, 120, 180]),
    ]
    if not authored:
        return derived
    by_action = {action: (frames, durations) for action, frames, durations in derived}
    for action, frames in authored.items():
        by_action[action] = (frames, ACTION_DURATIONS[action])
    return [(action, *by_action[action]) for action in sorted(by_action)]


def encode_tpk2(path: Path) -> bytes:
    actions = animation_frames(load_views(path), load_action_frames(path))
    counts = Counter(
        pixel for _action, frames, _durations in actions
        for frame in frames for pixel in frame if pixel != TRANSPARENT
    )
    palette = [pixel for pixel, _count in counts.most_common(254)]
    palette_index = {color: index for index, color in enumerate(palette)}
    nearest = {}

    def index(color: int) -> int:
        if color == TRANSPARENT:
            return 0xFF
        direct = palette_index.get(color)
        if direct is not None:
            return direct
        cached = nearest.get(color)
        if cached is not None:
            return cached
        red, green, blue = color >> 11, (color >> 5) & 0x3F, color & 0x1F
        nearest[color] = min(range(len(palette)), key=lambda candidate: (
            (red - (palette[candidate] >> 11)) ** 2 +
            (green - ((palette[candidate] >> 5) & 0x3F)) ** 2 +
            (blue - (palette[candidate] & 0x1F)) ** 2
        ))
        return nearest[color]

    out = bytearray(b"TPK2")
    out.extend(struct.pack("<BH", len(actions), len(palette)))
    out.extend(struct.pack(f"<{len(palette)}H", *palette))
    for action, frames, durations in actions:
        out.extend(struct.pack("<4B", action, FRAME, FRAME, len(frames)))
        out.extend(struct.pack(f"<{len(durations)}H", *durations))
        out.extend(index(pixel) for frame in frames for pixel in frame)
    return bytes(out)


def expected_sources() -> dict[str, str]:
    document = json.loads(CATALOGUE.read_text(encoding="utf-8"))
    expected = {}
    for species in document["species"]:
        number = int(species["id"])
        if not species.get("art", {}).get("normal"):
            expected[f"base-{number:04d}.png"] = f"p{number:03d}.bin"
        for form in species["megaForms"]:
            if not form.get("art", {}).get("normal"):
                name = form.get("form", "standard")
                expected[f"mega-{number:04d}-{name}.png"] = f"pm{number:03d}-{name}.bin"
        if species["gigantamax"]:
            expected[f"gmax-{number:04d}.png"] = f"pg{number:03d}.bin"
    return expected


def catalogue_error(expected: dict[str, str], actual: set[str]) -> str:
    if actual == set(expected):
        return ""
    missing = sorted(set(expected) - actual)
    extra = sorted(actual - set(expected))
    return f"AI art catalogue mismatch; missing={missing}, extra={extra}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    expected = expected_sources()
    actual = {path.name for path in SOURCE.glob("*.png")}
    error = catalogue_error(expected, actual)
    if error:
        raise SystemExit(error)
    args.output.mkdir(parents=True, exist_ok=True)
    total = 0
    for source_name, output_name in sorted(expected.items()):
        blob = encode_tpk2(SOURCE / source_name)
        (args.output / output_name).write_bytes(blob)
        total += len(blob)
    print(f"AI sprites: {len(expected)} TPK2 files, {total / 1048576:.1f} MiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
