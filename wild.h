#pragma once
#include <stdint.h>

// Capture probability is derived from pack-owned rarity plus live battle
// state. ballMultiplier is a percentage supplied by the selected item.
uint8_t wildCaptureChance(uint8_t rarity, uint16_t hp, uint16_t maxHp,
                          bool hasStatus, uint16_t ballMultiplier);

// Normal encounters can range from level 1 through five above the player;
// hard encounters may use the full level range.
uint8_t wildEncounterMaxLevel(uint8_t playerLevel, bool hard);

// Escape starts at 90%. A lower-level creature scales that chance by its level
// ratio against the opponent, with a 10% floor.
uint8_t wildEscapeChance(uint8_t playerLevel, uint8_t foeLevel);

// A wild foe starts considering escape at 40% HP (10%), rises linearly to
// 30% at 10% HP, and stays capped there below that threshold.
uint8_t wildFoeEscapeChance(uint16_t hp, uint16_t maxHp);
