// Profile interactions use the real sketch dispatcher: the creature name is
// display-only, while tapping nature opens a modal explanation on the card.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 11;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void onTap(int16_t x, int16_t y);
void render();
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool cardOpen, kbOpen, natureInfoOpen;
extern uint8_t cardPage;

int main() {
  setup();
  pet.dbgHatchAs(6, false);

  constexpr int badgeX = 318, badgeY = 86, badgeW = 78, badgeH = 28;
  uint16_t withoutFactor[badgeW * badgeH];
  cardOpen = true;
  cardPage = 0;
  pet.gigantamaxFactor = false;
  render();
  for (int y = 0; y < badgeH; y++)
    std::memcpy(withoutFactor + y * badgeW,
                gfx->buffer() + (badgeY + y) * 466 + badgeX,
                badgeW * sizeof(uint16_t));

  pet.gigantamaxFactor = true;
  render();
  int changed = 0;
  for (int y = 0; y < badgeH; y++)
    for (int x = 0; x < badgeW; x++)
      if (gfx->buffer()[(badgeY + y) * 466 + badgeX + x] !=
          withoutFactor[y * badgeW + x]) changed++;
  if (changed < badgeW * badgeH / 2) {
    printf("FAIL: the profile does not visibly mark the Gigantamax Factor\n");
    return 1;
  }
  printf("PASS: the profile visibly marks the Gigantamax Factor\n");

  pet.gigantamaxFactor = false;

  cardOpen = true;
  cardPage = 0;
  kbOpen = false;
  onTap(233, 50);
  if (kbOpen || cardOpen) {
    printf("FAIL: tapping the profile name still opens the rename keyboard\n");
    return 1;
  }
  printf("PASS: tapping the profile name leaves rename unavailable\n");

  cardOpen = true;
  onTap(233, 328);
  if (!cardOpen || kbOpen || !natureInfoOpen) {
    printf("FAIL: tapping nature did not keep the profile open for its explanation\n");
    return 1;
  }
  printf("PASS: tapping nature opens its explanation on the profile\n");

  onTap(233, 220);
  if (!cardOpen || natureInfoOpen) {
    printf("FAIL: dismissing the nature explanation closed the profile too\n");
    return 1;
  }
  printf("PASS: the nature explanation dismisses back to the profile\n");
  return 0;
}
