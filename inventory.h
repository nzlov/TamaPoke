#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "items.h"

constexpr uint8_t INVENTORY_STACK_MAX = ITEM_STACK_LIMIT;
constexpr uint8_t INVENTORY_MAX_STACKS = 32;

struct InventoryStack {
  ItemKey key = ITEM_KEY_NONE;
  uint8_t count = 0;
};

class Inventory {
public:
  void begin();
  uint8_t count(ItemKey key) const;
  bool add(ItemKey key, uint8_t amount = 1);
  bool consume(ItemKey key, uint8_t amount = 1);
  uint8_t stackCount() const;
  const InventoryStack *stackAt(uint8_t index) const;
  void ensureDailySupply(uint32_t day);
  ItemKey grantWeightedDrop(uint32_t roll, const ItemKey *excluded = nullptr,
                            uint8_t excludedCount = 0);
  ItemKey grantMechanicReward(ItemMechanicKind mechanic,
                              MegaFormKind megaForm = MEGA_FORM_NONE);
  void save();

private:
  InventoryStack stacks[INVENTORY_MAX_STACKS] = {};
  uint32_t suppliedDay = 0;
  bool suppliedOnce = false;
  Preferences prefs;

  int find(ItemKey key) const;
  int freeSlot() const;
};

extern Inventory inventory;
