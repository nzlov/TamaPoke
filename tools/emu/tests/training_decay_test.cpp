// Training is maintained effort, unlike innate IVs. Every complete hour of
// life keeps 90% of ATK/DEF/SPE training, online and during RTC catch-up.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
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
       "one live hour keeps exactly 90 percent");
    for(int i=0;i<60;i++) p.dbgTick();
    ck(p.trAtk==81 && p.trDef==81 && p.trSpe==81,
       "hourly decay compounds from the current value");
  }

  {
    Pet p; ready(p);
    p.dbgSetSeen(1000);
    p.syncClock(1000 + 120*60);
    ck(p.trAtk==81 && p.trDef==81 && p.trSpe==81,
       "two offline hours apply the same two decay steps");
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
