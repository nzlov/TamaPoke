#pragma once
#include <stdint.h>

// Capture probability is derived from pack-owned rarity plus live battle
// state. ballMultiplier is a percentage supplied by the selected item.
uint8_t wildCaptureChance(uint8_t rarity, uint16_t hp, uint16_t maxHp,
                          bool hasStatus, uint16_t ballMultiplier);
