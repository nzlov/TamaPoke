#!/usr/bin/env python3
"""Regression checks for generated PMD display scales."""

import struct

from pmd_layout import pmd_display_scale, pmd_idle_bounds, pmd_pair_display_scale


def sprite(width: int, height: int, bounds: tuple[int, int, int, int],
           *more_bounds: tuple[int, int, int, int]) -> bytes:
    frames = []
    for left, top, right, bottom in (bounds,) + more_bounds:
        pixels = bytearray([0xFF] * (width * height))
        for y in range(top, bottom):
            for x in range(left, right):
                pixels[y * width + x] = 0
        frames.append(pixels)
    return (
        b"TPK2" + struct.pack("<BH", 1, 1) + struct.pack("<H", 0)
        + struct.pack("<4B", 0, width, height, len(frames))
        + struct.pack(f"<{len(frames)}H", *([100] * len(frames)))
        + b"".join(frames)
    )


def main() -> None:
    tight = sprite(19, 33, (0, 0, 19, 33))
    gallade_padding = sprite(24, 64, (3, 6, 22, 39))
    assert pmd_display_scale(tight) == 3
    assert pmd_display_scale(gallade_padding) == 3
    assert pmd_pair_display_scale(tight, gallade_padding) == 3

    animated = sprite(64, 64, (20, 10, 30, 20), (5, 15, 45, 40))
    assert pmd_idle_bounds(animated) == (40, 30)

    wide = sprite(80, 20, (0, 0, 80, 20))
    assert pmd_display_scale(wide) == 2

    try:
        pmd_display_scale(b"TPK2" + struct.pack("<BH", 0, 0))
    except ValueError:
        pass
    else:
        raise AssertionError("a TPK2 blob without Idle must be rejected")

    print("all PMD layout checks passed")


if __name__ == "__main__":
    main()
