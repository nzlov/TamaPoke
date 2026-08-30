// Gender is an individual property: it follows the species ratio, persists
// with the creature, and modifies only final Attack and Special Attack.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "gender.h"
#include "content.h"
#include <cstdio>
#include <cstdlib>

uint32_t g_seed=43; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  contentBegin();
  ck(dexEntry(1).femaleRate==4,
     "the regional species record exposes its canonical gender rate");
  struct SpriteCase { PetGender gender; bool shiny; uint16_t expected; } sprites[] = {
    {GENDER_MALE, false, 0x2D7F}, {GENDER_MALE, true, 0xF7A0},
    {GENDER_FEMALE, false, 0xF94A}, {GENDER_FEMALE, true, 0xFE59},
  };
  bool variants=true;
  for(const SpriteCase &item:sprites){
    uint8_t *data=nullptr; uint32_t size=0; uint8_t scale=0;
    if(!contentLoadSprite(1,item.shiny,item.gender,false,MEGA_FORM_NONE,false,
                          &data,&size,&scale) ||
       size<9 || scale!=4 ||
       (uint16_t)(data[7] | (data[8] << 8))!=item.expected) variants=false;
    free(data);
  }
  ck(variants,"normal, shiny and female sprite variants select independently");

  ck(genderFromRate(0,0)==GENDER_MALE && genderFromRate(0,7)==GENDER_MALE,
     "a male-only species always rolls male");
  ck(genderFromRate(8,0)==GENDER_FEMALE && genderFromRate(8,7)==GENDER_FEMALE,
     "a female-only species always rolls female");
  ck(genderFromRate(4,3)==GENDER_FEMALE && genderFromRate(4,4)==GENDER_MALE,
     "a one-half ratio divides the eight canonical roll slots");
  ck(genderFromRate(GENDER_RATE_NONE,0)==GENDER_NONE,
     "a genderless species stays genderless");

  Pet p; p.dbgHatchAs(65,false);            // Alakazam
  p.ageMinutes=49UL*MINUTES_PER_LEVEL;      // level 50
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=20;
  p.trAtk=p.trDef=p.trSpe=20;
  p.nature=NATURE_UNKNOWN;
  p.gender=GENDER_NONE;
  uint16_t plainAtk=p.atkStat(), plainSpa=p.spaStat(), plainDef=p.defStat();
  p.gender=GENDER_MALE;
  ck(p.atkStat()==(uint32_t)plainAtk*110/100 &&
     p.spaStat()==(uint32_t)plainSpa*90/100,
     "male applies +10 percent Attack and -10 percent Special Attack");
  p.gender=GENDER_FEMALE;
  ck(p.atkStat()==(uint32_t)plainAtk*90/100 &&
     p.spaStat()==(uint32_t)plainSpa*110/100,
     "female applies -10 percent Attack and +10 percent Special Attack");
  ck(p.defStat()==plainDef,
     "gender leaves unrelated combat stats unchanged");

  PartyMon m;
  m.dex=p.speciesId; m.level=p.level(); m.gender=p.gender; m.nature=p.nature;
  m.ivAtk=p.ivAtk; m.ivDef=p.ivDef; m.ivSpe=p.ivSpe; m.ivHp=p.ivHp;
  m.trAtk=p.trAtk; m.trDef=p.trDef; m.trSpe=p.trSpe;
  ck(p.atkStat()==party.atkOf(m) && p.spaStat()==party.spaOf(m),
     "live and banked creatures use the same gender calculation");

  printf("%s\n",bad?"FAILURES":"all good");
  return bad?1:0;
}
