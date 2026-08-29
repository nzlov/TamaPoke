#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "content.h"
#include "inventory.h"
#include "items.h"
#include "party.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 0x5700E;
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
extern Arduino_Canvas *gfx;
extern Pet pet;
extern Party party;
extern Inventory inventory;
extern bool bagOpen;
extern ItemKey bagSelectedKey, bagDetailKey;
extern MoveId bagSelectedMove, bagDetailMove;
extern uint8_t bagView;
enum BagStoneDialog : uint8_t {
  BAG_STONE_DIALOG_NONE = 0,
  BAG_STONE_DIALOG_CONFIRM,
  BAG_STONE_DIALOG_INCOMPATIBLE,
  BAG_STONE_DIALOG_KNOWN,
};
extern BagStoneDialog bagStoneDialog;
extern uint8_t bagStoneTarget;

static int bad = 0;
static void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static const ItemEntry *moveStone() {
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->effect == ITEM_EFFECT_TEACH_MOVE) return item;
  }
  return nullptr;
}

static MoveId findMove(bool compatible) {
  for (MoveId move = 1; move < moveCount(); move++)
    if (speciesCanLearnMove(pet.speciesId, move) == compatible &&
        (!compatible || !pet.knowsMove(move))) return move;
  return MOVE_NONE;
}

static void openStone(const ItemEntry &stone, MoveId move) {
  inventory.add(stone.key, 1, move);
  bagSelectedKey = stone.key;
  bagSelectedMove = move;
  bagDetailKey = ITEM_KEY_NONE;
  bagDetailMove = MOVE_NONE;
  bagStoneDialog = BAG_STONE_DIALOG_NONE;
  bagStoneTarget = PARTY_SLOTS;
  bagView = 1;
  bagOpen = true;
}

static void tapSlot(uint8_t slot) {
  bagTap(78 + (slot % 2) * 160 + 75, 82 + (slot / 2) * 84 + 35);
}

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  pet.dbgHatchAs(6, false);
  const ItemEntry *stone = moveStone();
  check(stone != nullptr, "the bag catalogue contains one move-stone item");
  if (!stone) return 1;

  MoveId compatible = findMove(true);
  check(moveValid(compatible), "the fixture finds a compatible unlearned move");
  if (!moveValid(compatible)) return 1;
  openStone(*stone, compatible);
  bagTap(233, 230);
  check(bagView == 3 && bagStoneDialog == BAG_STONE_DIALOG_NONE,
        "using a move stone opens the six-slot pet chooser");
  gfx->frameReady = false;
  render();
  check(gfx->frameReady, "the move-stone pet chooser flushes to the panel");
  tapSlot(party.activeIndex());
  check(bagStoneDialog == BAG_STONE_DIALOG_CONFIRM &&
        !pet.knowsMove(compatible) && inventory.count(stone->key, compatible) == 1,
        "using a compatible stone asks before learning or consuming it");
  gfx->frameReady = false;
  render();
  check(gfx->frameReady, "the learning confirmation popup flushes to the panel");
  bagTap(233, 242);
  check(pet.knowsMove(compatible) &&
        inventory.count(stone->key, compatible) == 0,
        "confirming teaches the move and consumes exactly that stone stack");

  uint8_t reserveSlot = party.activeIndex() == 0 ? 1 : 0;
  PartyMon reserve = pet.toPartyMon();
  for (MoveId &move : reserve.moves) move = MOVE_NONE;
  for (MoveId &move : reserve.reserveMoves) move = MOVE_NONE;
  party.replaceAt(reserveSlot, reserve);
  openStone(*stone, compatible);
  bagTap(233, 230);
  tapSlot(party.activeIndex());
  check(bagStoneDialog == BAG_STONE_DIALOG_KNOWN &&
        inventory.count(stone->key, compatible) == 1,
        "an already-known move opens an explanation and is not consumed");
  bagTap(233, 242);
  tapSlot(reserveSlot);
  check(bagStoneDialog == BAG_STONE_DIALOG_CONFIRM,
        "the same stone can target a compatible reserve cultivation pet");
  bagTap(233, 242);
  check(Pet::knowsLearnedMove(party.slots[reserveSlot].moves,
                              party.slots[reserveSlot].reserveMoves,
                              compatible) &&
        inventory.count(stone->key, compatible) == 0,
        "confirming teaches only the selected reserve pet and consumes the stone");

  MoveId incompatible = findMove(false);
  check(moveValid(incompatible), "the fixture finds an incompatible move");
  if (moveValid(incompatible)) {
    openStone(*stone, incompatible);
    bagTap(233, 230);
    tapSlot(party.activeIndex());
    check(bagStoneDialog == BAG_STONE_DIALOG_INCOMPATIBLE &&
          !pet.knowsMove(incompatible) &&
          inventory.count(stone->key, incompatible) == 1,
          "an incompatible stone explains the refusal without consuming it");
    gfx->frameReady = false;
    render();
    check(gfx->frameReady, "the incompatibility popup flushes to the panel");
    bagTap(233, 242);
    check(!pet.knowsMove(incompatible) &&
          inventory.count(stone->key, incompatible) == 1,
          "dismissing the refusal cannot teach or consume the move");
  }

  std::puts(bad ? "FAILURES" : "move-stone bag flow works");
  return bad ? 1 : 0;
}
