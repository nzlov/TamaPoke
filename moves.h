#pragma once
#include <stdint.h>
#include "dex.h"

enum : uint8_t { MC_PHYS = 0, MC_SPEC = 1, MC_STATUS = 2 };
enum : uint8_t {
  EF_NONE = 0, EF_STAGE, EF_RECOIL, EF_DRAIN, EF_FIXED_LVL, EF_FIXED,
  EF_PRIORITY, EF_NEVER_MISS, EF_MULTI, EF_HEAL, EF_RECHARGE, EF_CHARGE,
  EF_PROTECT, EF_SET_WEATHER, EF_SET_TERRAIN,
};
enum : uint8_t {
  MF_NONE = 0,
  MF_RAIN_ACCURATE = 1,
  MF_SNOW_ACCURATE = 2,
  MF_SOLAR_CHARGE = 4,
  MF_GRASSY_WEAKENED = 8,
};
enum : uint8_t { ST_ATK = 1, ST_DEF = 2, ST_SPA = 4, ST_SPD = 8, ST_SPE = 16 };
enum : uint8_t { TG_SELF = 0, TG_FOE = 1 };
enum : uint8_t {
  AIL_NONE = 0, AIL_PARA = 1, AIL_BURN = 2, AIL_POISON = 3,
  AIL_SLEEP = 4, AIL_FREEZE = 5, AIL_CONFUSE = 6,
};

using MoveId = uint16_t;
constexpr MoveId MOVE_NONE = 0;
constexpr MoveId CONTENT_MAX_MOVES = 2048;

struct MoveEntry {
  const char *name;
  uint8_t type, cat, power, acc, effect;
  int8_t param;
  uint8_t statMask;
  int8_t stages;
  uint8_t target, ailment, ailChance;
  uint8_t fieldFlags;
};

enum LearnMethod : uint8_t { LM_LEVEL_UP = 0, LM_TM = 1, LM_TUTOR = 2, LM_EGG = 3 };
struct LearnEntry { MoveId move; uint8_t level; uint8_t method; };

uint16_t moveCount();
bool moveValid(MoveId id);
const MoveEntry &moveEntry(MoveId id);
uint16_t learnCount(SpeciesId species);
MoveId learnMove(SpeciesId species, uint16_t index);
uint8_t learnLevel(SpeciesId species, uint16_t index);
LearnMethod learnMethod(SpeciesId species, uint16_t index);
