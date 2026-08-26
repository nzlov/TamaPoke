#include "wild.h"
#include "dex.h"

uint8_t wildEncounterMaxLevel(uint8_t playerLevel, bool hard) {
  if (hard) return 100;
  uint16_t limit = (uint16_t)playerLevel + 5;
  return limit > 100 ? 100 : (uint8_t)limit;
}

uint8_t wildEscapeChance(uint8_t playerLevel, uint8_t foeLevel) {
  if (!foeLevel || playerLevel >= foeLevel) return 90;
  uint8_t chance = (uint8_t)((uint16_t)90 * playerLevel / foeLevel);
  return chance < 10 ? 10 : chance;
}

uint8_t wildFoeEscapeChance(uint16_t hp, uint16_t maxHp) {
  if (!maxHp || !hp) return 0;
  if (hp > maxHp) hp = maxHp;
  uint32_t scaledHp = (uint32_t)hp * 100U;
  if (scaledHp > (uint32_t)maxHp * 40U) return 0;
  if (scaledHp <= (uint32_t)maxHp * 10U) return 30;
  uint32_t belowForty = (uint32_t)maxHp * 40U - scaledHp;
  return (uint8_t)(10U + belowForty * 20U / ((uint32_t)maxHp * 30U));
}

static uint8_t rareBonus(uint8_t bonus) {
  return bonus > WILD_RARE_BONUS_MAX ? WILD_RARE_BONUS_MAX : bonus;
}

uint8_t wildColorChance(uint8_t bonus) {
  return WILD_COLOR_BASE_CHANCE + rareBonus(bonus);
}

uint8_t wildSparkleChance(uint8_t bonus) {
  return WILD_SPARKLE_BASE_CHANCE + rareBonus(bonus);
}

WildTraits wildTraitsForRolls(uint8_t colorRoll, uint8_t sparkleRoll,
                              uint8_t bonus) {
  WildTraits out;
  out.color = colorRoll < wildColorChance(bonus);
  out.sparkle = sparkleRoll < wildSparkleChance(bonus);
  return out;
}

void wildApplyTraits(const WildTraits &traits, uint8_t &ivAtk, uint8_t &ivDef,
                     uint8_t &ivSpe, uint8_t &ivHp) {
  uint8_t *ivs[] = { &ivAtk, &ivDef, &ivSpe, &ivHp };
  for (uint8_t *iv : ivs) {
    if (traits.color && *iv < 20) *iv = 20;
    if (traits.sparkle) *iv = (uint8_t)(*iv + 10);
  }
}

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
