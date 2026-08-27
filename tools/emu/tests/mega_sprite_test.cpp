#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "content.h"
#include "gender.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 73;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

static int failures = 0;
static void ck(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

int main() {
  contentBegin();
  const MegaFormEntry *charizardX = megaFormFor(6, MEGA_FORM_X);
  const MegaFormEntry *charizardY = megaFormFor(6, MEGA_FORM_Y);
  ck(charizardX && charizardX->spriteSize,
     "the Kanto region pack provides Mega Charizard X art");
  ck(charizardY && !charizardY->spriteSize,
     "Mega Charizard Y is represented even when upstream art is unavailable");

  PmdMon normal, megaX;
  bool normalOk = normal.load(6, false, GENDER_NONE, false);
  bool megaOk = megaX.load(6, false, GENDER_NONE, true, MEGA_FORM_X);
  ck(normalOk && megaOk, "base and Mega Charizard X sprites both load");
  bool same = normalOk && megaOk && normal.palCount == megaX.palCount &&
              !std::memcmp(normal.pal, megaX.pal,
                           normal.palCount * sizeof(uint16_t));
  ck(!same, "Mega Charizard X uses distinct form artwork");
  ck(megaOk && megaX.has(pmdFacingAction(PMD_IDLE, true)) &&
     megaX.has(pmdFacingAction(PMD_HURT, true)) &&
     megaX.has(pmdFacingAction(PMD_ATTACK, true)),
     "available Mega art includes player-facing-back battle actions");
  normal.unload(); megaX.unload();

  PmdMon charizardNormal, charizardYFallback;
  bool yFallbackOk = charizardNormal.load(6, false, GENDER_NONE, false) &&
                     charizardYFallback.load(6, false, GENDER_NONE, true,
                                             MEGA_FORM_Y);
  bool yFallbackSame = yFallbackOk &&
      charizardNormal.palCount == charizardYFallback.palCount &&
      !std::memcmp(charizardNormal.pal, charizardYFallback.pal,
                   charizardNormal.palCount * sizeof(uint16_t));
  ck(yFallbackSame, "a missing Mega Y sprite safely falls back to base artwork");
  charizardNormal.unload(); charizardYFallback.unload();

  PmdMon charizardBaseShiny, charizardXMegaNormal, charizardXShinyFallback;
  bool shinyFallbackOk = charizardBaseShiny.load(6, true, GENDER_NONE, false) &&
      charizardXMegaNormal.load(6, false, GENDER_NONE, true, MEGA_FORM_X) &&
      charizardXShinyFallback.load(6, true, GENDER_NONE, true, MEGA_FORM_X);
  bool shinyFallbackSame = shinyFallbackOk &&
      charizardBaseShiny.palCount == charizardXShinyFallback.palCount &&
      !std::memcmp(charizardBaseShiny.pal, charizardXShinyFallback.pal,
                   charizardBaseShiny.palCount * sizeof(uint16_t));
  bool fellBackToNormalMega = shinyFallbackOk &&
      charizardXMegaNormal.palCount == charizardXShinyFallback.palCount &&
      !std::memcmp(charizardXMegaNormal.pal, charizardXShinyFallback.pal,
                   charizardXMegaNormal.palCount * sizeof(uint16_t));
  ck(shinyFallbackSame && !fellBackToNormalMega,
     "a missing shiny Mega sprite preserves the base shiny appearance");
  charizardBaseShiny.unload(); charizardXMegaNormal.unload();
  charizardXShinyFallback.unload();

  PmdMon venusaurNormal, venusaurFallback;
  bool fallbackOk = venusaurNormal.load(3, false, GENDER_NONE, false) &&
                    venusaurFallback.load(3, false, GENDER_NONE, true,
                                          MEGA_FORM_STANDARD);
  bool fallbackSame = fallbackOk &&
      venusaurNormal.palCount == venusaurFallback.palCount &&
      !std::memcmp(venusaurNormal.pal, venusaurFallback.pal,
                   venusaurNormal.palCount * sizeof(uint16_t));
  ck(fallbackSame, "a missing Mega form safely falls back to base artwork");
  venusaurNormal.unload(); venusaurFallback.unload();

  PmdMon alakazamNormal, alakazamShiny;
  bool shinyOk = alakazamNormal.load(65, false, GENDER_NONE, true,
                                     MEGA_FORM_STANDARD) &&
                 alakazamShiny.load(65, true, GENDER_NONE, true,
                                    MEGA_FORM_STANDARD);
  bool shinySame = shinyOk && alakazamNormal.palCount == alakazamShiny.palCount &&
      !std::memcmp(alakazamNormal.pal, alakazamShiny.pal,
                   alakazamNormal.palCount * sizeof(uint16_t));
  ck(shinyOk && !shinySame, "available shiny Mega artwork is loaded independently");
  alakazamNormal.unload(); alakazamShiny.unload();

  return failures ? 1 : 0;
}
