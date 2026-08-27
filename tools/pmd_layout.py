"""Build-time PMD sprite display-scale calculation."""

import struct


# Fit the opaque union of Idle frames into this box before per-screen caps.
TARGET_WIDTH = 140
TARGET_HEIGHT = 120
MIN_SCALE = 2
MAX_SCALE = 6


def pmd_idle_bounds(blob: bytes) -> tuple[int, int]:
    if len(blob) < 7 or blob[:4] != b"TPK2":
        raise ValueError("invalid TPK2 header")
    action_count = blob[4]
    palette_count = struct.unpack_from("<H", blob, 5)[0]
    offset = 7 + palette_count * 2
    if offset > len(blob):
        raise ValueError("truncated TPK2 palette")

    idle_bounds = None
    for _ in range(action_count):
        if offset + 4 > len(blob):
            raise ValueError("truncated TPK2 action header")
        action, width, height, frames = struct.unpack_from("<4B", blob, offset)
        offset += 4
        data_size = width * height * frames
        offset += frames * 2
        if not width or not height or not frames or offset + data_size > len(blob):
            raise ValueError("invalid TPK2 action payload")
        if action == 0:
            left, top, right, bottom = width, height, -1, -1
            frame_size = width * height
            for index, color in enumerate(blob[offset:offset + data_size]):
                if color == 0xFF:
                    continue
                pixel = index % frame_size
                x, y = pixel % width, pixel // width
                left, top = min(left, x), min(top, y)
                right, bottom = max(right, x), max(bottom, y)
            if right < left or bottom < top:
                raise ValueError("empty TPK2 Idle action")
            idle_bounds = right - left + 1, bottom - top + 1
        offset += data_size

    if idle_bounds is None:
        raise ValueError("TPK2 sprite has no Idle action")
    return idle_bounds


def pmd_display_scale(blob: bytes) -> int:
    width, height = pmd_idle_bounds(blob)
    scale = min(TARGET_WIDTH // width, TARGET_HEIGHT // height)
    return max(MIN_SCALE, min(MAX_SCALE, scale))


def pmd_pair_display_scale(normal: bytes, shiny: bytes = b"") -> int:
    if not normal:
        return 0
    scale = pmd_display_scale(normal)
    return min(scale, pmd_display_scale(shiny)) if shiny else scale
