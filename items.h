#pragma once
#include <stdint.h>
#include "moves.h"

// Item identities belong to the item pack. The firmware only carries an
// opaque key while executing one of these generic effect opcodes.
using ItemKey = uint16_t;
constexpr ItemKey ITEM_KEY_NONE = 0;
constexpr uint16_t CONTENT_MAX_ITEMS = 64;
constexpr uint8_t ITEM_STACK_LIMIT = 99;
// Catch-item params are percentage multipliers, except this data-driven
// sentinel which bypasses the probability roll for a valid wild target.
constexpr int16_t ITEM_CATCH_GUARANTEED = -1;

enum ItemCategory : uint8_t {
  ITEM_CATEGORY_BALL = 1,
  ITEM_CATEGORY_MEDICINE,
  ITEM_CATEGORY_STATUS,
  ITEM_CATEGORY_REVIVE,
  ITEM_CATEGORY_EVOLUTION,
  ITEM_CATEGORY_TRAINING,
  ITEM_CATEGORY_BATTLE_BOOST,
  ITEM_CATEGORY_MECHANIC,
  ITEM_CATEGORY_MOVE_STONE,
};

enum ItemEffect : uint8_t {
  ITEM_EFFECT_CATCH = 1,
  ITEM_EFFECT_HEAL_HP,
  ITEM_EFFECT_CURE_STATUS,
  ITEM_EFFECT_REVIVE,
  ITEM_EFFECT_EVOLVE,
  ITEM_EFFECT_TRAINING_FLOOR,
  ITEM_EFFECT_BATTLE_STAGE,
  ITEM_EFFECT_BATTLE_MECHANIC,
  ITEM_EFFECT_GIGANTAMAX_FACTOR,
  ITEM_EFFECT_TEACH_MOVE,
};

enum ItemStatMask : uint8_t {
  ITEM_STAT_ATK = 1 << 0,
  ITEM_STAT_DEF = 1 << 1,
  ITEM_STAT_SPA = 1 << 2,
  ITEM_STAT_SPD = 1 << 3,
  ITEM_STAT_SPE = 1 << 4,
};

enum ItemMechanicKind : uint8_t {
  ITEM_MECHANIC_Z_MOVE = 1,
  ITEM_MECHANIC_DYNAMAX = 2,
  ITEM_MECHANIC_MEGA = 3,
};

enum MegaFormKind : uint8_t {
  MEGA_FORM_STANDARD = 0,
  MEGA_FORM_X = 1,
  MEGA_FORM_Y = 2,
  MEGA_FORM_Z = 3,
  MEGA_FORM_NONE = 0xFF,
};

struct ItemEntry {
  ItemKey key = ITEM_KEY_NONE;
  const char *name = "?";
  uint8_t category = 0;
  uint8_t effect = 0;
  uint8_t rarity = 0;
  uint8_t flags = 0;
  int16_t param = 0;
  uint16_t dropWeight = 0;
  uint8_t dailyMin = 0;
};

// A catalogue key plus the per-instance move carried by a move stone. Ordinary
// items keep MOVE_NONE, so existing item identities remain unchanged.
struct ItemRef {
  ItemKey key = ITEM_KEY_NONE;
  MoveId move = MOVE_NONE;
  explicit operator bool() const { return key != ITEM_KEY_NONE; }
};

struct Combatant;
struct PartyMon;
class Pet;

// The pack selects an opcode and parameters. Firmware executes that generic
// effect without assigning meaning to any concrete item key.
bool itemCanApplyToCombatant(const ItemEntry &item, const Combatant &target);
bool itemApplyToCombatant(const ItemEntry &item, Combatant &target);
bool itemCanApplyToPet(const ItemEntry &item, const Pet &target,
                       MoveId move = MOVE_NONE);
bool itemApplyToPet(const ItemEntry &item, Pet &target,
                    MoveId move = MOVE_NONE);
bool itemUsableOutsideBattle(const ItemEntry &item);
bool itemCanApplyToPartyMon(const ItemEntry &item, const PartyMon &target,
                            MoveId move = MOVE_NONE);
bool itemApplyToPartyMon(const ItemEntry &item, PartyMon &target,
                         MoveId move = MOVE_NONE);
