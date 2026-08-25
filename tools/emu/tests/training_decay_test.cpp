// Training is maintained effort, unlike innate IVs. Every complete hour
// subtracts a fixed percentage of that individual's IV-based training CAP,
// online and during RTC catch-up.
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
  p.dbgHatchAs(6,false);
  p.nature=NATURE_ADAMANT;  // a canonical stat nature: no training modifier
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
  p.trAtk=p.trDef=p.trSpe=100;
  // Prevent the passive DEF tick from hiding the decay under a +1 gain.
  p.fullness=p.joy=p.energy=p.hygiene=0;
  p.ageMinutes=0;
}

int main(){
  {
    Pet p; ready(p);
    for(int i=0;i<59;i++) p.dbgTick();
    ck(p.trAtk==100 && p.trDef==100 && p.trSpe==100,
       "training does not decay before a complete hour");
    p.dbgTick();
    ck(p.trAtk==90 && p.trDef==90 && p.trSpe==90,
       "one live hour subtracts ten percent of the cap");
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==80 && p.trDef==80 && p.trSpe==80,
       "hourly decay stays based on the cap instead of compounding");
  }

  {
    Pet p; ready(p); p.trAtk=p.trDef=p.trSpe=50;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==40 && p.trDef==40 && p.trSpe==40,
       "decay uses the cap even when current training is lower");
  }

  {
    Pet p; ready(p);
    p.dbgSetSeen(1000);
    p.syncClock(1000 + 120*60);
    ck(p.trAtk==80 && p.trDef==80 && p.trSpe==80,
       "two offline hours apply the same two decay steps");
  }

  // The training natures use 9% for a strengthened channel and 11% for a
  // weakened one. Each loss is rounded UP from the IV-based cap.
  {
    Pet p; ready(p); p.nature=NATURE_HARDY;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==91 && p.trDef==90 && p.trSpe==90,
       "Hardy slows attack-training decay");
  }
  {
    Pet p; ready(p); p.nature=NATURE_BASHFUL;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==89 && p.trDef==91 && p.trSpe==90,
       "Bashful speeds attack decay and slows defence decay");
  }
  {
    Pet p; ready(p); p.nature=NATURE_QUIRKY;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==91 && p.trDef==89 && p.trSpe==90,
       "Quirky slows attack decay and speeds defence decay");
  }
  {
    Pet p; ready(p); p.nature=NATURE_SERIOUS;
    p.dbgSetSeen(1000); p.syncClock(1000+60*60);
    ck(p.trAtk==90 && p.trDef==90 && p.trSpe==91,
       "offline catch-up applies the same slower speed decay");
  }

  {
    Pet p; ready(p); p.ivAtk=8; p.trAtk=77; p.nature=NATURE_HARDY;
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==70,"a 9-percent loss from cap 77 rounds up to seven");
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
