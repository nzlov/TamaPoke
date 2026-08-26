// The bag persists opaque keys supplied by the move pack. No test names or
// hard-codes an item identity: it discovers definitions exactly as firmware does.
#include "Arduino.h"
#include "Preferences.h"
#include "content.h"
#include "inventory.h"
#include <cstdio>

uint32_t g_seed = 31;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

int main() {
  contentBegin();
  ck(itemCount() >= 5, "the move pack exposes a basic item catalogue");

  bool categories[ITEM_CATEGORY_REVIVE + 1] = {};
  bool unique = true;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item || !item->key || itemByKey(item->key) != item || !itemName(item->key)[0] ||
        !itemDescription(item->key, "de-DE"))
      unique = false;
    if (item && item->category <= ITEM_CATEGORY_REVIVE) categories[item->category] = true;
    for (uint16_t j = 0; item && j < i; j++)
      if (itemAt(j)->key == item->key) unique = false;
  }
  ck(unique, "item keys are unique opaque pack identities");
  bool allCategories = true;
  for (uint8_t category = ITEM_CATEGORY_BALL;
       category <= ITEM_CATEGORY_REVIVE; category++)
    if (!categories[category]) allCategories = false;
  ck(allCategories, "the first catalogue covers every basic effect category");
  uint16_t maxWeight[5] = {};
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->rarity < 5 && item->dropWeight > maxWeight[item->rarity])
      maxWeight[item->rarity] = item->dropWeight;
  }
  bool rarityWeighted = true;
  for (uint8_t rarity = 2; rarity < 5; rarity++)
    if (maxWeight[rarity] && maxWeight[rarity - 1] <= maxWeight[rarity]) rarityWeighted = false;
  ck(rarityWeighted, "rarer pack items have lower drop weight");

  bool dailyBalance = true, plannedWeights = true;
  bool rarityBands = true;
  const uint8_t minWeight[5] = { 0, 30, 10, 2, 1 };
  const uint8_t maxBandWeight[5] = { 0, 50, 20, 8, 1 };
  uint8_t trainingTonics = 0, battleBoosters = 0, mechanicItems = 0;
  uint8_t trainingStats = 0, battleStats = 0, mechanicKinds = 0;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    bool encounterReward = item->effect == ITEM_EFFECT_BATTLE_MECHANIC;
    if (item->rarity > 4 || (!encounterReward &&
        (item->dropWeight < minWeight[item->rarity] ||
         item->dropWeight > maxBandWeight[item->rarity]))) rarityBands = false;
    if (item->effect == ITEM_EFFECT_CATCH) {
      uint8_t expected = item->param == 100 ? 5 : 0;
      if (item->dailyMin != expected) dailyBalance = false;
      uint16_t expectedWeight = item->param == 100 ? 50 : item->param == 150 ? 20 : 8;
      if (item->dropWeight != expectedWeight) plannedWeights = false;
    } else if (item->effect == ITEM_EFFECT_HEAL_HP && item->param == 20) {
      if (item->dailyMin != 2) dailyBalance = false;
      if (item->dropWeight != 40) plannedWeights = false;
    } else if (item->effect == ITEM_EFFECT_HEAL_HP && item->param == 60) {
      if (item->dropWeight != 16) plannedWeights = false;
    } else if (item->effect == ITEM_EFFECT_CURE_STATUS) {
      if (item->dropWeight != 14) plannedWeights = false;
    } else if (item->effect == ITEM_EFFECT_REVIVE) {
      if (item->dropWeight != 6) plannedWeights = false;
    }
    if (item->effect == ITEM_EFFECT_TRAINING_FLOOR && item->param == 10 &&
        item->dropWeight == 3 && !item->dailyMin) {
      trainingTonics++;
      trainingStats |= item->flags;
    }
    if (item->effect == ITEM_EFFECT_BATTLE_STAGE && item->param == 1 &&
        item->dropWeight == 10 && !item->dailyMin) {
      battleBoosters++;
      battleStats |= item->flags;
    }
    if (encounterReward && item->category == ITEM_CATEGORY_MECHANIC &&
        item->rarity == 4 && !item->dropWeight && !item->dailyMin && !item->param) {
      mechanicItems++;
      mechanicKinds |= (uint8_t)(1u << (item->flags - 1));
    }
  }
  ck(dailyBalance,
     "only basic balls refill to five and basic medicine refills to two");
  ck(rarityBands, "drop weights stay inside non-overlapping rarity bands");
  ck(plannedWeights, "each basic item keeps its planned value within that rarity band");
  ck(trainingTonics == 3 && trainingStats == (ITEM_STAT_ATK | ITEM_STAT_DEF | ITEM_STAT_SPE) &&
     battleBoosters == 5 &&
     battleStats == (ITEM_STAT_ATK | ITEM_STAT_DEF | ITEM_STAT_SPA |
                     ITEM_STAT_SPD | ITEM_STAT_SPE),
     "the pack exposes three rare training tonics and five battle boosters");
  ck(mechanicItems == 3 && mechanicKinds == 0x07,
     "the pack exposes exactly one non-weighted reward item for each battle mechanic");

  Inventory bag;
  bag.begin();
  bag.ensureDailySupply(100);
  bool supplied = true;
  ItemKey dailyKey = ITEM_KEY_NONE;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (!item->dailyMin) continue;
    if (!dailyKey) dailyKey = item->key;
    if (bag.count(item->key) < item->dailyMin) supplied = false;
  }
  ck(supplied && dailyKey, "daily minimums come from pack data");

  uint8_t before = bag.count(dailyKey);
  ck(bag.consume(dailyKey), "an item can be consumed");
  bag.ensureDailySupply(100);
  ck(bag.count(dailyKey) == before - 1, "the same day does not refill twice");
  bag.ensureDailySupply(101);
  ck(bag.count(dailyKey) == before, "the next day restores the configured minimum");

  ck(bag.add(dailyKey, 255) && bag.count(dailyKey) == INVENTORY_STACK_MAX,
     "a stack saturates at the firmware capacity");
  Inventory loaded;
  loaded.begin();
  ck(loaded.count(dailyKey) == INVENTORY_STACK_MAX,
     "opaque-key inventory survives a reload");

  bool hasWeightedDrop = false;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item->dropWeight && loaded.count(item->key) < INVENTORY_STACK_MAX) {
      hasWeightedDrop = loaded.grantWeightedDrop(0);
      break;
    }
  }
  ck(hasWeightedDrop, "a weighted drop grants one non-full pack item");

  bool mechanicRewards = true;
  for (uint8_t kind = ITEM_MECHANIC_Z_MOVE; kind <= ITEM_MECHANIC_MEGA; kind++) {
    const ItemEntry *expected = nullptr;
    for (uint16_t i = 0; i < itemCount(); i++) {
      const ItemEntry *item = itemAt(i);
      if (item && item->effect == ITEM_EFFECT_BATTLE_MECHANIC && item->flags == kind) {
        expected = item;
        break;
      }
    }
    if (!expected) { mechanicRewards = false; continue; }
    uint8_t beforeReward = loaded.count(expected->key);
    ItemKey granted = loaded.grantMechanicReward((ItemMechanicKind)kind);
    if (granted != expected->key || loaded.count(expected->key) != beforeReward + 1)
      mechanicRewards = false;
  }
  ck(mechanicRewards, "each wild mechanic grants its one corresponding reward item");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
