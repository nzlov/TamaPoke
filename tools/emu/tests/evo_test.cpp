// Walks one real life: hatch a Charmander, age it, evolve at each gate, and
// print the moveset at every step. Answers what actually happens to moves
// across an evolution, rather than assuming.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "dex.h"
#include "moves.h"
#include <cstdio>

uint32_t g_seed = 0xC0FFEE;
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

static void show(Pet &p, const char *when) {
  printf("%-22s %-11s L%-3u :", when, dexEntry(p.speciesId).name, p.level());
  for (int i = 0; i < MOVE_SLOTS; i++)
    printf(" %s", p.moves[i] ? moveEntry(p.moves[i]).name : "-");
  printf(" | reserve:");
  for (int i = 0; i < RESERVE_MOVE_SLOTS; i++)
    printf(" %s", p.reserveMoves[i] ? moveEntry(p.reserveMoves[i]).name : "-");
  printf("\n");
}

// what the species could learn by this level but does not know
static void pending(Pet &p) {
  uint16_t n = learnCount(p.speciesId);
  uint8_t miss = 0;
  char buf[240];
  buf[0] = 0;
  for (uint16_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(p.speciesId, i);
    if (learnMethod(p.speciesId, i) != LM_LEVEL_UP || at > p.level()) continue;
    MoveId mv = learnMove(p.speciesId, i);
    if (p.knowsMove(mv)) continue;
    miss++;
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s@%u", moveEntry(mv).name, at);
  }
  if (miss) printf("%-22s   MISSED level-up moves:%s\n", "", buf);
}

int main() {
  Pet p;
  p.dbgHatchAs(4, false);          // Charmander, the form that actually hatches
  p.ageMinutes = 0;
  show(p, "hatched");

  // healthy pet, so evolution is never blocked by low stats
  auto healthy = [&] { p.fullness = p.joy = p.energy = p.hygiene = 100; };
  for (uint32_t lvl : { 15u, 16u, 35u, 36u, 54u, 73u, 100u }) {
    p.ageMinutes = (lvl - 1) * MINUTES_PER_LEVEL;
    healthy();
    const char *note = "";
    if (p.canEvolveNow()) { p.evolve(); note = " (evolved)"; }
    p.checkLearnGates();
    char when[40];
    snprintf(when, sizeof(when), "level %u%s", lvl, note);
    show(p, when);
    pending(p);
  }

  Pet branch;
  branch.dbgHatchAs(133, false);
  branch.ageMinutes = 29UL * MINUTES_PER_LEVEL;
  branch.fullness = branch.joy = branch.energy = branch.hygiene = 100;
  bool ready = branch.canEvolveNow();
  branch.evolve();
  bool validBranch = false;
  for (uint8_t i = 0; i < evolutionCount(133); i++)
    if (branch.speciesId == evolutionTarget(133, i)) validBranch = true;
  printf("%s  branching evolution selects a target supplied by the pack\n",
         ready && validBranch ? "PASS" : "FAIL");
  return ready && validBranch ? 0 : 1;
}
