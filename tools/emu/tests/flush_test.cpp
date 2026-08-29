// Every screen must flush, or the panel freezes on the previous frame.
// Headless screenshots CANNOT catch this: shotMode reads gfx->buffer()
// directly and never looks at frameReady, which is exactly how the training
// submenu shipped frozen.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "quiz.h"
#include "ui_art.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
// wasPressed lives in the sketch and millis() in clock.cpp; both are linked in
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render(); void renderBootSplash();
void onSwipe(int dir);
void battleTap(int16_t x, int16_t y);
uint8_t uiCurrentScreen();
bool uiScreenContinuous(uint8_t screen);
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern QuizRuntime quiz;
extern bool cardOpen, natureInfoOpen, galleryOpen, clockOpen, kbOpen, menuOpen,
            navMenuOpen, bagOpen, boxOpen, breedingOpen, partyPick;
extern bool trainOpen, movePickOpen, battleOpen, gymOpen, playerOpen;
extern int16_t galleryDetail;
extern uint32_t btlWinUntil;
extern bool uiRenderDirty;
extern uint32_t lastRender;
extern uint8_t cardPage;
extern uint8_t breedingView;
void startBattle(int16_t dex, uint8_t lvl);

static int bad = 0;
static void clearAll(){
  cardOpen=natureInfoOpen=galleryOpen=clockOpen=kbOpen=menuOpen=navMenuOpen=bagOpen=boxOpen=breedingOpen=partyPick=false;
  trainOpen=movePickOpen=battleOpen=gymOpen=playerOpen=false;
  breedingView=0; party.breeding.status=BREEDING_IDLE;
  galleryDetail=0; btlWinUntil=0;
}
static void check(const char *name){
  gfx->frameReady = false;
  gfx->fullBlackClears = 0;
  render();
  if (!gfx->frameReady) { printf("FAIL  %-12s never flushed -- the panel would freeze\n", name); bad++; }
  else if (gfx->fullBlackClears) {
    printf("FAIL  %-12s clears the shared framebuffer to black before redraw\n", name);
    bad++;
  } else printf("PASS  %-12s flushes without a black intermediate frame\n", name);
}

// The crash breadcrumb has to name the screen that is ACTUALLY drawn, or a
// crash report points at the wrong one -- which is worse than no report, since
// it sends the next person hunting in the wrong file. Checked here because
// this test already knows how to put each screen up.
static void crumbIs(const char *want){
  render();
  const char *got = SCREEN_NAME[uiCurrentScreen()];
  if (strcmp(got, want)) {
    printf("FAIL  crumb says '%s' while '%s' is on the panel\n", got, want); bad++;
  } else printf("PASS  crumb   %-12s named correctly\n", want);
}
int main(){
  setup();
  gfx->frameReady = false;
  renderBootSplash();
  size_t lit = 0;
  for (size_t i = 0; i < 466UL * 466UL; i++)
    if (gfx->buffer()[i] != RGB565_BLACK) lit++;
  if (!gfx->frameReady || lit < 10000) {
    printf("FAIL  boot splash is blank or never flushed\n"); bad++;
  } else printf("PASS  boot splash draws and flushes\n");
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 60UL*60;

  clearAll(); bool mainAnimated = uiScreenContinuous(uiCurrentScreen());
  clearAll(); cardOpen = true; bool cardAnimated = uiScreenContinuous(uiCurrentScreen());
  clearAll(); bagOpen = true; bool bagStatic = !uiScreenContinuous(uiCurrentScreen());
  clearAll(); boxOpen = true; bool boxStatic = !uiScreenContinuous(uiCurrentScreen());
  clearAll(); breedingOpen = true;
  bool breedingStatic = !uiScreenContinuous(uiCurrentScreen());
  party.breeding.status = BREEDING_RUNNING;
  bool breedingRunning = uiScreenContinuous(uiCurrentScreen());
  clearAll(); clockOpen = true; bool clockStatic = !uiScreenContinuous(uiCurrentScreen());
  clearAll(); galleryOpen = true; galleryDetail = 0;
  bool galleryGridStatic = !uiScreenContinuous(uiCurrentScreen());
  galleryDetail = 1;
  bool galleryDetailAnimated = uiScreenContinuous(uiCurrentScreen());
  clearAll(); battleOpen = true; btlWinUntil = 1;
  bool winStatic = !uiScreenContinuous(uiCurrentScreen());
  btlWinUntil = 0;
  if (!mainAnimated || !cardAnimated || !bagStatic || !boxStatic ||
      !breedingStatic || !breedingRunning || !clockStatic ||
      !galleryGridStatic || !galleryDetailAnimated || !winStatic) {
    printf("FAIL  frame scheduler misclassifies animated and static screens\n"); bad++;
  } else printf("PASS  frame scheduler redraws animated screens and idles static screens\n");

  clearAll(); bagOpen = true; uiRenderDirty = true;
  lastRender = millis() - 101; gfx->frameReady = false; loop();
  bool initialBagFrame = gfx->frameReady;
  lastRender = millis() - 101; gfx->frameReady = false; loop();
  bool idleBagFrame = gfx->frameReady;
  uiRenderDirty = true;
  lastRender = millis() - 101; gfx->frameReady = false; loop();
  bool dirtyBagFrame = gfx->frameReady;
  if (!initialBagFrame || idleBagFrame || !dirtyBagFrame) {
    printf("FAIL  static screen scheduler does not honor dirty state\n"); bad++;
  } else printf("PASS  static screen redraws once, idles, then redraws when dirty\n");

  // A page mutation owns its redraw. Requiring the touch dispatcher to mark
  // the screen dirty leaves every static paged screen blank or stale when the
  // same navigation action comes from another input path.
  clearAll(); cardOpen = true; cardPage = 0; uiRenderDirty = true;
  lastRender = millis() - 101; loop();
  gfx->frameReady = false;
  gfx->fullBlackClears = 0;
  onSwipe(-1);
  lastRender = millis() - 101; loop();
  size_t pageInk = 0;
  for (size_t i = 0; i < 466UL * 466UL; i++)
    if (gfx->buffer()[i] != RGB565_BLACK) pageInk++;
  if (cardPage != 1 || !gfx->frameReady || gfx->fullBlackClears || pageInk < 10000) {
    printf("FAIL  horizontal page change does not submit a complete non-black frame\n"); bad++;
  } else printf("PASS  horizontal page change submits a complete non-black frame\n");

  clearAll(); check("main");
  clearAll(); trainOpen=true;    check("train");
  clearAll(); movePickOpen=true; check("movepick");
  clearAll(); gymOpen=true;      check("gyms");
  clearAll(); playerOpen=true;   check("player");
  clearAll(); menuOpen=true;     check("menu");
  clearAll(); navMenuOpen=true;  check("navmenu");
  clearAll(); bagOpen=true;      check("bag");
  clearAll(); boxOpen=true;      check("box");
  clearAll(); breedingOpen=true; check("breeding");
  clearAll(); clockOpen=true;    check("clock");
  for (uint8_t p=0;p<4;p++){ clearAll(); cardOpen=true; cardPage=p;
    char n[16]; snprintf(n,sizeof(n),"card%u",p); check(n); }
  clearAll(); cardOpen=true; natureInfoOpen=true; check("natureinfo");
  clearAll(); startBattle(9,50);
  gfx->getFramebuffer()[10 * 466 + 233] = 0x1234;
  check("battle");
  uint16_t battleTop = gfx->buffer()[10 * 466 + 233];
  if (battleTop == 0x1234 || battleTop == UI_BG_DAY) {
    printf("FAIL  battle does not extend its backdrop across the visible top cap\n");
    bad++;
  } else {
    printf("PASS  battle backdrop covers the visible top cap\n");
  }
  gfx->fullBlackClears = 0;
  battleTap(149, 308);  // FIGHT through the real battle tap dispatcher
  render();
  if (gfx->fullBlackClears) {
    printf("FAIL  battle action clears the live framebuffer to black before redraw\n");
    bad++;
  } else {
    printf("PASS  battle action redraw keeps a valid frame throughout\n");
  }
  clearAll(); quiz.config.choiceWeight=0;
  quiz.begin("en-US"); check("quiz"); quiz.active=false;

  clearAll();                       crumbIs("main");
  clearAll(); trainOpen=true;       crumbIs("train");
  clearAll(); menuOpen=true;        crumbIs("menu");
  clearAll(); navMenuOpen=true;     crumbIs("menu");
  clearAll(); bagOpen=true;         crumbIs("bag");
  clearAll(); boxOpen=true;         crumbIs("box");
  clearAll(); breedingOpen=true;    crumbIs("breeding");
  clearAll(); gymOpen=true;         crumbIs("gym");
  clearAll(); playerOpen=true;      crumbIs("player");
  clearAll(); movePickOpen=true;    crumbIs("movepick");
  clearAll(); clockOpen=true;       crumbIs("clock");
  clearAll(); cardOpen=true;        crumbIs("card");
  clearAll(); startBattle(9,50);    crumbIs("battle");
  clearAll(); quiz.config.choiceWeight=0;
  quiz.begin("en-US"); crumbIs("quiz"); quiz.active=false;
  clearAll();

  printf("%s\n", bad ? "FAILURES" : "every screen flushes");
  return bad?1:0;
}
