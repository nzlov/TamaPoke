// Capture presentation is time-driven and must lock battle input until its
// success or break-free finish has been visible.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "sdmon.h"
#include <cstdio>

uint32_t g_seed=37; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}

extern bool battleOpen, btlOver, btlCaptureAnimating, btlCaptureSuccess,
            btlCaptureCuePlayed;
extern uint32_t btlCaptureStartedAt, btlWinUntil;
extern uint8_t btlMenu, btlMsgCount;
extern PartyMon btlWildMon, capturedMon;
extern Combatant btlFoe;
extern bool btlWild;
uint8_t btlCaptureStageAt(uint32_t now);
uint8_t btlCaptureFoeAct(uint8_t stage);
void btlUpdateCapture(uint32_t now);
void battleTap(int16_t x, int16_t y);
bool btlAttemptFoeRun(uint8_t roll);

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  constexpr uint32_t T0 = 1000;
  constexpr uint32_t CENTER = 600, THROW = 650, ABSORB = 350,
                     SHAKE = 1350, RESULT = 700, RETURN = 600;
  btlCaptureAnimating = true;
  btlCaptureSuccess = true;
  btlCaptureStartedAt = T0;
  battleOpen = true;
  btlWinUntil = 0;
  btlMenu = 0;

  ck(btlCaptureStageAt(T0) == 1, "the animation first centres the wild creature");
  ck(btlCaptureStageAt(T0 + CENTER) == 2, "the throw begins after centring");
  ck(btlCaptureStageAt(T0 + CENTER + THROW) == 3,
     "the throw reaches the absorb beat");
  ck(btlCaptureStageAt(T0 + CENTER + THROW + ABSORB) == 4,
     "the closed ball enters the shake beat");
  ck(btlCaptureStageAt(T0 + CENTER + THROW + ABSORB + SHAKE) == 5,
     "a successful roll reaches its distinct finish");
  battleTap(150, 300);
  ck(btlMenu == 0, "battle input stays locked during capture");

  btlWildMon = PartyMon();
  btlWildMon.dex = 25;
  btlWildMon.level = 42;
  uint32_t resultAt = T0 + CENTER + THROW + ABSORB + SHAKE;
  btlUpdateCapture(resultAt + RESULT - 1);
  ck(btlCaptureAnimating && !btlWinUntil && battleOpen,
     "success does not settle before its finish is visible");
  btlUpdateCapture(resultAt + RESULT);
  ck(!btlCaptureAnimating && btlWinUntil && !battleOpen,
     "success settles into the reward page after the visible finish");
  ck(capturedMon.dex == btlWildMon.dex, "success preserves the caught creature");

  btlCaptureAnimating = true;
  btlCaptureSuccess = false;
  btlCaptureCuePlayed = false;
  btlCaptureStartedAt = T0;
  battleOpen = true;
  btlWinUntil = 0;
  btlOver = false;
  btlMsgCount = 0;
  btlFoe = Combatant();
  btlFoe.maxHp = 100;
  btlFoe.hp = 40;
  for (uint8_t i = 0; i < SI_COUNT; i++) btlFoe.base[i] = 100;
  ck(btlCaptureStageAt(resultAt) == 6,
     "a failed roll reaches its distinct break-free finish");
  ck(btlCaptureFoeAct(6) == PMD_ATTACK,
     "the break-free finish plays the wild creature's angry action");
  btlUpdateCapture(resultAt);
  bool boosted = btlFoe.angry;
  for (uint8_t i = 0; i < SI_COUNT; i++)
    boosted = boosted && battleEffectiveStat(btlFoe, i) == 105 && btlFoe.base[i] == 100;
  ck(boosted && btlFoe.hp == 40 && btlFoe.maxHp == 100,
     "anger raises five battle stats by five percent without changing HP");
  btlUpdateCapture(resultAt + 1);
  ck(battleEffectiveStat(btlFoe, SI_ATK) == 105,
     "anger remains non-stacking after repeated failed-capture updates");
  ck(btlCaptureStageAt(resultAt + RESULT) == 7,
     "the wild creature returns only after the failure finish");
  ck(btlCaptureFoeAct(7) == PMD_ATTACK,
     "the wild creature stays angry while returning to its battle position");
  btlUpdateCapture(resultAt + RESULT + RETURN - 1);
  ck(btlCaptureAnimating && battleOpen && !btlMsgCount,
     "battle UI stays hidden until the return movement completes");
  btlUpdateCapture(resultAt + RESULT + RETURN);
  ck(!btlCaptureAnimating && battleOpen && btlMsgCount,
     "failure restores battle UI and resolves the turn after returning");
  btlWild = true;
  btlOver = false;
  btlMsgCount = 0;
  btlFoe.hp = 41;
  ck(btlAttemptFoeRun(4) && btlOver,
     "the angry escape bonus reaches the real wild-battle caller");
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
