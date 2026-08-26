#include "nature.h"

bool natureValid(NatureId nature) {
  return (uint8_t)nature < NATURE_COUNT;
}

static bool trainingNature(NatureId nature) {
  if (!natureValid(nature)) return false;
  uint8_t id = (uint8_t)nature;
  return id / 5 == id % 5;
}

// +1 strengthens the channel, -1 weakens it, 0 leaves it alone.
static int8_t trainingEffect(NatureId nature, NatureTraining training) {
  switch (nature) {
    case NATURE_HARDY:
      return training == NATURE_TRAIN_ATK ? 1 : 0;
    case NATURE_DOCILE:
      return training == NATURE_TRAIN_DEF ? 1 : 0;
    case NATURE_SERIOUS:
      return training == NATURE_TRAIN_SPE ? 1 : 0;
    case NATURE_BASHFUL:
      return training == NATURE_TRAIN_DEF ? 1
           : training == NATURE_TRAIN_ATK ? -1 : 0;
    case NATURE_QUIRKY:
      return training == NATURE_TRAIN_ATK ? 1
           : training == NATURE_TRAIN_DEF ? -1 : 0;
    default:
      return 0;
  }
}

static NatureTraining trainingFor(NatureStat stat) {
  if (stat == NATURE_STAT_ATK || stat == NATURE_STAT_SPA) return NATURE_TRAIN_ATK;
  if (stat == NATURE_STAT_DEF || stat == NATURE_STAT_SPD) return NATURE_TRAIN_DEF;
  return NATURE_TRAIN_SPE;
}

uint16_t natureStatValue(NatureId nature, NatureStat stat,
                         uint16_t untrained, uint8_t training) {
  if (stat == NATURE_STAT_NONE || !natureValid(nature))
    return untrained + training;

  if (trainingNature(nature)) {
    int8_t effect = trainingEffect(nature, trainingFor(stat));
    uint16_t trained = effect > 0 ? (uint16_t)training * 110 / 100
                     : effect < 0 ? (uint16_t)training * 90 / 100
                                  : training;
    return untrained + trained;
  }

  uint16_t value = untrained + training;
  uint8_t id = (uint8_t)nature;
  if (id / 5 == (uint8_t)stat) return (uint32_t)value * 110 / 100;
  if (id % 5 == (uint8_t)stat) return (uint32_t)value * 90 / 100;
  return value;
}

uint8_t natureTrainingDecayPercent(NatureId nature, NatureTraining training) {
  int8_t effect = trainingEffect(nature, training);
  return effect > 0 ? 3 : effect < 0 ? 7 : 5;
}

NatureId natureForLegacy(int16_t dex, uint8_t ivAtk, uint8_t ivDef,
                         uint8_t ivSpe, uint8_t ivHp) {
  if (dex < 1) return NATURE_UNKNOWN;
  uint32_t hash = 2166136261u;
  const uint8_t bytes[] = {
    (uint8_t)dex, (uint8_t)((uint16_t)dex >> 8), ivAtk, ivDef, ivSpe, ivHp,
  };
  for (uint8_t value : bytes) {
    hash ^= value;
    hash *= 16777619u;
  }
  return (NatureId)(hash % NATURE_COUNT);
}
