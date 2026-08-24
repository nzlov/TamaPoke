#pragma once
#include <stdint.h>
#include "dex.h"

constexpr uint8_t TRAINER_TEAM_MAX = 6;
constexpr uint8_t MAX_TRAINER_LEVEL = 100;
constexpr uint8_t CONTENT_MAX_TRAINERS_PER_REGION = 32;
constexpr uint8_t CONTENT_MAX_BADGES_PER_REGION = 16;

struct TrainerMon {
  SpeciesId dex;
  uint8_t level;
};

struct Trainer {
  const char *name;
  const char *place;
  uint8_t type;
  uint8_t count;
  TrainerMon team[TRAINER_TEAM_MAX];
};

struct RegionBattleInfo {
  uint8_t trainerCount;
  uint8_t gymCount;
  uint8_t elite4Count;
  uint8_t easyIv;
  uint8_t hardIv;
};

bool regionBattleAvailable(uint8_t region);
const RegionBattleInfo &regionBattleInfo(uint8_t region);
const Trainer &trainerInfo(uint8_t region, uint8_t index);
const char *trainerName(uint8_t region, uint8_t index);
const char *trainerPlace(uint8_t region, uint8_t index);
