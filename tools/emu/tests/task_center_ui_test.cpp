#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "content.h"
#include "inventory.h"
#include "party.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 0x7A5Cu;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void renderTaskCenter();
void taskTap(int16_t x, int16_t y);
void onSwipe(int dir);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool taskOpen;
extern uint8_t taskView, taskPage, taskSelected, taskDetailPage;
extern ItemRef taskRewardItems[3];
extern uint8_t taskRewardItemCount;

static int bad = 0;
static void check(bool ok, const char *message) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message);
  if (!ok) bad++;
}

static PartyMon mon(SpeciesId species, uint16_t level) {
  PartyMon value;
  value.dex = species;
  value.level = level;
  value.ageMinutes = (uint32_t)(level - 1) * MINUTES_PER_LEVEL;
  value.nature = NATURE_HARDY;
  value.stateVersion = 6;
  value.ivAtk = value.ivDef = value.ivSpe = value.ivHp = 20;
  value.setAbilitySlot(ABILITY_SLOT_ONE);
  return value;
}

int main() {
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(1);
  if (pet.isEgg()) pet.dbgHatchAs(1, false);
  party.slots[0] = mon(1, 30);
  party.slots[1] = mon(1, 10);
  party.box[0] = mon(1, 55);
  pet.importState(party.slots[0]);
  player.dailyTasks.day = 10;
  player.dailyTasks.entries[0].species = 1;
  taskOpen = true;
  taskView = 0;

  renderTaskCenter();
  check(gfx->frameReady, "the task list renders a complete frame");
  taskTap(200, 110);
  check(taskView == 1, "tapping a task opens the individual picker");
  taskTap(100, 110);
  check(taskView == 2 && taskSelected == 0 && party.slots[0].dex == 1,
        "choosing one of duplicate species opens View and Submit without removing it");
  taskTap(233, 190);
  check(taskView == 3 && taskDetailPage == 0,
        "View opens the selected individual rather than a generic species card");
  onSwipe(-1);
  check(taskView == 3 && taskDetailPage == 1,
        "the selected individual detail pages horizontally");
  taskTap(233, 400);
  taskTap(233, 256);
  check(taskView == 4 && party.slots[0].dex == 1,
        "Submit opens confirmation before permanent handover");
  taskTap(233, 270);
  check(player.dailyTasks.entries[0].completed && party.slots[0].empty() &&
        party.slots[1].dex == 1,
        "confirmation removes exactly the selected duplicate and completes the task");
  check(taskView == 5 && taskRewardItemCount >= 2 && taskRewardItemCount <= 3,
        "a level ten above the pre-submit average receives hard drop count");
  bool rarityOk = true, stonesAttributed = true;
  for (uint8_t i = 0; i < taskRewardItemCount; i++) {
    const ItemEntry *item = itemByKey(taskRewardItems[i].key);
    rarityOk = rarityOk && item && item->rarity >= 2;
    if (item && item->effect == ITEM_EFFECT_TEACH_MOVE)
      stonesAttributed = stonesAttributed && moveValid(taskRewardItems[i].move);
  }
  check(rarityOk, "hard task rewards only contain rarity-two-or-higher items");
  check(stonesAttributed, "every rewarded move stone carries a random valid move");
  return bad ? 1 : 0;
}
