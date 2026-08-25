// The regional challenge ladders as DATA. Every entry is hand-authored, so the failure
// mode is a typo that puts a creature outside the dex or a level outside the
// curve -- and a bad dex number must be rejected before a runtime lookup.
//
// Kanto was checked by hand against FireRed/LeafGreen; Johto and Hoenn are
// checked by tools/verify_rosters.py against pokecrystal and pokeemerald. The
// later ladders target their selected games, with Alola and Galar mapped onto
// the same 8+4+champion runtime progression.
//
// This test is the cheap always-on half: it cannot know what is canonical, but
// it catches the mistakes that would crash or unbalance the game.
#include "Arduino.h"
#include "dex.h"
#include "trainers.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=1; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  const uint8_t regions = regionAll();
  ck(regions > 0 && regions + 1 == regionCount(),
     "real regions are followed by one derived ALL entry");
  {
    int unnamed = 0;
    for (int r = 0; r < regions; r++)
      if (!regionBattleAvailable(r) || !regionInfo(r).name ||
          !regionInfo(r).name[0] || !regionBattleInfo(r).trainerCount) unnamed++;
    ck(unnamed == 0, "every installed region has a named battle roster");
  }

  for (int r=0; r<regions; r++){
    const RegionBattleInfo &battle = regionBattleInfo(r);
    const char *region = regionInfo(r).name;
    int oob=0, badLvl=0, badCount=0, noName=0;
    uint8_t prevTop=0;
    int outOfRegion=0;
    for (int i=0;i<battle.trainerCount;i++){
      const Trainer &t = trainerInfo(r, i);
      if (!t.name || !t.name[0] || !t.place || !t.place[0]) noName++;
      if (t.count < 1 || t.count > TRAINER_TEAM_MAX) badCount++;
      uint8_t top=0;
      for (int k=0;k<t.count && k<TRAINER_TEAM_MAX;k++){
        const TrainerMon &m = t.team[k];
        // The one that would otherwise index beyond the loaded catalogue.
        if (m.dex < 1 || m.dex > dexCount()) { oob++; continue; }
        if (m.level < 1 || m.level > MAX_TRAINER_LEVEL) badLvl++;
        if (m.level > top) top = m.level;
      }
      // The ladder is sequential and level-capped to the leader's best, so a
      // later leader being far WEAKER would make the run go backwards. Johto's
      // Pryce really is below Jasmine in Gold/Silver, so a small dip is allowed
      // and only a collapse is a failure.
      if (top + 8 < prevTop) outOfRegion++;
      if (top > prevTop) prevTop = top;
    }
    char m[96];
    snprintf(m,sizeof(m),"%s: every creature is a real dex number", region);
    ck(oob==0, m);
    snprintf(m,sizeof(m),"%s: every level is inside the curve", region);
    ck(badLvl==0, m);
    snprintf(m,sizeof(m),"%s: every team has 1..%d members", region, TRAINER_TEAM_MAX);
    ck(badCount==0, m);
    snprintf(m,sizeof(m),"%s: every trainer is named and placed", region);
    ck(noName==0, m);
    snprintf(m,sizeof(m),"%s: the ladder never collapses in difficulty", region);
    ck(outOfRegion==0, m);
  }

  // the champion should be the hardest thing in each ladder
  for (int r=0; r<regions; r++){
    const RegionBattleInfo &battle = regionBattleInfo(r);
    uint8_t champ=0, best=0;
    const Trainer &champion = trainerInfo(r, battle.trainerCount - 1);
    for (int k=0;k<champion.count;k++)
      if (champion.team[k].level > champ) champ = champion.team[k].level;
    for (int i=0;i<battle.trainerCount-1;i++) {
      const Trainer &t = trainerInfo(r, i);
      for (int k=0;k<t.count;k++)
        if (t.team[k].level > best) best = t.team[k].level;
    }
    char m[96];
    snprintf(m,sizeof(m),"%s: the champion tops the ladder (%u vs %u)", regionInfo(r).name, champ, best);
    ck(champ >= best, m);
  }

  // Some of each region's own generation must appear, which is what would catch
  // a roster pasted into the wrong table. NOT "most": Johto's leaders are
  // famously Kanto-heavy in Gold/Silver -- Falkner's Pidgey, Bugsy's Metapod,
  // Morty's Gastly line, Pryce's Seel, Clair's Dragonair -- so only 13 of its 49
  // are Johto natives, and that is faithful rather than a mistake. Hoenn, which
  // shares almost nothing with Kanto, comes out at 47 of 57.
  for (int r=1; r<regions; r++){
    const RegionBattleInfo &battle = regionBattleInfo(r);
    int own=0, total=0;
    uint16_t lo = regionInfo(r).lo, hi = regionInfo(r).hi;
    for (int i=0;i<battle.trainerCount;i++) {
      const Trainer &t = trainerInfo(r, i);
      for (int k=0;k<t.count;k++){
        total++;
        uint16_t d = t.team[k].dex;
        if (d >= lo && d <= hi) own++;
      }
    }
    char m[110];
    snprintf(m,sizeof(m),"%s: %d of %d creatures are its own generation", regionInfo(r).name, own, total);
    ck(own >= 8, m);              // enough to prove it is the right table
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
