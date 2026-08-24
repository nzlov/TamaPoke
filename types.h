#pragma once
#include <stdint.h>
#include "content.h"

// Type effectiveness helpers. Type IDs are the engine ABI; chart and display
// metadata come from the active move pack.
//
// Everything is integer percentages: 100 = neutral, 200 = super effective,
// 50 = resisted, 0 = immune, and 400 / 25 for the doubled-up dual-type cases.
// No floats anywhere, so this is cheap on the MCU.

// Effectiveness of an attacking type against a defender with one or two types.
// Pass T_NONE as def2 for single-typed defenders.
static inline uint16_t typeEffPct(uint8_t atk, uint8_t def1, uint8_t def2) {
  if (atk >= TYPE_COUNT || def1 >= TYPE_COUNT) return 100;
  uint16_t e = typeEffectTenth(atk, def1);
  e *= (def2 < TYPE_COUNT) ? typeEffectTenth(atk, def2) : 10;
  return e;  // tenths x tenths = percent
}

// Same, but for a species straight out of the Pokedex.
static inline uint16_t typeEffVsDex(uint8_t atk, int16_t dex) {
  if (dex < 1 || dex > dexCount()) return 100;
  return typeEffPct(atk, dexEntry(dex).type1, dexEntry(dex).type2);
}

// Same-Type Attack Bonus: 1.5x when the move matches one of the user's types.
static inline bool hasStab(int16_t dex, uint8_t moveType) {
  if (dex < 1 || dex > dexCount()) return false;
  return dexEntry(dex).type1 == moveType || dexEntry(dex).type2 == moveType;
}

// Short display name for a type. English in every language, matching how the
// species names in the current regional packs are English for every UI locale.
static inline const char *typeName(uint8_t t) { return packedTypeName(t); }

// One colour per type, for the chips on move rows and the battle grid. The move
// list used to print the type as grey 6px text, which on a 466px round panel at
// arm's length is unreadable -- and type is the single most important thing
// about a move in a game whose whole combat model is the type chart.
//
static inline uint16_t typeColor(uint8_t t) {
  return packedTypeColor(t);
}
static inline bool typeColorIsLight(uint8_t t) {
  return packedTypeColorIsLight(t);
}
