// Training follows care state, unlike innate IVs. Every 60 total minutes settle
// exactly once: a maintained majority applies half the nature-based decay, a
// low-state majority applies double decay, and a 30:30 tie applies base decay.
// Care state never grants training. Online and RTC catch-up use the same rule.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "nature.h"
#include <cstdio>

uint32_t g_seed=31; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void ready(Pet &p){
  p.speciesId=6;
  p.nature=NATURE_ADAMANT;  // a canonical stat nature: no training modifier
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
  p.trAtk=p.trDef=p.trSpe=100;
  // Keep care state low so this fixture exercises doubled decay.
  p.fullness=p.joy=p.energy=p.hygiene=0;
  p.ageMinutes=0;
}

int main(){
  {
    Pet p; ready(p); p.trAtk=p.trDef=p.trSpe=50;
    p.sleeping=true;
    for(int i=0;i<59;i++) p.dbgTick();
    ck(p.trAtk==50 && p.trDef==50 && p.trSpe==50,
       "wellbeing waits for 60 accumulated minutes");
    p.dbgTick();
    ck(p.trAtk==47 && p.trDef==47 && p.trSpe==47,
       "sleep applies half decay without granting training");
  }

  {
    Pet p; ready(p); p.trAtk=p.trDef=p.trSpe=50;
    for(int i=0;i<60;i++) {
      p.fullness=p.joy=p.energy=p.hygiene=100;
      p.dbgTick();
    }
    ck(p.trAtk==47 && p.trDef==47 && p.trSpe==47,
       "maintained waking time applies half decay without granting training");
  }

  {
    Pet p; ready(p); p.nature=NATURE_HARDY; p.sleeping=true;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==98 && p.trDef==97 && p.trSpe==97,
       "half decay keeps fractional percentages until the final round-up");
  }

  {
    Pet p; ready(p);
    for(int i=0;i<59;i++) p.dbgTick();
    ck(p.trAtk==100 && p.trDef==100 && p.trSpe==100,
       "training does not decay before a complete hour");
    p.dbgTick();
    ck(p.trAtk==90 && p.trDef==90 && p.trSpe==90,
       "one low-state hour applies double the five-percent decay");
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==80 && p.trDef==80 && p.trSpe==80,
       "low-state decay stays based on the cap instead of compounding");
  }

  {
    Pet p; ready(p); p.trAtk=p.trDef=p.trSpe=50;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==40 && p.trDef==40 && p.trSpe==40,
       "double decay uses the cap even when current training is lower");
  }

  {
    Pet p; ready(p);
    p.dbgSetSeen(1000);
    p.syncClock(1000 + 120*60);
    ck(p.trAtk==80 && p.trDef==80 && p.trSpe==80,
       "two offline low-state hours apply the same double decay");
  }

  {
    Pet p; ready(p); p.trAtk=p.trDef=p.trSpe=50; p.sleeping=true;
    p.dbgSetSeen(1000);
    p.syncClock(1000 + 120*60);
    ck(p.trAtk==44 && p.trDef==44 && p.trSpe==44,
       "two offline sleeping hours apply the same half decay without gains");
  }

  {
    Pet p; ready(p); p.trAtk=p.trDef=p.trSpe=50; p.sleeping=true;
    for(int i=0;i<31;i++) p.dbgTick();
    p.sleeping=false; p.fullness=p.joy=p.energy=p.hygiene=0;
    for(int i=0;i<29;i++) p.dbgTick();
    ck(p.trAtk==47 && p.trDef==47 && p.trSpe==47,
       "a maintained majority selects half decay at the fixed hour boundary");
  }

  {
    Pet p; ready(p); p.trAtk=p.trDef=p.trSpe=50; p.sleeping=true;
    for(int i=0;i<29;i++) p.dbgTick();
    p.sleeping=false; p.fullness=p.joy=p.energy=p.hygiene=0;
    for(int i=0;i<31;i++) p.dbgTick();
    ck(p.trAtk==40 && p.trDef==40 && p.trSpe==40,
       "a low-state majority selects double decay at the fixed hour boundary");
  }

  {
    Pet p; ready(p); p.trAtk=p.trDef=p.trSpe=50; p.sleeping=true;
    for(int i=0;i<30;i++) p.dbgTick();
    p.sleeping=false; p.fullness=p.joy=p.energy=p.hygiene=0;
    for(int i=0;i<30;i++) p.dbgTick();
    ck(p.trAtk==45 && p.trDef==45 && p.trSpe==45,
       "a 30-to-30 tie selects base decay exactly once");
    for(int i=0;i<59;i++) p.dbgTick();
    ck(p.trAtk==45 && p.trDef==45 && p.trSpe==45,
       "the next cycle cannot settle before another 60 total minutes");
    p.dbgTick();
    ck(p.trAtk==35 && p.trDef==35 && p.trSpe==35,
       "the next fixed cycle settles independently");
  }

  // The training natures use 3% for a strengthened channel and 7% for a
  // weakened one. Each loss is rounded UP from the IV-based cap.
  {
    Pet p; ready(p); p.nature=NATURE_HARDY;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==94 && p.trDef==90 && p.trSpe==90,
       "Hardy slows doubled attack-training decay");
  }
  {
    Pet p; ready(p); p.nature=NATURE_DOCILE;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==90 && p.trDef==94 && p.trSpe==90,
       "Docile slows doubled defence-training decay");
  }
  {
    Pet p; ready(p); p.nature=NATURE_BASHFUL;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==86 && p.trDef==94 && p.trSpe==90,
       "Bashful scales doubled attack and defence decay");
  }
  {
    Pet p; ready(p); p.nature=NATURE_QUIRKY;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==94 && p.trDef==86 && p.trSpe==90,
       "Quirky scales doubled attack and defence decay");
  }
  {
    Pet p; ready(p); p.nature=NATURE_SERIOUS;
    p.dbgSetSeen(1000); p.syncClock(1000+60*60);
    ck(p.trAtk==90 && p.trDef==90 && p.trSpe==94,
       "offline catch-up applies the same doubled speed decay");
  }

  {
    Pet p; ready(p); p.ivAtk=8; p.trAtk=77; p.nature=NATURE_HARDY;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==72,"doubled 3-percent loss from cap 77 rounds up to five");
  }

  {
    Pet p; ready(p);
    p.trAtk=3;
    ck(p.raiseTrainingFloor(TRAINING_ATK,10) &&
       p.trMinAtk==10 && p.trAtk==10,
       "a training tonic raises its permanent floor and current value");
    for(int i=0;i<24*60;i++) p.dbgTick();
    ck(p.trAtk==10,
       "state-based decay never crosses the raised training floor");
    for(int i=0;i<9;i++) ck(p.raiseTrainingFloor(TRAINING_ATK,10),
                            "the floor can rise in ten-point steps");
    ck(p.trMinAtk==p.trMaxAtk() && !p.canRaiseTrainingFloor(TRAINING_ATK),
       "a tonic is unusable once its floor reaches the IV training cap");
  }
  {
    Pet p; ready(p); p.sleeping=true;
    p.trMinAtk=p.trMinDef=p.trMinSpe=50;
    p.trAtk=p.trDef=p.trSpe=50;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==50 && p.trDef==50 && p.trSpe==50,
       "maintained decay respects every floor without granting training");
  }

  {
    Pet p; ready(p);
    p.frozen=true;
    p.dbgSetSeen(1000);
    p.syncClock(1000 + 120*60);
    ck(p.trAtk==100 && p.trDef==100 && p.trSpe==100,
       "a frozen banked companion never loses training");
  }

  printf("%s\n",bad?"FAILURES":"all good");
  return bad?1:0;
}
