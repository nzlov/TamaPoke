#pragma once
#include <stdint.h>
#include "battle.h"

constexpr uint8_t WILD_COLOR_BASE_CHANCE = 5;
constexpr uint8_t WILD_SPARKLE_BASE_CHANCE = 1;
constexpr uint8_t WILD_GIGANTAMAX_FACTOR_CHANCE = 5;
constexpr uint8_t WILD_RARE_BONUS_MAX = 15;
constexpr uint8_t WILD_ANGRY_ESCAPE_BONUS = 5;

struct WildTraits {
  bool color = false;
  bool sparkle = false;
};

uint8_t wildColorChance(uint8_t bonus);
uint8_t wildSparkleChance(uint8_t bonus);
// The two rolls are separate inputs on purpose: color and sparkle are
// independent traits, not categories cut from one random number.
WildTraits wildTraitsForRolls(uint8_t colorRoll, uint8_t sparkleRoll,
                              uint8_t bonus);
void wildApplyTraits(const WildTraits &traits, uint8_t &ivAtk, uint8_t &ivDef,
                     uint8_t &ivSpe, uint8_t &ivHp);
bool wildGigantamaxFactorForRoll(SpeciesId species, uint8_t roll);

// Capture probability is derived from pack-owned rarity plus live battle
// state. ballModifier is a percentage supplied by the selected item, or the
// generic guaranteed-catch sentinel defined in items.h.
uint8_t wildCaptureChance(uint8_t rarity, uint16_t hp, uint16_t maxHp,
                          bool hasStatus, int16_t ballModifier);

// Normal encounters can range from level 1 through five above the player;
// hard encounters may use the full level range.
uint8_t wildEncounterMaxLevel(uint8_t playerLevel, bool hard);

// Escape starts at 90%. A lower-level creature scales that chance by its level
// ratio against the opponent, with a 10% floor.
uint8_t wildEscapeChance(uint8_t playerLevel, uint8_t foeLevel);

// A wild foe starts considering escape at 40% HP (10%), rises linearly to
// 30% at 10% HP, and stays capped there below that threshold. Anger adds five
// percentage points, including above the normal HP threshold.
uint8_t wildFoeEscapeChance(uint16_t hp, uint16_t maxHp, bool angry = false);

// Deterministic input keeps the biome weather table testable at every boundary.
BattleField wildBattleField(uint8_t biome, uint8_t roll);
