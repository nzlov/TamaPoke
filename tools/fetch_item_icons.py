#!/usr/bin/env python3
"""Download selected PokeAPI item sprites and convert them for items-core.

The downloaded PNGs and converted TIC1 files stay in the ignored local cache.
They are official Pokemon artwork; see CREDITS.md before redistributing them.
"""

from __future__ import annotations

import json
import shutil
import struct
import subprocess
import urllib.request
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None


HERE = Path(__file__).resolve().parent
CACHE = HERE / "item_icon_cache"
ITEMS = json.loads((HERE / "item_data.json").read_text(encoding="utf-8"))["items"]
POKEAPI_SPRITES_REVISION = "c10459b9b0129eaca5c5d9b1cac65336debb1d08"
RAW_BASE = (
    "https://raw.githubusercontent.com/PokeAPI/sprites/"
    f"{POKEAPI_SPRITES_REVISION}/sprites/items"
)


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def rgba_pixels(source: Path) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    if Image is not None:
        image = Image.open(source).convert("RGBA")
        return image.width, image.height, list(image.getdata())
    if not shutil.which("ffprobe") or not shutil.which("ffmpeg"):
        raise SystemExit("Pillow or ffmpeg is required to convert item icons")
    dimensions = subprocess.run([
        "ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height", "-of", "csv=p=0:s=x", str(source),
    ], check=True, capture_output=True, text=True).stdout.strip()
    width, height = (int(value) for value in dimensions.split("x", 1))
    raw = subprocess.run([
        "ffmpeg", "-v", "error", "-i", str(source), "-f", "rawvideo",
        "-pix_fmt", "rgba", "-",
    ], check=True, capture_output=True).stdout
    if len(raw) != width * height * 4:
        raise ValueError(f"{source.name}: unexpected decoded RGBA size")
    return width, height, [tuple(raw[index:index + 4])
                           for index in range(0, len(raw), 4)]


def convert(source: Path, target: Path) -> None:
    width, height, rgba = rgba_pixels(source)
    if not 1 <= width <= 32 or not 1 <= height <= 32:
        raise ValueError(f"{source.name}: expected at most 32x32 pixels, got {width}x{height}")

    palette: list[int] = []
    palette_index: dict[int, int] = {}
    pixels = bytearray()
    for red, green, blue, alpha in rgba:
        if alpha < 128:
            pixels.append(0xFF)
            continue
        color = rgb565(red, green, blue)
        index = palette_index.get(color)
        if index is None:
            if len(palette) >= 255:
                raise ValueError(f"{source.name}: more than 255 opaque RGB565 colors")
            index = len(palette)
            palette_index[color] = index
            palette.append(color)
        pixels.append(index)
    if not palette:
        raise ValueError(f"{source.name}: icon has no opaque pixels")

    blob = bytearray(struct.pack("<4sBBBB", b"TIC1", width, height, len(palette), 0))
    blob.extend(struct.pack(f"<{len(palette)}H", *palette))
    blob.extend(pixels)
    target.write_bytes(blob)


def main() -> int:
    CACHE.mkdir(parents=True, exist_ok=True)
    icons = sorted({item["icon"] for item in ITEMS if item.get("icon")})
    for slug in icons:
        png = CACHE / f"{slug}.png"
        packed = CACHE / f"{slug}.ticon"
        if not png.exists():
            url = f"{RAW_BASE}/{slug}.png"
            print(f"download {url}")
            urllib.request.urlretrieve(url, png)
        convert(png, packed)
        print(f"wrote {packed.relative_to(HERE)}")
    print(f"packed {len(icons)} item icons from PokeAPI/sprites {POKEAPI_SPRITES_REVISION}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
