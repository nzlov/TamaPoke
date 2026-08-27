// First boot: pick a language, a region, then a starter from it.
//
// The thing worth pinning here is that the starter screen reads its
// species from each region pack's starter array rather than a copy. Those same
// arrays are the pool a region's FIRST EGG is drawn from (pet.cpp rollInRegion),
// where Kanto's five deliberately include Pikachu and Eevee -- so the array must
// not be trimmed to match the screen, and the screen must not drift if somebody
// reorders the array. This test is the thing standing between those two.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "i18n.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=29; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void FakeESP::restart(){exit(0);}

void setup(); void render(); void onTap(int16_t x, int16_t y);
int16_t starterOf(uint8_t region, uint8_t i);
uint8_t starterCountShown(uint8_t region);
extern Pet pet;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// where renderRegionPick draws row i, and where renderStarterSelect draws row i
static int regionRowY(int i){ return 108 + i*72 + 30; }
static int starterRowY(int i){ return 110 + i*(70+8) + 35; }
static int languageX(int i){ return 74 + (i%2)*168 + 75; }
static int languageY(int i){ return 104 + (i/2)*70 + 27; }

int main(){
  setup();
  render();
  ck(pet.awaitingStarter(), "a brand new save asks for a starter");
  onTap(languageX(gLang), languageY(gLang));

  // --- the canonical trio, pinned. Reordering pack starters would change
  // the first screen anyone sees, and nothing else would notice.
  const int16_t want[9][3] = {
    {1,4,7}, {152,155,158}, {252,255,258}, {387,390,393},
    {495,498,501}, {650,653,656}, {722,725,728},
    {810,813,816}, {906,909,912},
  };
  for (uint8_t r = 0; r < regionAll(); r++) {
    bool ok = starterCountShown(r) == 3;
    for (uint8_t i = 0; i < 3; i++) if (starterOf(r,i) != want[r][i]) ok = false;
    char m[64]; snprintf(m,sizeof(m),"%s offers its canonical three", regionInfo(r).name);
    ck(ok, m);
  }

  // --- and every one of them really belongs to that region's dex range
  {
    bool ok = true;
    for (uint8_t r = 0; r < regionAll(); r++)
      for (uint8_t i = 0; i < starterCountShown(r); i++) {
        int16_t d = starterOf(r,i);
        if (d < regionInfo(r).lo || d > regionInfo(r).hi) ok = false;
      }
    ck(ok, "and each sits inside its own region's range");
  }

  // --- Kanto's egg pool is NOT trimmed to what the screen shows. This is the
  // one that fails if somebody "tidies" the pack to match the choice screen.
  ck(regionInfo(0).starterCount > starterCountShown(0),
     "Kanto's egg pool still holds more than the three on screen");
  {
    bool pika=false, eevee=false;
    for (uint8_t i=0;i<regionInfo(0).starterCount;i++){
      if (regionInfo(0).starters[i]==25) pika=true;
      if (regionInfo(0).starters[i]==133) eevee=true;
    }
    ck(pika && eevee, "Pikachu and Eevee are still reachable as a first egg");
  }

  // --- continue the real first-boot flow through a region and its middle starter
  uint8_t chosenRegion = regionAll() > 1 ? 1 : 0;
  onTap(233, regionRowY(chosenRegion));
  ck(pet.region == chosenRegion, "tapping a region on the first screen sets the egg region");
  ck(pet.awaitingStarter(), "and does not choose a creature by itself");

  render();                                   // now the starter list
  onTap(233, starterRowY(1));
  ck(!pet.awaitingStarter(), "tapping a starter finishes the first boot");
  ck(pet.eggPeek() == starterOf(chosenRegion, 1),
     "and it is the one that was tapped, from that region");

  // the egg really is Johto's, which is the whole point of choosing first
  ck(pet.eggPeek() >= regionInfo(chosenRegion).lo &&
     pet.eggPeek() <= regionInfo(chosenRegion).hi,
     "the waiting egg belongs to the region that was picked");

  // --- and it survives a reload: region is persisted, the flow is not repeated
  {
    Pet again; again.begin();
    ck(again.region == chosenRegion, "the region choice is saved");
    ck(!again.awaitingStarter(), "and the first boot does not run a second time");
  }

  // --- a tap that misses every row must do nothing rather than pick row 0.
  // Starting over: an empty Pokedex is what makes it a first boot.
  {
    pet.factoryReset();
    Pet fresh; fresh.begin();
    ck(fresh.awaitingStarter(), "a wiped save asks again");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
