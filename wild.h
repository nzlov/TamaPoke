#pragma once
#include <stdint.h>
#include "battle.h"

// One roll uses a common denominator so 100 / 409600 is exactly 1 / 4096 and
// every 4096 added by the blessing bonus is exactly one percentage point.
constexpr uint32_t WILD_RARE_ROLL_SCALE = 409600;
constexpr uint32_t WILD_RARE_BASE_THRESHOLD = 100;
constexpr uint32_t WILD_RARE_BONUS_THRESHOLD = 4096;
constexpr uint8_t WILD_GIGANTAMAX_FACTOR_CHANCE = 5;
constexpr uint8_t WILD_HIDDEN_ABILITY_CHANCE = 5;
constexpr uint8_t WILD_BONUS_DROP_CHANCE = 30;
constexpr uint8_t WILD_RARE_BONUS_MAX = 15;
constexpr uint8_t WILD_ANGRY_ESCAPE_BONUS = 5;
constexpr uint8_t WILD_MAX_LEVEL = 120;

uint32_t wildRareThreshold(uint8_t bonus);
bool wildRareForRoll(uint32_t roll, uint8_t bonus);
void wildApplyRare(bool rare, uint8_t &ivAtk, uint8_t &ivDef,
                   uint8_t &ivSpe, uint8_t &ivHp);
bool wildGigantamaxFactorForRoll(SpeciesId species, uint8_t roll);
AbilitySlot wildAbilitySlotForRoll(SpeciesId species, bool hard, uint8_t roll,
                                   uint32_t normalRoll);

// Capture probability is derived from pack-owned rarity plus live battle
// state. ballModifier is a percentage supplied by the selected item, or the
// generic guaranteed-catch sentinel defined in items.h.
uint8_t wildCaptureChance(uint8_t rarity, uint16_t hp, uint16_t maxHp,
                          bool hasStatus, int16_t ballModifier);

// Encounters range from level 1 through ten above the party's highest level
// on normal difficulty, or twenty above it on hard, capped at level 120.
uint8_t wildEncounterMaxLevel(const Combatant party[], uint8_t partyCount,
                              bool hard);

// Normal grants one weighted item and hard grants two. Both difficulties make
// one independent roll for a single bonus item.
uint8_t wildWeightedDropCount(bool hard, uint8_t bonusRoll);

// Escape starts at 90%. A lower-level creature scales that chance by its level
// ratio against the opponent, with a 10% floor.
uint8_t wildEscapeChance(uint8_t playerLevel, uint8_t foeLevel);

// A wild foe starts considering escape at 40% HP (10%), rises linearly to
// 30% at 10% HP, and stays capped there below that threshold. Anger adds five
// percentage points, including above the normal HP threshold.
uint8_t wildFoeEscapeChance(uint16_t hp, uint16_t maxHp, bool angry = false);

// Deterministic input keeps the biome weather table testable at every boundary.
BattleField wildBattleField(uint8_t biome, uint8_t roll);
