#include "wild.h"
#include "dex.h"
#include "items.h"

BattleField wildBattleField(uint8_t biome, uint8_t roll) {
  BattleField field;
  if (biome >= 6 || roll < 50) return field;

  static const BattleWeather PRIMARY[6] = {
    BWEATHER_SUN, BWEATHER_RAIN, BWEATHER_RAIN,
    BWEATHER_SUN, BWEATHER_SAND, BWEATHER_SNOW,
  };
  static const BattleWeather SECONDARY[6] = {
    BWEATHER_RAIN, BWEATHER_SUN, BWEATHER_SUN,
    BWEATHER_SAND, BWEATHER_SNOW, BWEATHER_SUN,
  };
  bool thunderAllowed = biome != 3 && biome != 5;
  BattleWeather weather = roll < 80 ? PRIMARY[biome] : SECONDARY[biome];
  BattleTerrain terrain = BTERRAIN_NONE;
  if (thunderAllowed && roll >= 90) {
    weather = BWEATHER_RAIN;
    terrain = BTERRAIN_ELECTRIC;
  }
  battleSetEnvironment(field, weather, terrain);
  return field;
}

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

uint8_t wildFoeEscapeChance(uint16_t hp, uint16_t maxHp, bool angry) {
  if (!maxHp || !hp) return 0;
  if (hp > maxHp) hp = maxHp;
  uint32_t scaledHp = (uint32_t)hp * 100U;
  uint8_t chance = 0;
  if (scaledHp <= (uint32_t)maxHp * 10U) {
    chance = 30;
  } else if (scaledHp <= (uint32_t)maxHp * 40U) {
    uint32_t belowForty = (uint32_t)maxHp * 40U - scaledHp;
    chance = (uint8_t)(10U + belowForty * 20U / ((uint32_t)maxHp * 30U));
  }
  if (angry) chance = (uint8_t)(chance + WILD_ANGRY_ESCAPE_BONUS);
  return chance;
}

static uint8_t rareBonus(uint8_t bonus) {
  return bonus > WILD_RARE_BONUS_MAX ? WILD_RARE_BONUS_MAX : bonus;
}

uint32_t wildRareThreshold(uint8_t bonus) {
  return WILD_RARE_BASE_THRESHOLD +
         (uint32_t)rareBonus(bonus) * WILD_RARE_BONUS_THRESHOLD;
}

bool wildGigantamaxFactorForRoll(SpeciesId species, uint8_t roll) {
  return battleGigantamaxEligible(species) &&
         roll < WILD_GIGANTAMAX_FACTOR_CHANCE;
}

bool wildRareForRoll(uint32_t roll, uint8_t bonus) {
  return roll < wildRareThreshold(bonus);
}

void wildApplyRare(bool rare, uint8_t &ivAtk, uint8_t &ivDef,
                   uint8_t &ivSpe, uint8_t &ivHp) {
  if (!rare) return;
  uint8_t *ivs[] = { &ivAtk, &ivDef, &ivSpe, &ivHp };
  for (uint8_t *iv : ivs) if (*iv < 20) *iv = 20;
}

uint8_t wildCaptureChance(uint8_t rarity, uint16_t hp, uint16_t maxHp,
                          bool hasStatus, int16_t ballModifier) {
  if (!maxHp || !hp) return 0;
  if (ballModifier == ITEM_CATCH_GUARANTEED) return 100;
  if (ballModifier <= 0) return 0;
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
  uint32_t chance = (uint32_t)base * hpFactor * statusFactor *
                    (uint16_t)ballModifier / 1000000U;
  if (!chance) chance = 1;
  return chance > 95 ? 95 : (uint8_t)chance;
}
