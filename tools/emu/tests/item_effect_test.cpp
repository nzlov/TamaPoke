#include "Arduino.h"
#include "Preferences.h"
#include <cstdio>
#include "content.h"
#include "items.h"
#include "inventory.h"
#include "battle.h"

uint32_t g_seed = 41;
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

static int fails = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)

static const ItemEntry *effect(uint8_t opcode) {
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->effect == opcode) return item;
  }
  return nullptr;
}

int main() {
  contentBegin();
  CHECK(contentHasMoves());
  const ItemEntry *heal = effect(ITEM_EFFECT_HEAL_HP);
  const ItemEntry *cure = effect(ITEM_EFFECT_CURE_STATUS);
  const ItemEntry *revive = effect(ITEM_EFFECT_REVIVE);
  const ItemEntry *training = effect(ITEM_EFFECT_TRAINING_FLOOR);
  const ItemEntry *boost = effect(ITEM_EFFECT_BATTLE_STAGE);
  CHECK(heal && cure && revive && training && boost);
  if (!heal || !cure || !revive || !training || !boost) return 1;

  Combatant target;
  target.maxHp = 100;
  target.hp = 10;
  Inventory warehouse;
  warehouse.begin();
  CHECK(warehouse.add(heal->key, 2));
  uint8_t stored = warehouse.count(heal->key);
  CHECK(itemApplyToCombatant(*heal, target));
  CHECK(warehouse.consume(heal->key));
  CHECK(warehouse.count(heal->key) == stored - 1);
  CHECK(target.hp > 10 && target.hp <= target.maxHp);

  target.ailment = AIL_BURN;
  target.ailTurns = 3;
  target.confuseTurns = 2;
  CHECK(itemApplyToCombatant(*cure, target));
  CHECK(target.ailment == AIL_NONE && !target.ailTurns && !target.confuseTurns);

  target.hp = 0;
  target.ailment = AIL_POISON;
  target.stage[SI_ATK] = 4;
  CHECK(itemApplyToCombatant(*revive, target));
  CHECK(target.hp > 0 && target.hp <= target.maxHp);
  CHECK(target.ailment == AIL_NONE && target.stage[SI_ATK] == 0);

  const ItemEntry *catchItem = effect(ITEM_EFFECT_CATCH);
  CHECK(catchItem && !itemApplyToCombatant(*catchItem, target));

  Pet pet;
  pet.speciesId = 6;
  pet.ivAtk = pet.ivDef = pet.ivSpe = 31;
  pet.trAtk = pet.trDef = pet.trSpe = 0;
  CHECK(itemCanApplyToPet(*training, pet));
  CHECK(itemApplyToPet(*training, pet));
  CHECK(pet.trMinAtk == 10 && pet.trAtk == 10);
  while (itemApplyToPet(*training, pet)) {}
  CHECK(pet.trMinAtk == pet.trMaxAtk());
  CHECK(!itemCanApplyToPet(*training, pet));

  Combatant boosted;
  boosted.maxHp = boosted.hp = 100;
  CHECK(itemCanApplyToCombatant(*boost, boosted));
  CHECK(itemApplyToCombatant(*boost, boosted));
  uint8_t boostedStat = 0xFF;
  for (uint8_t i = 0; i < SI_COUNT; i++)
    if (boost->flags & (1u << i)) boostedStat = i;
  CHECK(boostedStat < SI_COUNT && boosted.stage[boostedStat] == 1);
  while (itemApplyToCombatant(*boost, boosted)) {}
  CHECK(boosted.stage[boostedStat] == 6);
  CHECK(!itemCanApplyToCombatant(*boost, boosted));
  Combatant nextBattle;
  CHECK(nextBattle.stage[boostedStat] == 0);
  if (fails) return 1;
  std::puts("PASS generic item effects");
  return 0;
}
