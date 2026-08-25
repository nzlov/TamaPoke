#include "Arduino.h"
#include "Preferences.h"
#include "party.h"
#include <cstdio>

uint32_t g_seed = 97;
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

int main() {
  Party storage;
  PartyMon caught;
  caught.dex = 1;
  if (storage.store(caught) != PARTY_STORE_PARTY || storage.slots[0].dex != 1) {
    std::puts("FAIL party-first storage");
    return 1;
  }
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) storage.slots[i].dex = (SpeciesId)(i + 1);
  caught.dex = 25;
  if (storage.store(caught) != PARTY_STORE_BOX || storage.box[0].dex != 25) {
    std::puts("FAIL box fallback");
    return 1;
  }
  for (uint8_t i = 0; i < BOX_SLOTS; i++) storage.box[i].dex = (SpeciesId)(i + 1);
  caught.dex = 150;
  if (storage.store(caught) != PARTY_STORE_FULL) {
    std::puts("FAIL full storage signal");
    return 1;
  }
  std::puts("PASS capture stores party, then box, then requests replacement");
  return 0;
}
