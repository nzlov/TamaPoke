#include "inventory.h"
#include "content.h"

Inventory inventory;

int Inventory::find(ItemKey key) const {
  if (!key) return -1;
  for (uint8_t i = 0; i < INVENTORY_MAX_STACKS; i++)
    if (stacks[i].key == key && stacks[i].count) return i;
  return -1;
}

int Inventory::freeSlot() const {
  for (uint8_t i = 0; i < INVENTORY_MAX_STACKS; i++)
    if (!stacks[i].key || !stacks[i].count) return i;
  return -1;
}

void Inventory::begin() {
  for (auto &stack : stacks) stack = InventoryStack();
  prefs.begin("tamapoke", false);
  if (prefs.getBytesLength("items") == sizeof(stacks))
    prefs.getBytes("items", stacks, sizeof(stacks));
  suppliedDay = prefs.getUInt("itemday", 0);
  suppliedOnce = prefs.getBool("iteminit", false);
  for (auto &stack : stacks) {
    if (!stack.key || !stack.count) { stack = InventoryStack(); continue; }
    if (stack.count > INVENTORY_STACK_MAX) stack.count = INVENTORY_STACK_MAX;
  }
}

uint8_t Inventory::count(ItemKey key) const {
  int index = find(key);
  return index < 0 ? 0 : stacks[index].count;
}

bool Inventory::add(ItemKey key, uint8_t amount) {
  if (!key || !amount || !itemByKey(key)) return false;
  int index = find(key);
  if (index < 0) index = freeSlot();
  if (index < 0) return false;
  uint16_t next = (uint16_t)stacks[index].count + amount;
  stacks[index].key = key;
  stacks[index].count = next > INVENTORY_STACK_MAX ? INVENTORY_STACK_MAX : (uint8_t)next;
  save();
  return true;
}

bool Inventory::consume(ItemKey key, uint8_t amount) {
  int index = find(key);
  if (index < 0 || !amount || stacks[index].count < amount) return false;
  stacks[index].count -= amount;
  if (!stacks[index].count) stacks[index] = InventoryStack();
  save();
  return true;
}

uint8_t Inventory::stackCount() const {
  uint8_t result = 0;
  for (const auto &stack : stacks) if (stack.key && stack.count) result++;
  return result;
}

const InventoryStack *Inventory::stackAt(uint8_t index) const {
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

ItemKey Inventory::grantWeightedDrop(uint32_t roll) {
  uint32_t total = 0;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->dropWeight && count(item->key) < INVENTORY_STACK_MAX)
      total += item->dropWeight;
  }
  if (!total) return ITEM_KEY_NONE;
  uint32_t pick = roll % total;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item || !item->dropWeight || count(item->key) >= INVENTORY_STACK_MAX) continue;
    if (pick < item->dropWeight) return add(item->key) ? item->key : ITEM_KEY_NONE;
    pick -= item->dropWeight;
  }
  return ITEM_KEY_NONE;
}

ItemKey Inventory::grantMechanicReward(ItemMechanicKind mechanic,
                                       MegaFormKind megaForm) {
  if (mechanic < ITEM_MECHANIC_Z_MOVE || mechanic > ITEM_MECHANIC_MEGA)
    return ITEM_KEY_NONE;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item || item->effect != ITEM_EFFECT_BATTLE_MECHANIC ||
        item->flags != (uint8_t)mechanic ||
        (mechanic == ITEM_MECHANIC_MEGA && megaForm != MEGA_FORM_NONE &&
         item->param != megaForm)) continue;
    return add(item->key) ? item->key : ITEM_KEY_NONE;
  }
  return ITEM_KEY_NONE;
}

void Inventory::save() {
  prefs.putBytes("items", stacks, sizeof(stacks));
}
