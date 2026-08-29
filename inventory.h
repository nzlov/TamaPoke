#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "items.h"

constexpr uint8_t INVENTORY_STACK_MAX = ITEM_STACK_LIMIT;
constexpr uint16_t INVENTORY_MAX_STACKS = 384;

struct InventoryStack {
  ItemKey key = ITEM_KEY_NONE;
  MoveId move = MOVE_NONE;
  uint8_t count = 0;
  ItemRef ref() const { return { key, move }; }
};
static_assert(sizeof(InventoryStack) * INVENTORY_MAX_STACKS <= 4096,
              "the inventory blob must fit one NVS value");

class Inventory {
public:
  void begin();
  uint8_t count(ItemKey key, MoveId move = MOVE_NONE) const;
  bool add(ItemKey key, uint8_t amount = 1, MoveId move = MOVE_NONE);
  bool add(ItemRef item, uint8_t amount = 1) { return add(item.key, amount, item.move); }
  bool consume(ItemKey key, uint8_t amount = 1, MoveId move = MOVE_NONE);
  bool consume(ItemRef item, uint8_t amount = 1) { return consume(item.key, amount, item.move); }
  uint16_t stackCount() const;
  const InventoryStack *stackAt(uint16_t index) const;
  void ensureDailySupply(uint32_t day);
  ItemRef grantWeightedDrop(uint32_t roll, const MoveId *foeMoves = nullptr,
                            uint8_t foeMoveCount = 0,
                            const ItemRef *excluded = nullptr,
                            uint8_t excludedCount = 0);
  ItemRef grantMechanicReward(ItemMechanicKind mechanic,
                              MegaFormKind megaForm = MEGA_FORM_NONE);
  void save();

private:
  InventoryStack stacks[INVENTORY_MAX_STACKS] = {};
  uint32_t suppliedDay = 0;
  bool suppliedOnce = false;
  Preferences prefs;

  int find(ItemKey key, MoveId move = MOVE_NONE) const;
  int freeSlot() const;
  bool canAdd(ItemKey key, MoveId move) const;
};

extern Inventory inventory;
