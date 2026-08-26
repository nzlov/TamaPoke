#pragma once
#include <stdint.h>

// The canonical 5x5 nature table: the row raises a combat stat and the column
// lowers one. The five diagonal entries are repurposed for TamaPoke's training
// system because their canonical modifiers cancel out.
enum NatureId : uint8_t {
  NATURE_HARDY, NATURE_LONELY, NATURE_BRAVE, NATURE_ADAMANT, NATURE_NAUGHTY,
  NATURE_BOLD, NATURE_DOCILE, NATURE_RELAXED, NATURE_IMPISH, NATURE_LAX,
  NATURE_TIMID, NATURE_HASTY, NATURE_SERIOUS, NATURE_JOLLY, NATURE_NAIVE,
  NATURE_MODEST, NATURE_MILD, NATURE_QUIET, NATURE_BASHFUL, NATURE_RASH,
  NATURE_CALM, NATURE_GENTLE, NATURE_SASSY, NATURE_CAREFUL, NATURE_QUIRKY,
  NATURE_COUNT,
  NATURE_UNKNOWN = 0xFF,
};

// Order matches the canonical nature matrix above. NONE is used for HP, which
// is never modified by a nature.
enum NatureStat : uint8_t {
  NATURE_STAT_ATK, NATURE_STAT_DEF, NATURE_STAT_SPE,
  NATURE_STAT_SPA, NATURE_STAT_SPD,
  NATURE_STAT_NONE = 0xFF,
};

// TamaPoke stores three training values. ATK is also the training contribution
// for SpA, and DEF is also the contribution for SpD.
enum NatureTraining : uint8_t {
  NATURE_TRAIN_ATK, NATURE_TRAIN_DEF, NATURE_TRAIN_SPE,
};

bool natureValid(NatureId nature);

// Applies either a canonical +/-10% final-stat modifier, or one of the five
// training-nature rules to the training term alone.
uint16_t natureStatValue(NatureId nature, NatureStat stat,
                         uint16_t untrained, uint8_t training);

// 3 for a strengthened training channel, 7 for a weakened one, otherwise 5.
// Pet uses this against the IV-based maximum for fixed hourly decay.
uint8_t natureTrainingDecayPercent(NatureId nature, NatureTraining training);

// Existing saves had no nature. This gives each existing individual a stable
// value from fields it already owns, without turning every boot into a reroll.
NatureId natureForLegacy(int16_t dex, uint8_t ivAtk, uint8_t ivDef,
                         uint8_t ivSpe, uint8_t ivHp);
