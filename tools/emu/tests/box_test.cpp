// The 24-slot Box is persisted as four pages. This checks full-state exchange,
// page persistence, and both legacy combat-only layouts.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include "save.h"
#include <cstdio>
uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
struct LegacyPartyMonV1 {
  int16_t dex = 0;
  uint16_t level = 1;
  uint16_t medals = 0;
  uint8_t ivAtk = 0, ivDef = 0, ivSpe = 0, ivHp = 0;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  uint8_t shiny = 0;
  char nick[12] = "";
  MoveId moves[MOVE_SLOTS] = { 0, 0, 0, 0 };
};
struct LegacyPartyMonV2 {
  LegacyPartyMonV1 mon;
  uint8_t gymIvRewards[GYM_IV_REWARD_SLOTS] = { 0 };
};
struct LegacyPartyMonV3 {
  LegacyPartyMonV2 mon;
  NatureId nature = NATURE_UNKNOWN;
};
static PartyMon mk(int dex,int lvl){ PartyMon m; m.dex=dex; m.level=lvl;
  m.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; return m; }
template <size_t N>
static void putPreDeathMons(Preferences &prefs, const char *key,
                            PartyMon (&mons)[N]) {
  constexpr size_t recordSize = offsetof(PartyMon, state);
  uint8_t raw[recordSize * N];
  for (size_t i = 0; i < N; i++)
    memcpy(raw + i * recordSize, &mons[i], recordSize);
  prefs.putBytes(key, raw, sizeof(raw));
}

int main(){
  // Current-format team with no Box pages.
  { Preferences seed; seed.begin("tamapoke", false);
    PartyMon old[PARTY_SLOTS];
    for (int i=0;i<PARTY_SLOTS;i++) old[i]=mk(1+i*20, 30+i);
    seed.putBytes("team1", old, sizeof(old));
    seed.putUShort("rostv", 1);
    seed.putUChar("active", 0);
    seed.end(); }
  Party p; p.begin();
  bool kept = true;
  for (int i=0;i<PARTY_SLOTS;i++) if (p.slots[i].dex != 1+i*20) kept=false;
  ck(kept, "an absent box key leaves the party intact");
  ck(p.boxCount()==0, "and comes up with an empty box, not garbage");

  // deposit: party slot 0 <-> empty box slot 0
  int16_t was = p.slots[0].dex;
  p.swapPartyBox(0, 0);
  ck(p.box[0].dex==was && p.slots[0].empty(), "swapping into an empty box slot deposits");
  // withdraw: the same call the other way round
  p.swapPartyBox(0, 0);
  ck(p.slots[0].dex==was && p.box[0].empty(), "and swapping back withdraws");

  // a real exchange, both occupied
  p.box[3] = mk(150, 70);
  p.box[3].fullness = 17;
  p.box[3].joy = 29;
  p.box[3].bond = 63;
  int16_t a = p.slots[2].dex, b = p.box[3].dex;
  p.swapPartyBox(2, 3);
  ck(p.slots[2].dex==b && p.box[3].dex==a, "two occupied slots exchange");
  ck(p.slots[2].fullness==17 && p.slots[2].joy==29 && p.slots[2].bond==63,
     "the exchange carries full cultivation state");

  // and it survives a reload
  Party q; q.begin();
  ck(q.slots[2].dex==b && q.box[3].dex==a, "the swap persists across a reload");
  ck(q.slots[2].fullness==17 && q.slots[2].joy==29 && q.slots[2].bond==63,
     "the cultivation state persists too");

  p.box[23] = mk(149, 61);
  p.box[23].energy = 11;
  p.boxSave();
  Party page4; page4.begin();
  ck(page4.box[23].dex==149 && page4.box[23].energy==11,
     "the fourth Box page persists slot 24");

  ck(p.boxFirstFree()==0, "boxFirstFree finds the hole");
  for (int i=0;i<BOX_SLOTS;i++) p.box[i]=mk(19,5);
  ck(p.boxFirstFree()==-1 && !p.boxAdd(mk(1,1)), "a full box refuses more");

  // a captured newcomer with a full party must reach the box rather than being stuck
  { Party r; r.begin();
    for (int i=0;i<PARTY_SLOTS;i++) r.slots[i]=mk(1+i,40);
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=PartyMon();
    r.save(); r.boxSave();
    PartyMon newcomer = mk(150, 73);
    bool toParty = r.add(newcomer);
    bool toBox = toParty ? false : r.boxAdd(newcomer);
    ck(!toParty && toBox, "a full party sends the newcomer to the box");
    ck(r.box[0].dex==150, "and it is really there");
    // a full party AND a full box is the only case that should refuse
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=mk(19,5);
    ck(!r.add(newcomer) && !r.boxAdd(newcomer),
       "only a full party AND a full box refuses, which is when the player picks");
  }

  // withdrawing into a party that has room must not need a party slot picked
  { Party r; r.begin();
    for (int i=0;i<PARTY_SLOTS;i++) r.slots[i]=PartyMon();
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=PartyMon();
    r.slots[0]=mk(6,50); r.box[0]=mk(9,40);
    r.save(); r.boxSave();
    int free = r.firstFree();
    ck(free==1, "the first free party slot is found");
    r.swapPartyBox((uint8_t)free, 0);
    ck(r.slots[1].dex==9 && r.box[0].empty(),
       "withdrawing into a free slot moves it without displacing anyone");
  }

  // Adding the per-creature byte array changes the raw NVS record size. Saves
  // from the previous firmware must be mapped rather than appearing empty.
  {
    Preferences seed; seed.begin("tamapoke", false); seed.clear();
    seed.putUShort("savev", SAVE_STATE_VERSION);
    seed.putBool("init", true);
    LegacyPartyMonV1 oldParty[PARTY_SLOTS];
    LegacyPartyMonV1 oldBox[18];
    oldParty[1].dex=25; oldParty[1].level=44; oldParty[1].ivDef=27;
    oldParty[1].trDef=73; oldParty[1].moves[0]=9;
    snprintf(oldParty[1].nick,sizeof(oldParty[1].nick),"SPARK");
    oldBox[4].dex=150; oldBox[4].level=70; oldBox[4].shiny=1;
    seed.putBytes("party", oldParty, sizeof(oldParty));
    seed.putBytes("box", oldBox, sizeof(oldBox));
    seed.end();
    Pet live; live.begin();
    Party migrated; migrated.begin(); migrated.attach(live);
    ck(migrated.slots[2].dex==25 && migrated.slots[2].level==44 &&
       migrated.slots[2].ivDef==27 && migrated.slots[2].trDef==73 &&
       migrated.slots[2].moves[0]==9 && !strcmp(migrated.slots[2].nick,"SPARK"),
       "the previous party record layout migrates without losing fields");
    ck(migrated.box[4].dex==150 && migrated.box[4].level==70 && migrated.box[4].shiny,
       "the previous box record layout migrates too");
    ck(!migrated.slots[2].gymIvRewards[0] && !migrated.box[4].gymIvRewards[0],
       "legacy creatures start with unclaimed gym IV bytes");
  }

  // The immediately previous record already had gym history but no nature.
  // It must keep every byte and receive one stable migrated nature.
  {
    Preferences seed; seed.begin("tamapoke", false); seed.clear();
    seed.putUShort("savev", SAVE_STATE_VERSION);
    seed.putBool("init", true);
    LegacyPartyMonV2 oldParty[PARTY_SLOTS];
    LegacyPartyMonV2 oldBox[18];
    oldParty[2].mon.dex=94; oldParty[2].mon.level=55;
    oldParty[2].mon.ivAtk=9; oldParty[2].mon.ivDef=17;
    oldParty[2].mon.ivSpe=25; oldParty[2].mon.ivHp=31;
    oldParty[2].gymIvRewards[8]=GYM_IV_REWARD_HP;
    oldBox[5].mon.dex=149; oldBox[5].mon.level=73;
    oldBox[5].gymIvRewards[16]=GYM_IV_REWARD_ATK;
    seed.putBytes("party", oldParty, sizeof(oldParty));
    seed.putBytes("box", oldBox, sizeof(oldBox)); seed.end();
    Pet live; live.begin();
    Party migrated; migrated.begin(); migrated.attach(live);
    ck(migrated.slots[3].dex==94 && migrated.slots[3].level==55 &&
       migrated.slots[3].gymIvRewards[8]==GYM_IV_REWARD_HP,
       "the pre-nature party layout preserves its gym history");
    ck(migrated.box[5].dex==149 && migrated.box[5].level==73 &&
       migrated.box[5].gymIvRewards[16]==GYM_IV_REWARD_ATK,
       "the pre-nature box layout preserves its gym history");
    ck(natureValid(migrated.slots[3].nature) && natureValid(migrated.box[5].nature),
       "pre-nature banked creatures receive stable valid natures");
  }

  // The immediately previous layout had natures but no persistent death bit.
  // It must remain alive after migration rather than reading tail padding.
  {
    Preferences seed; seed.begin("tamapoke", false); seed.clear();
    PartyMon oldParty[PARTY_SLOTS];
    PartyMon oldBoxPage[BOX_PAGE_SLOTS];
    oldParty[3]=mk(131,61);
    oldParty[3].gymIvRewards[12]=GYM_IV_REWARD_DEF;
    oldParty[3].nature=NATURE_CALM;
    oldBoxPage[2]=mk(143,67);
    oldBoxPage[2].nature=NATURE_BRAVE;
    putPreDeathMons(seed, "team1", oldParty);
    putPreDeathMons(seed, "box11", oldBoxPage);
    seed.putUShort("rostv", 1);
    seed.putUChar("active", 3);
    seed.end();
    Party migrated; migrated.begin();
    ck(migrated.slots[3].dex==131 && migrated.slots[3].level==61 &&
       migrated.slots[3].nature==NATURE_CALM &&
       migrated.slots[3].gymIvRewards[12]==GYM_IV_REWARD_DEF &&
       !migrated.slots[3].dead(),
       "the pre-death party layout migrates alive with all fields intact");
    ck(migrated.box[8].dex==143 && migrated.box[8].level==67 &&
       migrated.box[8].nature==NATURE_BRAVE && !migrated.box[8].dead(),
       "the pre-death box layout migrates alive with all fields intact");
  }

  // Death belongs to the creature record and must survive both party and box
  // reloads until a revive clears it.
  {
    Preferences seed; seed.begin("tamapoke", false); seed.clear(); seed.end();
    Pet live; live.begin();
    Party stored; stored.begin(); stored.attach(live);
    stored.replaceAt(0, mk(25, 40));
    stored.box[2] = mk(94, 55);
    stored.box[2].setDead(true);
    stored.boxSave();
    stored.setDeadAt(0, true);
    Party loaded; loaded.begin();
    ck(loaded.slots[0].dead() && loaded.box[2].dead(),
       "party and box deaths persist across a reload");
    loaded.setDeadAt(0, false);
    loaded.box[2].setDead(false);
    loaded.boxSave();
    Party revived; revived.begin();
    ck(!revived.slots[0].dead() && !revived.box[2].dead(),
       "reviving clears the persistent death state");
  }

  // Roster state v3 used the same byte for retired evolution debt. Gender now
  // reuses that byte, so the per-creature state version must drive migration.
  {
    Preferences seed; seed.begin("tamapoke", false); seed.clear();
    PartyMon oldParty[PARTY_SLOTS];
    PartyMon oldBoxPage[BOX_PAGE_SLOTS];
    oldParty[3] = mk(25, 61);
    oldParty[3].stateVersion = 3;
    oldParty[3].gender = (PetGender)99;
    oldBoxPage[0] = mk(81, 34);
    oldBoxPage[0].stateVersion = 3;
    oldBoxPage[0].gender = (PetGender)99;
    seed.putBytes("team1", oldParty, sizeof(oldParty));
    seed.putBytes("box11", oldBoxPage, sizeof(oldBoxPage));
    seed.putUShort("rostv", 2);
    seed.putUChar("active", 3);
    seed.end();
    Party migrated; migrated.begin();
    PetGender first = migrated.slots[3].gender;
    ck(genderValid(first),
       "the pre-gender roster gains a valid stable gender");
    ck(migrated.box[6].gender == GENDER_NONE,
       "the pre-gender Box keeps genderless species neutral");
    migrated.save(); migrated.boxSave();
    Party again; again.begin();
    ck(again.slots[3].gender == first && again.slots[3].stateVersion == 4,
       "the migrated roster gender persists with its new state version");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
