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

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
