#!/usr/bin/env python3
"""Regression checks for player-facing move descriptions."""

from dex_moves import MOVES
from gen_data_packs import move_descriptions


def description(name: str, locale: str) -> str:
    descriptions = move_descriptions(list(MOVES))[locale]
    index = next(i for i, row in enumerate(MOVES, 1) if row[0] == name)
    return descriptions[index]


def main() -> None:
    cases = {
        ("GROWL", "en-US"): "Lowers foe's Attack by 1 stage.",
        ("GROWL", "zh-CN"): "使对手的攻击-1级。",
        ("DRAGON DANCE", "en-US"): "Raises user's Attack and Speed by 1 stage.",
        ("DRAGON DANCE", "zh-CN"): "使自身攻击和速度提高1级。",
        ("RECOVER", "en-US"): "Restores 50% of the user's maximum HP.",
        ("RECOVER", "zh-CN"): "回复使用者最大生命的50%。",
        ("SNARL", "en-US"): "Also lowers foe's Sp. Atk by 1 stage.",
        ("CLOSE COMBAT", "zh-CN"): "同时使自身防御和特防-1级。",
        ("FLAMETHROWER", "zh-CN"): "有10%概率使对手陷入灼伤。",
        ("SUNNY DAY", "en-US"): "Creates sun for 5 turns.",
        ("SNOWSCAPE", "zh-CN"): "使天气变为雪，持续5回合。",
        ("ELECTRIC TERRAIN", "en-US"): "Creates Electric Terrain for 5 turns.",
        ("MISTY TERRAIN", "zh-CN"): "形成薄雾场地，持续5回合。",
        ("THUNDER", "en-US"): "Always hits in rain; accuracy is 50% in harsh sun.",
        ("BLIZZARD", "zh-CN"): "雪天必中。",
        ("SOLAR BEAM", "en-US"): "Attacks immediately in harsh sun; power is halved in other weather.",
        ("EARTHQUAKE", "zh-CN"): "对青草场地上的地面目标威力减半。",
    }
    for (move, locale), expected in cases.items():
        actual = description(move, locale)
        assert expected in actual, f"{move} {locale}: {actual!r}"
        assert "效果" not in actual and "参数" not in actual and "Effect " not in actual
    print(f"PASS: {len(cases)} readable move descriptions")


if __name__ == "__main__":
    main()
