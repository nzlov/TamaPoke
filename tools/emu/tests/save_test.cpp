// The save backup. A run is weeks of real time, so the thing that matters is
// not that export/import runs -- it is that NOTHING is quietly left out, and
// that a bad blob cannot destroy the save it was meant to protect.
//
// The completeness check at the bottom is the important one: it compares the
// backup's field table against the keys the firmware ACTUALLY wrote, so adding
// a field and forgetting the backup fails here instead of silently dropping it
// from every player's export.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include <cstdio>
#include <set>
#include <string>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  // --- a save worth losing: a raised creature, a full party, a filled box,
  // both ladders, a Pokedex with holes in it, a named trainer
  Pet pet;
  pet.begin();
  pet.dbgHatchAs(6,true);
  pet.shiny = true;
  pet.gigantamaxFactor = true;
  pet.raisedMinutes = 1234;
  player.wildRareBonus = 15;
  pet.ageMinutes = 72UL*MINUTES_PER_LEVEL;
  pet.ivAtk=31; pet.ivDef=7; pet.ivSpe=22; pet.ivHp=19;
  pet.trAtk=64; pet.trDef=31; pet.trSpe=90;
  pet.trMinAtk=40; pet.trMinDef=20; pet.trMinSpe=70;
  pet.nature=NATURE_MODEST;
  pet.gender=GENDER_FEMALE;
  pet.abilitySlot=ABILITY_SLOT_HIDDEN;
  pet.gymIvRewards[0]=GYM_IV_REWARD_DEF;
  pet.gymIvRewards[71]=GYM_IV_REWARD_LEGACY_CLAIMED;
  pet.relearnFromLevel();
  MoveId progressMove = MOVE_NONE;
  uint8_t progressSlot = 0;
  for (uint8_t i = 0; i < MOVE_SLOTS; i++)
    if (pet.moves[i] != 2 && pet.moves[i] != 9 &&
        moveTracksProgress(pet.moves[i])) {
      progressMove = pet.moves[i]; progressSlot = i; break;
    }
  pet.moveUses[progressSlot] = 81;
  pet.rename("SCORCH");
  player.avatar = 6;        // past the old four, so a stale & 3 mask would break it
  player.badges = 0x00BF; player.badgesHard = 0x000A;
  player.streak = 12; player.bestStreak = 19; player.totalMedals = 41;
  player.gameHi = 33; player.strHi = 21; player.spdHi = 9;
  pet.bond = 77; pet.careMistakes = 2; pet.weight = 55;
  player.dexReg[0] = 0x5A; player.dexReg[9] = 0xC3; player.dexShinyReg[3] = 0x11;
  party.begin();
  party.attach(pet);
  for (int i=1;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=20+i*7; m.level=40+i;
    m.ageMinutes=(uint32_t)(m.level-1)*MINUTES_PER_LEVEL;
    m.nature=(NatureId)(i%NATURE_COUNT);
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20+i; m.shiny=(i==2); m.sparkle=(i==3);
    m.raisedMinutes=300+i;
    m.gender=(i&1)?GENDER_FEMALE:GENDER_MALE;
    if(i==2) m.trainingTicks=(uint16_t)(29u<<8)|17u;
    m.gymIvRewards[i]=GYM_IV_REWARD_ATK;
    snprintf(m.nick,sizeof(m.nick),"P%d",i);
    m.moves[0]=1+i; m.moves[1]=9; m.reserveMoves[0]=20+i;
    party.replaceAt(i,m); }
  party.slots[1].moves[3] = progressMove;
  party.slots[1].moveUses[3] = 27;
  for (int i=0;i<BOX_SLOTS;i++){ PartyMon m; m.dex=1+i*3; m.level=10+i;
    m.ageMinutes=(uint32_t)(m.level-1)*MINUTES_PER_LEVEL;
    m.nature=(NatureId)((i+5)%NATURE_COUNT);
    m.gender=(i&1)?GENDER_MALE:GENDER_FEMALE;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=11;
    m.trMinSpe=30;
    if(i==7) m.trainingTicks=(uint16_t)(31u<<8)|23u;
    m.gymIvRewards[i]=GYM_IV_REWARD_SPE; m.reserveMoves[1]=40+i;
    party.box[i]=m; }
  party.box[0].setAbilitySlot(ABILITY_SLOT_HIDDEN);
  party.box[0].moves[3] = progressMove;
  party.box[0].moveUses[3] = 9;
  party.boxSave();
  // renameTrainer() persists, and save() writes every field -- so this is also
  // what commits everything set above. save() itself is private on purpose.
  player.renameTrainer("DYLAN");
  pet.setDead(true);
  party.setDeadAt(1, true);
  party.box[7].setDead(true);
  party.boxSave();

  // --- COMPLETENESS. Everything the firmware stored must be in the table.
  // This is what stops the backup rotting: add a key to pet.cpp and forget
  // save.cpp, and this fails instead of quietly dropping it from every export.
  //
  // It runs HERE, against the store the firmware just wrote -- not after a
  // restore. After an import the store only holds what the backup put back, so
  // a forgotten key would be missing from both sides and the check would pass
  // itself. It did exactly that until this was moved.
  {
    std::set<std::string> inTable;
    for (uint16_t i=0;i<SAVE_FIELD_COUNT;i++) inTable.insert(SAVE_FIELDS[i].key);
    int missing = 0;
    for (auto &kv : nvs()) {
      if (inTable.count(kv.first)) continue;
      printf("      key '%s' is stored but NOT backed up\n", kv.first.c_str());
      missing++;
    }
    ck(missing==0, "every key the firmware writes is in the backup table");
    ck(inTable.size()==SAVE_FIELD_COUNT, "and the table has no duplicates");
  }

  static uint8_t buf[SAVE_BLOB_MAX];
  size_t n = saveExport(buf, sizeof(buf));
  ck(n > 0, "a save exports");
  ck(n < sizeof(buf), "and fits a sensible buffer");
  printf("      (%u bytes)\n", (unsigned)n);

  // --- a mangled blob must never be applied
  {
    std::vector<uint8_t> t(buf, buf+n);
    t[0] = 'X';
    ck(!saveImport(t.data(), t.size()), "a blob with the wrong magic is refused");
    t = std::vector<uint8_t>(buf, buf+n);
    t[4] = SAVE_VERSION + 9;
    ck(!saveImport(t.data(), t.size()), "and one from a future version");
    t = std::vector<uint8_t>(buf, buf+n);
    t[SAVE_HDR + 3] ^= 0xFF;                   // flip a byte in the middle
    ck(!saveImport(t.data(), t.size()), "a single flipped byte fails the checksum");
    t = std::vector<uint8_t>(buf, buf+n-1);    // chopped short
    ck(!saveImport(t.data(), t.size()), "and a truncated blob is refused");
    ck(!saveImport(buf, 3), "as is one too short to hold a header");
    // and none of that touched the live save
    ck(nvs().count("team2") == 1, "a refused import leaves the save alone");
  }

  // --- the real thing: wipe everything, restore, and compare
  pet.factoryReset();
  ck(nvs().empty() || nvs().count("team2")==0, "the wipe really emptied NVS");
  ck(saveImport(buf, n), "the backup imports");

  Pet p2; Party q2;
  p2.begin(); q2.begin(); q2.attach(p2);
  ck(p2.speciesId==6 && p2.shiny && p2.gigantamaxFactor,
     "the creature is back with its combined rare and Gigantamax states");
  ck(p2.isDead(), "the active creature's death state is restored");
  ck(p2.raisedMinutes==1234 && player.wildRareBonus==15,
     "cultivation time and the player-wide wild bonus survive");
  ck(p2.ageMinutes==72UL*MINUTES_PER_LEVEL, "at the age it was");
  ck(p2.ivAtk==31 && p2.ivDef==7 && p2.ivSpe==22 && p2.ivHp==19, "with its IVs");
  ck(p2.trAtk==64 && p2.trDef==31 && p2.trSpe==90, "and its training");
  ck(p2.trMinAtk==40 && p2.trMinDef==20 && p2.trMinSpe==70,
     "and its permanent training floors");
  ck(p2.nature==NATURE_MODEST,"and its nature");
  ck(p2.gender==GENDER_FEMALE,"and its gender");
  ck(p2.gymIvRewards[0]==GYM_IV_REWARD_DEF &&
     p2.gymIvRewards[71]==GYM_IV_REWARD_LEGACY_CLAIMED, "and its gym IV reward bytes");
  ck(!strcmp(player.trainerName,"DYLAN"), "the trainer name survives");
  ck(p2.abilitySlot==ABILITY_SLOT_HIDDEN,
     "the live creature's hidden ability survives backup and restore");
  ck(!strcmp(p2.nick,"SCORCH"), "so does the nickname");
  ck(player.avatar==6, "and the avatar, including one past the original four");
  ck(player.badges==0x00BF && player.badgesHard==0x000A, "both badge ladders");
  ck(player.streak==12 && player.bestStreak==19 && player.totalMedals==41, "streak and medals");
  ck(player.gameHi==33 && player.strHi==21 && player.spdHi==9, "every minigame record");
  ck(p2.bond==77 && p2.careMistakes==2 && p2.weight==55, "bond, mistakes, weight");
  // The current roster restores the player snapshot after the legacy scalar mirrors, so
  // a partial boot load cannot add or lose Pokedex bits.
  ck((player.dexReg[0] & 0x5A)==0x5A && player.dexReg[9]==0xC3 &&
     (player.dexShinyReg[3] & 0x11)==0x11, "the Pokedex, holes and all");
  ck((player.dexReg[0] & ~0x5A) == 0,
     "and snapshot restore does not merge extra Pokedex bits");
  bool moves = true;
  for (int i=0;i<MOVE_SLOTS;i++) if (p2.moves[i]!=pet.moves[i]) moves=false;
  for (int i=0;i<RESERVE_MOVE_SLOTS;i++)
    if (p2.reserveMoves[i]!=pet.reserveMoves[i]) moves=false;
  ck(moves, "and all active and reserve moves");
  ck(progressMove && p2.moveUses[progressSlot] == 81 &&
     p2.moveLevel(progressMove) == 4,
     "and the active creature's per-move progress");

  ck(q2.count()==PARTY_SLOTS, "the whole party is back");
  bool party_ok = true;
  const PartyMon &active = q2.slots[0];
  if (active.dex != 6 || active.ageMinutes != 72UL*MINUTES_PER_LEVEL ||
      strcmp(active.nick, "SCORCH") || active.fullness != pet.fullness ||
      active.bond != 77 || active.nature != NATURE_MODEST ||
      active.abilitySlot() != ABILITY_SLOT_HIDDEN) party_ok = false;
  for (int i=1;i<PARTY_SLOTS;i++){
    const PartyMon &m = q2.slots[i];
    if (m.dex != 20+i*7 || m.level != 40+i) party_ok = false;
    if (m.moves[0] != 1+i || m.moves[1] != 9 || m.reserveMoves[0] != 20+i)
      party_ok = false;
    if (m.gymIvRewards[i] != GYM_IV_REWARD_ATK) party_ok = false;
    if (m.nature != (NatureId)(i%NATURE_COUNT)) party_ok = false;
    if (m.gender != ((i&1)?GENDER_FEMALE:GENDER_MALE)) party_ok = false;
    char want[8]; snprintf(want,sizeof(want),"P%d",i);
    if (strcmp(m.nick, want)) party_ok = false;
  }
  ck(party_ok, "with every level, moveset and nickname");
  ck(q2.slots[2].shiny && q2.slots[2].sparkle,
     "and a banked color-only creature is normalized to combined rare");
  ck(q2.slots[1].dead(), "and a party creature's death state is restored");
  ck(q2.slots[1].moveUseCount(progressMove) == 27,
     "and a banked party move keeps its own progress");
  ck(q2.slots[3].shiny && q2.slots[3].sparkle &&
     q2.slots[3].raisedMinutes==303,
     "and a banked sparkle-only creature migrates without losing cultivation time");
  ck(q2.slots[2].trainingTicks==((uint16_t)(29u<<8)|17u),
     "and both cultivation-state training counters survive");
  ck(q2.boxCount()==BOX_SLOTS, "the box comes back full");
  bool boxReserves = true;
  for (int i=0;i<BOX_SLOTS;i++)
    if (q2.box[i].reserveMoves[1] != 40+i) boxReserves = false;
  ck(boxReserves, "with every boxed reserve move");
  ck(q2.box[7].dex==1+7*3 && q2.box[7].level==17, "with the right creatures in it");
  ck(q2.box[7].gymIvRewards[7]==GYM_IV_REWARD_SPE,
     "with each banked creature's gym IV bytes");
  ck(q2.box[7].trMinSpe==30,
     "with each banked creature's permanent training floors");
  ck(q2.box[7].trainingTicks==((uint16_t)(31u<<8)|23u),
     "with each banked creature's training-state counters");
  ck(q2.box[7].nature==(NatureId)((7+5)%NATURE_COUNT),
     "with each banked creature's nature");
  ck(q2.box[7].dead(), "with each banked creature's death state");
  ck(q2.box[7].gender==GENDER_MALE,
     "with each banked creature's gender");
  ck(q2.box[0].abilitySlot()==ABILITY_SLOT_HIDDEN,
     "with each banked creature's ability slot");
  ck(q2.box[0].moveUseCount(progressMove) == 9,
     "with boxed move progress intact");

  // --- a restore must not leave anything of whatever was there before
  {
    Pet p3; Party q3; p3.begin(); q3.begin(); q3.attach(p3);
    player.badges = 0xFFFF;
    player.renameTrainer("GHOST");
    ck(saveImport(buf, n), "a second restore over a different save");
    Pet p4; Party q4; p4.begin(); q4.begin(); q4.attach(p4);
    ck(!strcmp(player.trainerName,"DYLAN") && player.badges==0x00BF,
       "overwrites it rather than merging with it");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
