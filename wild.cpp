#include "wild.h"
#include "dex.h"

uint8_t wildCaptureChance(uint8_t rarity, uint16_t hp, uint16_t maxHp,
                          bool hasStatus, uint16_t ballMultiplier) {
  if (!maxHp || !ballMultiplier) return 0;
  if (hp > maxHp) hp = maxHp;
  uint8_t base;
  switch (rarity) {
    case R_LEGENDARIO: base = 5; break;
    case R_RARO: base = 18; break;
    case R_EVO: base = 24; break;
    default: base = 30; break;
  }
  uint16_t hpFactor = (uint16_t)(100U + (uint32_t)(maxHp - hp) * 80U / maxHp);
  uint16_t statusFactor = hasStatus ? 130 : 100;
  uint32_t chance = (uint32_t)base * hpFactor * statusFactor * ballMultiplier / 1000000U;
  if (!chance) chance = 1;
  return chance > 95 ? 95 : (uint8_t)chance;
}
