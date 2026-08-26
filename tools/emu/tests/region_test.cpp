// Choosing which generation your egg comes from.
//
// The species is rolled when the egg APPEARS, not when it cracks, so changing
// region has to move the waiting egg or the setting would look broken -- you
// would pick Johto and still hatch a Rattata. That makes the interesting
// question not "does it work" but "can it be farmed", and these are the two
// rules that stop it:
//
//   1. The rarity tier the egg was granted is kept; only which species of that
//      tier changes. So flipping region cannot fish for a legendary.
//   2. Each region's answer is REMEMBERED for the current egg, so switching
//      back shows the same creature. There is nothing to gain by flipping.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <set>
uint32_t g_seed=23; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// a pet with enough of the dex seen that the lottery is past the starter case
static void seed(Pet &p, int upto=60){
  p.begin();
  for (int d=1; d<=upto; d++) p.dbgHatchAs(d,false);
}

int main(){
  // --- the lottery stays inside the chosen region
  {
    Pet p; seed(p);
    for (uint8_t r=0; r<regionCount(); r++){
      p.setRegion(r);
      const RegionInfo &ri = regionInfo(r);
      int out=0;
      for (int i=0;i<400;i++){
        int16_t d = p.pickEggSpecies();
        if (d < ri.lo || d > ri.hi || !spriteAvailable(d)) out++;
      }
      char msg[80];
      snprintf(msg,sizeof(msg),"%s eggs stay in range and have art", ri.name);
      ck(out==0, msg);
    }
  }

  // --- ALL really does span everything
  {
    Pet p; seed(p);
    p.setRegion(regionAll());
    bool seen[CONTENT_MAX_REGIONS]={false};
    for (int i=0;i<3600;i++){
      int16_t d = p.pickEggSpecies();
      uint8_t region = regionOfDex(d);
      if (region < regionAll()) seen[region] = true;
    }
    bool allSeen = true;
    for (uint8_t region=0; region<regionAll(); region++)
      if (!seen[region]) allSeen = false;
    ck(allSeen, "ALL draws from every installed region");
  }

  // --- a first egg gives a starter from the chosen region
  {
    for (uint8_t r=0; r<regionCount(); r++){
      Pet p; p.begin(); p.factoryReset(); p.begin();
      p.setRegion(r);
      const RegionInfo &ri = regionInfo(r);
      bool ok=true;
      for (int i=0;i<60;i++){
        int16_t d = p.pickEggSpecies();
        bool found=false;
        for (uint8_t k=0;k<ri.starterCount;k++) if (ri.starters[k]==d) found=true;
        if (!found) ok=false;
      }
      char msg[72];
      snprintf(msg,sizeof(msg),"a first egg in %s is one of its starters", ri.name);
      ck(ok, msg);
    }
  }

  // --- switching region moves the WAITING egg
  {
    Pet p; seed(p);
    p.setRegion(0);
    p.newEgg();
    ck(p.isEgg(), "an egg is waiting");
    const RegionInfo &first = regionInfo(0);
    ck(p.eggPeek() >= first.lo && p.eggPeek() <= first.hi,
       "and it belongs to the selected installed region");
    bool moved = true;
    for (uint8_t r = 1; r < regionAll(); r++) {
      p.setRegion(r);
      const RegionInfo &ri = regionInfo(r);
      if (p.eggPeek() < ri.lo || p.eggPeek() > ri.hi) moved = false;
    }
    ck(moved, "switching moves the egg through every installed region");
  }

  // --- RULE 1: the rarity it was granted is kept
  {
    int checked=0, kept=0;
    for (int trial=0; trial<40; trial++){
      Pet p; seed(p);
      p.setRegion(0);
      p.newEgg();
      uint8_t tier = p.eggRarity();
      for (uint8_t r=1; r<regionCount(); r++){
        p.setRegion(r);
        checked++;
        if (p.eggRarity()==tier) kept++;
      }
    }
    ck(checked==kept, "a region change never changes the rarity tier");
    printf("      (%d switches, %d kept their tier)\n", checked, kept);
  }

  // --- RULE 2: each region is remembered, so flipping is not a re-roll
  {
    Pet p; seed(p);
    p.setRegion(0);
    p.newEgg();
    int16_t first[regionCount()];
    for (uint8_t r=0;r<regionCount();r++){ p.setRegion(r); first[r]=p.eggPeek(); }
    bool stable=true;
    std::set<int16_t> everSeen[regionCount()];
    for (int lap=0; lap<25; lap++)
      for (uint8_t r=0;r<regionCount();r++){
        p.setRegion(r);
        everSeen[r].insert(p.eggPeek());
        if (p.eggPeek() != first[r]) stable=false;
      }
    ck(stable, "flipping between regions always returns the same creature");
    size_t worst=0;
    for (uint8_t r=0;r<regionCount();r++) worst = worst > everSeen[r].size() ? worst : everSeen[r].size();
    ck(worst==1, "so 100 switches yield at most one candidate per region");
  }

  // --- a hatched creature is never touched
  {
    Pet p; seed(p);
    p.dbgHatchAs(6,false);
    int16_t was = p.speciesId;
    p.setRegion(1);
    p.setRegion(2);
    ck(p.speciesId==was, "changing region does not transform a living creature");
  }

  // --- a new egg forgets the old one's candidates
  {
    Pet p; seed(p);
    p.setRegion(0);
    p.newEgg();
    uint8_t other = regionAll() > 1 ? 1 : 0;
    p.setRegion(other);
    int16_t before = p.eggPeek();
    p.newEgg();
    p.setRegion(0);
    p.setRegion(other);
    // it may coincide by chance, but the memo must have been cleared: check the
    // stored table rather than the outcome
    bool cleared = true;
    for (uint8_t r = 0; r < regionCount(); r++)
      if (r != p.region && p.eggByRegion[r] != 0) cleared = false;
    ck(cleared, "a new egg clears what the old one would have been");
    (void)before;
  }

  // --- it survives a reload, like every other player-wide setting
  {
    Pet p; seed(p);
    uint8_t selected = regionAll() > 1 ? 1 : 0;
    p.setRegion(selected);
    p.newEgg();
    int16_t t = p.eggPeek();
    Pet q; q.begin();
    ck(q.region==selected, "the region persists");
    ck(q.eggPeek()==t, "and so does the egg it chose");
    q.setRegion(0);
    Pet r2; r2.begin();
    ck(r2.region==0, "and a change persists too");
  }

  // --- an out-of-range region in a save cannot index the table
  {
    Pet p; seed(p);
    p.setRegion(1);
    p.region = 99;               // as a corrupt or newer save might have it
    p.setRegion(99);             // must normalise rather than read off the end
    ck(p.region < regionCount(), "an impossible region is brought back in range");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
