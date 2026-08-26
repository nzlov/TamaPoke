// The legacy revive entry point now imports a complete cultivation record. It
// remains as an API compatibility path, but no longer creates a frozen pet.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include <cstdio>
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
static uint32_t g_ms=0; uint32_t millis(){return g_ms;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  Pet p; p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  PartyMon m; m.dex=6; m.level=61;
  m.ageMinutes=60UL*MINUTES_PER_LEVEL; m.shiny=1; m.nature=NATURE_BRAVE;
  m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=24; m.trAtk=m.trDef=m.trSpe=35;
  m.fullness=43; m.joy=54; m.energy=65; m.hygiene=76; m.bond=87;
  m.gymIvRewards[3]=GYM_IV_REWARD_HP;
  m.moves[0]=1; m.moves[1]=2;
  snprintf(m.nick,sizeof(m.nick),"BLAZE");

  p.reviveFrom(m);
  ck(p.speciesId==6 && p.level()==61, "comes back at the level it was banked at");
  ck(p.shiny && !strcmp(p.nick,"BLAZE"), "keeps its shininess and its name");
  ck(p.moves[0]==1 && p.moves[1]==2, "and its moveset");
  ck(p.gymIvRewards[3]==GYM_IV_REWARD_HP, "and its per-creature gym history");
  ck(p.nature==NATURE_BRAVE,"and its nature");
  ck(!p.frozen && p.fullness==43 && p.joy==54 && p.energy==65 &&
     p.hygiene==76 && p.bond==87, "and restores its cultivation state unfrozen");

  // A cultivation slot remains alive and therefore ages.
  uint8_t lvl = p.level();
  for (int i=0;i<MINUTES_PER_LEVEL;i++){ g_ms += 60000; p.update(g_ms);
    p.fullness=p.joy=p.energy=p.hygiene=100; }
  ck(p.level()==lvl+1, "continues ageing while in a cultivation slot");

  // and it survives a reload, still active rather than frozen
  Pet q; q.begin();
  ck(!q.frozen && q.speciesId==6 && q.nature==NATURE_BRAVE,
     "stays active with its nature across a reload");

  // and a brand new egg is a normal life again
  q.newEgg();
  ck(!q.frozen, "a new egg remains active too");
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
