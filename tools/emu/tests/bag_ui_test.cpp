#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "content.h"
#include "inventory.h"
#include "items.h"
#include "party.h"
#include "pet.h"
#include "ui_scroll.h"
#include <cstdio>

uint32_t g_seed = 0xBA601;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void render();
void bagTap(int16_t x, int16_t y);
void onSwipeV(int dir);

extern Pet pet;
extern Party party;
extern Inventory inventory;
extern Arduino_Canvas *gfx;
extern bool bagOpen;
extern uint8_t bagView, bagDiscardAmount;
extern ItemKey bagSelectedKey, bagDetailKey;
extern UiScrollView bagScroll;

enum : uint8_t {
  BAG_VIEW_LIST,
  BAG_VIEW_ACTIONS,
  BAG_VIEW_DETAIL,
  BAG_VIEW_TARGET,
  BAG_VIEW_QUANTITY,
  BAG_VIEW_CONFIRM,
};

static int bad = 0;

static void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static void emptyBag() {
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    while (item && inventory.consume(item->key)) {}
  }
}

static const ItemEntry *fieldItem() {
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->effect == ITEM_EFFECT_TRAINING_FLOOR) return item;
  }
  return nullptr;
}

static const ItemEntry *battleOnlyItem() {
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && !itemUsableOutsideBattle(*item)) return item;
  }
  return nullptr;
}

static void openOnly(const ItemEntry &item, uint8_t count) {
  emptyBag();
  inventory.add(item.key, count);
  bagOpen = true;
  bagView = BAG_VIEW_LIST;
  bagSelectedKey = bagDetailKey = ITEM_KEY_NONE;
  bagDiscardAmount = 1;
  bagScroll.reset();
  render();
}

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(1);
  if (pet.isEgg()) pet.dbgHatchAs(1, false);

  emptyBag();
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item) inventory.add(item->key);
  }
  bagOpen = true;
  bagView = BAG_VIEW_LIST;
  bagScroll.reset();
  render();
  check(bagScroll.canScrollDown(),
        "a populated bag exposes overflow through the shared scroll component");
  onSwipeV(-1);
  check(bagOpen && bagView == BAG_VIEW_LIST && bagScroll.offset() > 0,
        "swiping up scrolls the bag instead of closing it");
  bool scrollFramesValid = true;
  while (bagScroll.canScrollDown()) {
    onSwipeV(-1);
    gfx->frameReady = false;
    gfx->fullBlackClears = 0;
    render();
    size_t lit = 0;
    for (size_t pixel = 0; pixel < 466UL * 466UL; pixel++)
      if (gfx->buffer()[pixel] != RGB565_BLACK) lit++;
    if (!gfx->frameReady || gfx->fullBlackClears || lit < 10000)
      scrollFramesValid = false;
  }
  check(scrollFramesValid,
        "every scrolled bag frame flushes without a black intermediate frame");

  const ItemEntry *usable = fieldItem();
  check(usable != nullptr, "the content pack provides a field-use item");
  if (!usable) return 1;

  openOnly(*usable, 3);
  bagTap(200, 105);
  check(bagView == BAG_VIEW_ACTIONS && bagSelectedKey == usable->key,
        "tapping an item opens its action menu");
  bagTap(200, 172);
  check(bagView == BAG_VIEW_DETAIL && bagDetailKey == usable->key,
        "VIEW opens the localized item description");
  bagTap(233, 408);
  check(bagView == BAG_VIEW_ACTIONS,
        "leaving the description returns to the item action menu");

  const ItemEntry *battleOnly = battleOnlyItem();
  check(battleOnly != nullptr, "the content pack provides a battle-only item");
  if (battleOnly) {
    openOnly(*battleOnly, 1);
    bagTap(200, 105);
    bagTap(200, 230);
    check(bagView == BAG_VIEW_ACTIONS && inventory.count(battleOnly->key) == 1,
          "USE rejects an item whose effect belongs only to battle");
  }

  PartyMon reserve = pet.toPartyMon();
  reserve.dex = 4;
  reserve.trAtk = reserve.trDef = reserve.trSpe = 0;
  reserve.trMinAtk = reserve.trMinDef = reserve.trMinSpe = 0;
  reserve.ivAtk = reserve.ivDef = reserve.ivSpe = 31;
  party.replaceAt(1, reserve);
  openOnly(*usable, 3);
  bagTap(200, 105);
  uint8_t before = inventory.count(usable->key);
  bagTap(200, 230);
  check(bagView == BAG_VIEW_TARGET,
        "USE on a field item opens the cultivation-team target page");
  bagTap(300, 120);
  check(bagView == BAG_VIEW_LIST &&
        party.slots[1].trMinAtk + party.slots[1].trMinDef +
            party.slots[1].trMinSpe > 0 &&
        inventory.count(usable->key) == before - 1,
        "choosing a reserve member applies the item, persists it, and consumes one");

  openOnly(*usable, 3);
  bagTap(200, 105);
  bagTap(200, 288);
  check(bagView == BAG_VIEW_QUANTITY && bagDiscardAmount == 1,
        "discarding a stack opens quantity input at one");
  bagTap(315, 244);
  check(bagDiscardAmount == 2,
        "the quantity stepper increases without exceeding the stack");
  bagTap(233, 308);
  check(bagView == BAG_VIEW_CONFIRM,
        "accepting a quantity asks for destructive confirmation");
  bagTap(233, 242);
  check(bagView == BAG_VIEW_LIST && inventory.count(usable->key) == 1,
        "confirming removes exactly the requested quantity");

  bagTap(200, 105);
  bagTap(200, 288);
  check(bagView == BAG_VIEW_CONFIRM && bagDiscardAmount == 1,
        "discarding a single item skips quantity input but still confirms");
  bagTap(233, 304);
  check(bagView == BAG_VIEW_ACTIONS && inventory.count(usable->key) == 1,
        "cancelling confirmation preserves the item stack");

  std::printf("%s\n", bad ? "FAILURES" : "bag UI behavior passes");
  return bad ? 1 : 0;
}
