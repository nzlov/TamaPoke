#pragma once
#include <stdint.h>

#include "dex.h"
#include "nature.h"

enum PetGender : uint8_t {
  GENDER_UNKNOWN = 0,
  GENDER_MALE,
  GENDER_FEMALE,
  GENDER_NONE,
};

constexpr uint8_t GENDER_RATE_NONE = 0xFF;

static inline bool genderValid(PetGender gender) {
  return gender >= GENDER_MALE && gender <= GENDER_NONE;
}

static inline PetGender genderFromRate(uint8_t femaleRate, uint8_t roll) {
  if (femaleRate == GENDER_RATE_NONE) return GENDER_NONE;
  return (roll & 7) < femaleRate ? GENDER_FEMALE : GENDER_MALE;
}

static inline PetGender genderForLegacy(SpeciesId dex, uint8_t femaleRate,
                                        uint8_t ivAtk, uint8_t ivDef,
                                        uint8_t ivSpe, uint8_t ivHp) {
  uint32_t seed = (uint32_t)dex * 2654435761u;
  seed ^= (uint32_t)ivAtk | ((uint32_t)ivDef << 8) |
          ((uint32_t)ivSpe << 16) | ((uint32_t)ivHp << 24);
  seed ^= seed >> 16;
  return genderFromRate(femaleRate, (uint8_t)seed);
}

static inline uint16_t genderStatValue(PetGender gender, NatureStat stat,
                                       uint16_t value) {
  if (gender == GENDER_MALE) {
    if (stat == NATURE_STAT_ATK) return (uint32_t)value * 110 / 100;
    if (stat == NATURE_STAT_SPA) return (uint32_t)value * 90 / 100;
  } else if (gender == GENDER_FEMALE) {
    if (stat == NATURE_STAT_ATK) return (uint32_t)value * 90 / 100;
    if (stat == NATURE_STAT_SPA) return (uint32_t)value * 110 / 100;
  }
  return value;
}
