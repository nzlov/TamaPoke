#pragma once
#include <stdint.h>
#include "dex.h"

enum : uint8_t { MC_PHYS = 0, MC_SPEC = 1, MC_STATUS = 2 };
enum : uint8_t {
  EF_NONE = 0, EF_STAGE, EF_RECOIL, EF_DRAIN, EF_FIXED_LVL, EF_FIXED,
  EF_PRIORITY, EF_NEVER_MISS, EF_MULTI, EF_HEAL, EF_RECHARGE, EF_CHARGE,
  EF_PROTECT, EF_SET_WEATHER, EF_SET_TERRAIN, EF_SET_SCREEN, EF_SET_HAZARD,
  EF_CLEAR_FIELD, EF_FORCE_SWITCH, EF_PIVOT,
};
enum : uint8_t {
  MF_NONE = 0,
  MF_RAIN_ACCURATE = 1,
  MF_SNOW_ACCURATE = 2,
  MF_SOLAR_CHARGE = 4,
  MF_GRASSY_WEAKENED = 8,
  MF_STANCE_SHIELD = 16,
  MF_AURA_WHEEL = 32,
  MF_GULP_MISSILE = 64,
};
enum : uint16_t {
  MT_NONE = 0,
  MT_CONTACT = 1 << 0,
  MT_SOUND = 1 << 1,
  MT_PUNCH = 1 << 2,
  MT_BITE = 1 << 3,
  MT_PULSE = 1 << 4,
  MT_BALLISTIC = 1 << 5,
  MT_POWDER = 1 << 6,
  MT_DANCE = 1 << 7,
  MT_SLICING = 1 << 8,
  MT_WIND = 1 << 9,
  MT_REFLECTABLE = 1 << 10,
  MT_ALL = (1 << 11) - 1,
};
enum : uint8_t {
  ST_ATK = 1, ST_DEF = 2, ST_SPA = 4, ST_SPD = 8, ST_SPE = 16,
  ST_ACC = 32, ST_EVA = 64,
};
enum : uint8_t { TG_SELF = 0, TG_FOE = 1 };
enum BattleScreen : uint8_t {
  BSCREEN_NONE = 0, BSCREEN_REFLECT, BSCREEN_LIGHT_SCREEN, BSCREEN_AURORA_VEIL,
};
enum BattleHazard : uint8_t {
  BHAZARD_NONE = 0, BHAZARD_SPIKES, BHAZARD_TOXIC_SPIKES,
  BHAZARD_STEALTH_ROCK, BHAZARD_STICKY_WEB,
};
enum BattleFieldClear : uint8_t {
  BCLEAR_NONE = 0, BCLEAR_OWN_HAZARDS, BCLEAR_ALL,
};
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
  uint16_t tags;
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
bool speciesCanLearnMove(SpeciesId species, MoveId move);
