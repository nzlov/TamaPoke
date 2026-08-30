// Unsupported unversioned saves reset, while the supported v1 scalar layout is
// consumed exactly once and replaced by the canonical roster snapshot.
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
  loadedParty.attach(pet);

  Preferences current;
  current.begin("tamapoke", true);
  char nickname[12] = {};
  current.getString("nick", nickname, sizeof(nickname));
  ck(current.getUShort("savev", 0) == SAVE_STATE_VERSION,
     "the clean save records the current schema");
  ck(strcmp(nickname, "BLAZE") != 0 && !current.isKey("party"),
     "unsupported contents do not survive the reset");
  current.end();

  old.begin("tamapoke", false);
  old.clear();
  old.putUShort("savev", SAVE_STATE_VERSION_LEGACY);
  old.putBool("init", true);
  old.putShort("dexn", 59);
  old.putString("nick", "BLAZE");
  old.putUShort("badg", 0x0005);
  old.end();

  Pet migratedPet;
  Party migratedParty;
  migratedPet.begin();
  migratedParty.begin();
  migratedParty.attach(migratedPet);
  ck(migratedPet.speciesId == 59 && !strcmp(migratedPet.nick, "BLAZE") &&
         migratedPet.playerProgress().badges == 0x0005,
     "schema v1 scalar data migrates into the current roster");

  current.begin("tamapoke", true);
  ck(current.getUShort("savev", 0) == SAVE_STATE_VERSION &&
         current.getUShort("rostv", 0) == 7 && current.isKey("team2"),
     "migration commits the current schema and roster snapshot");
  ck(!current.isKey("init") && !current.isKey("dexn") &&
         !current.isKey("nick") && !current.isKey("badg") &&
         !current.isKey("party"),
     "migration removes legacy scalar and roster keys");
  current.end();

  migratedPet.playerProgress().renameTrainer("CURRENT");
  current.begin("tamapoke", true);
  ck(!current.isKey("tnam") && current.isKey("team2"),
     "current player saves update only the canonical snapshot");
  current.end();

  Pet reloadedPet;
  Party reloadedParty;
  reloadedPet.begin();
  reloadedParty.begin();
  reloadedParty.attach(reloadedPet);
  ck(!strcmp(reloadedPet.playerProgress().trainerName, "CURRENT") &&
         reloadedPet.speciesId == 59,
     "the migrated current snapshot reloads without legacy keys");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
