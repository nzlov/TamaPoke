// Natures belong to the individual, so the same one must drive the live pet,
// its banked PartyMon, battle stats and save/reload. The five canonical neutral
// natures modify the three training channels in TamaPoke.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "nature.h"
#include "i18n.h"
#include "save.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed=37; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void sameIndividual(Pet &p, PartyMon &m){
  p.dbgHatchAs(65,false);                 // Alakazam
  p.ageMinutes=49UL*MINUTES_PER_LEVEL;    // level 50
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=20;
  p.trAtk=p.trDef=p.trSpe=20;
  m.dex=p.speciesId; m.level=p.level();
  m.ivAtk=p.ivAtk; m.ivDef=p.ivDef; m.ivSpe=p.ivSpe; m.ivHp=p.ivHp;
  m.trAtk=p.trAtk; m.trDef=p.trDef; m.trSpe=p.trSpe;
  m.gender=p.gender;
}

int main(){
  ck(NATURE_COUNT==25,"the complete 25-nature catalogue is present");
  for(uint8_t i=0;i<NATURE_COUNT;i++)
    ck(natureValid((NatureId)i),"every catalogue id is valid");

  // Non-neutral natures modify the whole final combat stat, canonically.
  ck(natureStatValue(NATURE_ADAMANT,NATURE_STAT_ATK,100,20)==132,
     "Adamant raises final attack by 10 percent");
  ck(natureStatValue(NATURE_ADAMANT,NATURE_STAT_SPA,100,20)==108,
     "Adamant lowers final special attack by 10 percent");
  ck(natureStatValue(NATURE_ADAMANT,NATURE_STAT_DEF,100,20)==120,
     "Adamant leaves unrelated stats alone");
  ck(natureStatValue(NATURE_ADAMANT,NATURE_STAT_NONE,100,20)==120,
     "HP is never nature-modified");

  // Neutral natures modify the TRAINING term, not the base/IV/level term.
  // ATK training backs both physical and special attack, while DEF training
  // backs both physical and special defence; neither special stat has its own
  // training value in TamaPoke.
  ck(natureStatValue(NATURE_HARDY,NATURE_STAT_ATK,100,20)==122,
     "Hardy boosts attack training contribution by 10 percent");
  ck(natureStatValue(NATURE_HARDY,NATURE_STAT_SPA,100,20)==122,
     "Hardy boosts the same training contribution reused by special attack");
  ck(natureStatValue(NATURE_BASHFUL,NATURE_STAT_ATK,100,20)==118 &&
     natureStatValue(NATURE_BASHFUL,NATURE_STAT_DEF,100,20)==122,
     "Bashful weakens attack training and strengthens defence training");
  ck(natureStatValue(NATURE_QUIRKY,NATURE_STAT_ATK,100,20)==122 &&
     natureStatValue(NATURE_QUIRKY,NATURE_STAT_DEF,100,20)==118,
     "Quirky strengthens attack training and weakens defence training");
  ck(natureStatValue(NATURE_SERIOUS,NATURE_STAT_SPE,100,20)==122,
     "Serious strengthens speed training");

  // Pet and Party must consume the same domain rule; battle builds from these.
  {
    Pet p; PartyMon m; sameIndividual(p,m);
    p.nature=NATURE_UNKNOWN;
    uint16_t plainAtk=p.atkStat(), plainSpa=p.spaStat();
    p.nature=m.nature=NATURE_ADAMANT;
    ck(p.atkStat()==party.atkOf(m) && p.spaStat()==party.spaOf(m),
       "live and banked stats apply the same nature");
    ck(p.atkStat()>plainAtk && p.spaStat()<plainSpa,
       "Adamant changes the real Pet combat stats");
  }

  // A hatch rolls once, and the value survives the key-driven save backup.
  {
    nvs().clear();
    Pet p; p.begin(); p.dbgHatchAs(25,false);
    ck(natureValid(p.nature),"hatching assigns a valid nature");
    NatureId rolled=p.nature;
    p.saveNow();
    ck(nvs().count("nat")==1,"the live nature has its own NVS field");
    Pet q; q.begin();
    ck(q.nature==rolled,"the live nature survives reload");
  }

  // A save from before natures gets one stable value rather than resetting or
  // rerolling on each boot.
  {
    nvs().clear(); Preferences old; old.begin("tamapoke",false);
    old.putUShort("savev",SAVE_STATE_VERSION); old.putBool("init",true);
    old.putShort("dexn",6); old.putUChar("ivat",17); old.putUChar("ivdf",23);
    old.putUChar("ivsp",29); old.putUChar("ivhp",31); old.end();
    Pet first; first.begin(); NatureId migrated=first.nature;
    Pet second; second.begin();
    ck(natureValid(migrated) && second.nature==migrated,
       "a legacy live pet receives a stable migrated nature");
  }

  int8_t en=uiFindLocale("en-US"), zh=uiFindLocale("zh-CN");
  ck(en>=0 && zh>=0,"nature display locales are installed");
  if(en>=0){setLang((Lang)en); ck(!strcmp(natureName(NATURE_HARDY),"Hardy"),
                                  "English nature name is localized");}
  if(zh>=0){setLang((Lang)zh); ck(!strcmp(natureName(NATURE_HARDY),"勤奋"),
                                  "Chinese nature name is localized");}

  printf("%s\n",bad?"FAILURES":"all good");
  return bad?1:0;
}
