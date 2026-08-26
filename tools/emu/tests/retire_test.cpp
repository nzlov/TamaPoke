// The creature menu has two mutually exclusive exits: a qualified final form
// says farewell, while every other eligible creature is simply released.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>

uint32_t g_seed = 41;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t gNow = 1;
uint32_t millis() { return gNow; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static void clearSave() {
  Preferences prefs;
  prefs.begin("tamapoke", false);
  prefs.clear();
  prefs.end();
}

static void ready(Pet &pet, Party &party, int16_t dex) {
  clearSave();
  pet.begin();
  pet.dbgHatchAs(dex, false);
  party.begin();
  party.attach(pet);
}

static void finish(Pet &pet) {
  gNow += CEREMONY_MS + 1;
  pet.update(gNow);
}

int main() {
  {
    Pet pet; Party party; ready(pet, party, 4);
    ck(pet.canExitNow(), "a normal awake creature can leave");
    ck(!pet.canFarewellNow(), "a non-final form cannot say farewell");
    pet.startFarewell();
    ck(pet.ceremony == CER_NONE, "an ineligible farewell cannot be forced");
    pet.release();
    ck(pet.ceremony == CER_RELEASE, "the menu exit for it is release");
    finish(pet);
    ck(pet.isEgg(), "an otherwise empty roster receives one safety egg");
  }

  {
    Pet pet; Party party; ready(pet, party, 6);
    pet.ageMinutes = FAREWELL_AGE_MIN * 2;
    pet.raisedMinutes = FAREWELL_AGE_MIN - 1;
    ck(!pet.canFarewellNow(), "age alone cannot qualify a final form");
    pet.raisedMinutes++;
    ck(pet.canFarewellNow(), "three raised days qualify a final form");
    pet.release();
    ck(pet.ceremony == CER_NONE, "a qualified creature cannot bypass farewell as release");
    pet.startFarewell();
    ck(pet.ceremony == CER_FAREWELL, "the qualified menu exit is farewell");
  }

  {
    Pet pet; Party party; ready(pet, party, 4);
    pet.sleeping = true;
    ck(!pet.canExitNow(), "a sleeping creature cannot leave");
    pet.sleeping = false;
    pet.frozen = true;
    ck(!pet.canExitNow(), "a Box-compatibility frozen creature cannot leave");
    pet.frozen = false;
    pet.newEgg();
    ck(!pet.canExitNow(), "an egg cannot leave");
  }

  std::printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
