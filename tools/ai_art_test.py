#!/usr/bin/env python3
"""Validate the committed AI turnaround catalogue and its TPK2 conversion."""

from collections import Counter

from gen_backs import read_png
from pack_ai_art import (
    ACTION_SOURCE,
    SOURCE,
    TRANSPARENT,
    catalogue_error,
    encode_tpk2,
    expected_sources,
    load_action_frames,
)


def main() -> int:
    expected = expected_sources()
    actual = {path.name for path in SOURCE.glob("*.png")}
    assert not catalogue_error(expected, actual)
    deliberately_incomplete = actual - {next(iter(actual))}
    assert catalogue_error(expected, deliberately_incomplete)

    categories = Counter(name.split("-", 1)[0] for name in expected)
    assert categories == {"base": 48, "mega": 57, "gmax": 32}

    total = 0
    for source_name in sorted(expected):
        path = SOURCE / source_name
        width, height, channels, rows = read_png(str(path))
        assert (width, height, channels) == (128, 128, 4), source_name
        for x, y in ((0, 0), (63, 0), (0, 63), (63, 63),
                     (64, 0), (127, 0), (64, 63), (127, 63),
                     (0, 64), (63, 64), (0, 127), (63, 127),
                     (64, 64), (127, 64), (64, 127), (127, 127)):
            assert rows[y][x * 4 + 3] < 128, f"{source_name}: opaque corner"
        authored = load_action_frames(path)
        blob = encode_tpk2(path)
        expected_actions = 15 if source_name == "base-0668.png" else 12
        assert blob[:4] == b"TPK2" and blob[4] == expected_actions, source_name
        if source_name == "base-0668.png":
            assert set(authored) == {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                     10, 11, 12, 17, 18}
            for action, frames in authored.items():
                assert len(frames) == 4, action
                assert len({tuple(frame) for frame in frames}) == 4, action
                for frame in frames:
                    opaque = sum(pixel != TRANSPARENT for pixel in frame)
                    assert 24 <= opaque < 1800, (action, opaque)
                    assert frame[0] == frame[47] == frame[-48] == frame[-1] == TRANSPARENT
        total += len(blob)

    assert {path.name for path in ACTION_SOURCE.glob("base-0668-*.png")} == {
        "base-0668-front-action.png",
        "base-0668-front-care.png",
        "base-0668-front-motion.png",
        "base-0668-rear.png",
    }

    print(f"PASS  {len(expected)} AI turnarounds: {dict(categories)}, "
          f"{total / 1048576:.1f} MiB encoded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
