#pragma once
#include <stdint.h>

// Item identities belong to the move pack. The firmware only carries an
// opaque key while executing one of these generic effect opcodes.
using ItemKey = uint16_t;
constexpr ItemKey ITEM_KEY_NONE = 0;
constexpr uint16_t CONTENT_MAX_ITEMS = 64;
constexpr uint8_t ITEM_STACK_LIMIT = 99;

enum ItemCategory : uint8_t {
  ITEM_CATEGORY_BALL = 1,
  ITEM_CATEGORY_MEDICINE,
  ITEM_CATEGORY_STATUS,
  ITEM_CATEGORY_REVIVE,
  ITEM_CATEGORY_EVOLUTION,
  ITEM_CATEGORY_TRAINING,
  ITEM_CATEGORY_BATTLE_BOOST,
  ITEM_CATEGORY_MECHANIC,
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

struct Combatant;
class Pet;

// The pack selects an opcode and parameters. Firmware executes that generic
// effect without assigning meaning to any concrete item key.
bool itemCanApplyToCombatant(const ItemEntry &item, const Combatant &target);
bool itemApplyToCombatant(const ItemEntry &item, Combatant &target);
bool itemCanApplyToPet(const ItemEntry &item, const Pet &target);
bool itemApplyToPet(const ItemEntry &item, Pet &target);
