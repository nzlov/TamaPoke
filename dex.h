#pragma once
#include <stdint.h>

// Stable IDs understood by the engine. Names, colours, charts and species data
// live in SD packs; only the wire/save ABI remains in firmware.
enum PkType : uint8_t {
  T_NORMAL, T_FIRE, T_WATER, T_ELECTRIC, T_GRASS, T_ICE, T_FIGHTING,
  T_POISON, T_GROUND, T_FLYING, T_PSYCHIC, T_BUG, T_ROCK, T_GHOST,
  T_DRAGON, T_DARK, T_STEEL, T_FAIRY,
  T_NONE = 255
};
constexpr uint8_t TYPE_COUNT = 18;

enum : uint8_t { R_EVO = 0, R_COMUN, R_RARO, R_LEGENDARIO };

using SpeciesId = uint16_t;
constexpr SpeciesId SPECIES_NONE = 0;
// Embedded resource limits, not catalogue counts. A future pack may grow up to
// these limits without changing the save or public structures.
constexpr SpeciesId CONTENT_MAX_SPECIES = 2048;
constexpr uint8_t CONTENT_MAX_REGIONS = 16;
constexpr uint8_t CONTENT_MAX_EVOLUTIONS = 8;

struct DexEntry {
  const char *name;
  uint8_t evolveLevel;
  uint8_t rarity;
  uint16_t accent;
  uint8_t bHp, bAtk, bDef, bSpe, bSpA, bSpD;
  uint8_t biome;
  uint8_t type1, type2;
  SpeciesId evolutions[CONTENT_MAX_EVOLUTIONS];
  uint8_t evolutionCount;
};

struct RegionInfo {
  const char *name;
  SpeciesId lo, hi;
  const SpeciesId *starters;
  uint8_t starterCount;
};

uint16_t dexCount();
bool dexValid(SpeciesId id);
const DexEntry &dexEntry(SpeciesId id);
uint8_t evolutionCount(SpeciesId id);
SpeciesId evolutionTarget(SpeciesId id, uint8_t index);
bool evolutionAvailable(SpeciesId id);

uint8_t regionCount();             // real regions plus the derived ALL entry
uint8_t regionAll();
const RegionInfo &regionInfo(uint8_t index);
bool regionPackAvailable(uint8_t index);
const char *regionName(uint8_t index);
