// Saves without the current schema marker are deliberately reset. Runtime
// packs widen species and move IDs, so interpreting an old raw struct would be
// less safe than starting a clean game.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 13;
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
  Preferences old;
  old.begin("tamapoke", false);
  old.clear();
  old.putBool("init", true);
  old.putShort("dexn", 59);
  old.putString("nick", "BLAZE");
  uint8_t oldMoves[MOVE_SLOTS] = { 1, 2, 3, 4 };
  old.putBytes("mvs", oldMoves, sizeof(oldMoves));
  PartyMon oldParty[PARTY_SLOTS] = {};
  oldParty[0].dex = 25;
  old.putBytes("party", oldParty, sizeof(oldParty));
  old.end();

  Pet pet;
  Party loadedParty;
  pet.begin();
  loadedParty.begin();

  ck(pet.isEgg(), "an unversioned save is reset to a new egg");
  ck(pet.awaitingStarter(), "the reset starts the normal starter flow");
  ck(loadedParty.count() == 0 && loadedParty.boxCount() == 0,
     "old party and box data are removed with the save");

  Preferences current;
  current.begin("tamapoke", true);
  char nickname[12] = {};
  current.getString("nick", nickname, sizeof(nickname));
  ck(current.getUShort("savev", 0) == SAVE_STATE_VERSION,
     "the clean save records the current schema");
  ck(strcmp(nickname, "BLAZE") != 0 && !current.isKey("party"),
     "unsupported contents do not survive the reset");
  current.end();

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
