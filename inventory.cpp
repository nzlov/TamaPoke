#include "inventory.h"
#include "content.h"
#include "perf.h"

Inventory inventory;
constexpr uint8_t MOVE_STONE_DROP_MOVE_MAX = 8;

struct LegacyInventoryStack {
  ItemKey key;
  uint8_t count;
};
static_assert(sizeof(LegacyInventoryStack) == 4,
              "legacy inventory stack layout must stay byte-exact");
constexpr uint8_t LEGACY_INVENTORY_MAX_STACKS = 32;

int Inventory::find(ItemKey key, MoveId move) const {
  if (!key) return -1;
  for (uint16_t i = 0; i < INVENTORY_MAX_STACKS; i++)
    if (stacks[i].key == key && stacks[i].move == move && stacks[i].count) return i;
  return -1;
}

int Inventory::freeSlot() const {
  for (uint16_t i = 0; i < INVENTORY_MAX_STACKS; i++)
    if (!stacks[i].key || !stacks[i].count) return i;
  return -1;
}

bool Inventory::canAdd(ItemKey key, MoveId move) const {
  const ItemEntry *item = itemByKey(key);
  if (!item) return false;
  if (item->effect == ITEM_EFFECT_TEACH_MOVE) {
    if (!moveValid(move)) return false;
  } else if (move != MOVE_NONE) {
    return false;
  }
  int index = find(key, move);
  return index >= 0 ? stacks[index].count < INVENTORY_STACK_MAX : freeSlot() >= 0;
}

void Inventory::begin() {
  for (auto &stack : stacks) stack = InventoryStack();
  batchDepth = 0;
  batchDirty = false;
  prefs.begin("tamapoke", false);
  size_t stored = prefs.getBytesLength("items");
  if (stored == sizeof(stacks)) {
    prefs.getBytes("items", stacks, sizeof(stacks));
  } else if (stored == sizeof(LegacyInventoryStack) * LEGACY_INVENTORY_MAX_STACKS) {
    // GLUE: old stacks had no per-instance attribute. Copy their ordinary item
    // keys into the expanded representation; remove after legacy saves expire.
    LegacyInventoryStack old[LEGACY_INVENTORY_MAX_STACKS] = {};
    prefs.getBytes("items", old, sizeof(old));
    for (uint8_t i = 0; i < LEGACY_INVENTORY_MAX_STACKS; i++) {
      stacks[i].key = old[i].key;
      stacks[i].count = old[i].count;
    }
  }
  suppliedDay = prefs.getUInt("itemday", 0);
  suppliedOnce = prefs.getBool("iteminit", false);
  for (auto &stack : stacks) {
    if (!stack.key || !stack.count) { stack = InventoryStack(); continue; }
    const ItemEntry *item = itemByKey(stack.key);
    if (!item || (item->effect == ITEM_EFFECT_TEACH_MOVE && !moveValid(stack.move)) ||
        (item->effect != ITEM_EFFECT_TEACH_MOVE && stack.move != MOVE_NONE)) {
      stack = InventoryStack();
      continue;
    }
    if (stack.count > INVENTORY_STACK_MAX) stack.count = INVENTORY_STACK_MAX;
  }
}

uint8_t Inventory::count(ItemKey key, MoveId move) const {
  int index = find(key, move);
  return index < 0 ? 0 : stacks[index].count;
}

bool Inventory::add(ItemKey key, uint8_t amount, MoveId move) {
  if (!amount || !canAdd(key, move)) return false;
  int index = find(key, move);
  if (index < 0) index = freeSlot();
  if (index < 0) return false;
  uint16_t next = (uint16_t)stacks[index].count + amount;
  stacks[index].key = key;
  stacks[index].move = move;
  stacks[index].count = next > INVENTORY_STACK_MAX ? INVENTORY_STACK_MAX : (uint8_t)next;
  persistChange();
  return true;
}

bool Inventory::consume(ItemKey key, uint8_t amount, MoveId move) {
  int index = find(key, move);
  if (index < 0 || !amount || stacks[index].count < amount) return false;
  stacks[index].count -= amount;
  if (!stacks[index].count) stacks[index] = InventoryStack();
  persistChange();
  return true;
}

uint16_t Inventory::stackCount() const {
  uint16_t result = 0;
  for (const auto &stack : stacks) if (stack.key && stack.count) result++;
  return result;
}

const InventoryStack *Inventory::stackAt(uint16_t index) const {
  for (const auto &stack : stacks) {
    if (!stack.key || !stack.count) continue;
    if (!index--) return &stack;
  }
  return nullptr;
}

void Inventory::ensureDailySupply(uint32_t day) {
  if (suppliedOnce && (!day || suppliedDay == day)) return;
  bool changed = false;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item || !item->dailyMin) continue;
    uint8_t have = count(item->key);
    if (have >= item->dailyMin) continue;
    int slot = find(item->key);
    if (slot < 0) slot = freeSlot();
    if (slot < 0) continue;
    stacks[slot].key = item->key;
    stacks[slot].count = item->dailyMin;
    changed = true;
  }
  suppliedOnce = true;
  suppliedDay = day;
  prefs.putBool("iteminit", true);
  prefs.putUInt("itemday", suppliedDay);
  if (changed) save();
}

ItemRef Inventory::grantWeightedDrop(uint32_t roll, const MoveId *foeMoves,
                                     uint8_t foeMoveCount,
                                     const ItemRef *excluded,
                                     uint8_t excludedCount) {
  const ItemEntry *stone = nullptr;
  MoveId stoneMoves[MOVE_STONE_DROP_MOVE_MAX] = {};
  uint8_t stoneMoveCount = 0;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->effect == ITEM_EFFECT_TEACH_MOVE) { stone = item; break; }
  }
  if (stone && foeMoves) {
    for (uint8_t i = 0; i < foeMoveCount && i < MOVE_STONE_DROP_MOVE_MAX; i++) {
      MoveId move = foeMoves[i];
      if (!moveValid(move) || !canAdd(stone->key, move)) continue;
      bool duplicate = false;
      for (uint8_t j = 0; j < stoneMoveCount; j++)
        if (stoneMoves[j] == move) duplicate = true;
      if (!duplicate) stoneMoves[stoneMoveCount++] = move;
    }
  }
  uint32_t total = 0;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item || !item->dropWeight) continue;
    if (item->effect == ITEM_EFFECT_TEACH_MOVE) {
      bool hasEligibleMove = false;
      for (uint8_t j = 0; j < stoneMoveCount; j++) {
        bool blocked = false;
        for (uint8_t k = 0; excluded && k < excludedCount; k++)
          if (excluded[k].key == item->key) blocked = true;
        if (!blocked) { hasEligibleMove = true; break; }
      }
      if (hasEligibleMove) total += item->dropWeight;
    } else if (canAdd(item->key, MOVE_NONE)) {
      bool blocked = false;
      for (uint8_t j = 0; excluded && j < excludedCount; j++)
        if (excluded[j].key == item->key) blocked = true;
      if (blocked) continue;
      total += item->dropWeight;
    }
  }
  if (!total) return {};
  uint32_t pick = roll % total;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item || !item->dropWeight) continue;
    if (item->effect == ITEM_EFFECT_TEACH_MOVE) {
      if (!stoneMoveCount) continue;
      MoveId eligibleMoves[MOVE_STONE_DROP_MOVE_MAX] = {};
      uint8_t eligibleCount = 0;
      for (uint8_t j = 0; j < stoneMoveCount; j++) {
        bool blocked = false;
        for (uint8_t k = 0; excluded && k < excludedCount; k++)
          if (excluded[k].key == item->key) blocked = true;
        if (!blocked) eligibleMoves[eligibleCount++] = stoneMoves[j];
      }
      if (!eligibleCount) continue;
      if (pick < item->dropWeight) {
        MoveId move = eligibleMoves[(roll / total) % eligibleCount];
        ItemRef result = { item->key, move };
        return add(result) ? result : ItemRef();
      }
    } else {
      if (!canAdd(item->key, MOVE_NONE)) continue;
      bool blocked = false;
      for (uint8_t j = 0; excluded && j < excludedCount; j++)
        if (excluded[j].key == item->key) blocked = true;
      if (blocked) continue;
      if (pick < item->dropWeight) {
        ItemRef result = { item->key, MOVE_NONE };
        return add(result) ? result : ItemRef();
      }
    }
    pick -= item->dropWeight;
  }
  return {};
}

ItemRef Inventory::grantMechanicReward(ItemMechanicKind mechanic,
                                       MegaFormKind megaForm) {
  if (mechanic < ITEM_MECHANIC_Z_MOVE || mechanic > ITEM_MECHANIC_MEGA)
    return {};
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item || item->effect != ITEM_EFFECT_BATTLE_MECHANIC ||
        item->flags != (uint8_t)mechanic ||
        (mechanic == ITEM_MECHANIC_MEGA && megaForm != MEGA_FORM_NONE &&
         item->param != megaForm)) continue;
    ItemRef result = { item->key, MOVE_NONE };
    return add(result) ? result : ItemRef();
  }
  return {};
}

void Inventory::beginBatch() {
  if (batchDepth < UINT8_MAX) batchDepth++;
}

void Inventory::commitBatch() {
  if (!batchDepth || --batchDepth) return;
  if (batchDirty) save();
  batchDirty = false;
}

void Inventory::persistChange() {
  if (batchDepth) batchDirty = true;
  else save();
}

void Inventory::save() {
  uint32_t started = perfNowUs();
  prefs.putBytes("items", stacks, sizeof(stacks));
  perfRecord(PERF_INVENTORY_SAVE, perfNowUs() - started, 1);
}
