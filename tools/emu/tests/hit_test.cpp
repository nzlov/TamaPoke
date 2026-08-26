// Where the battle grid can actually be TAPPED, as opposed to where it is drawn.
//
// This exists because of a hardware report: the two bottom buttons were much
// harder to press than the top two. The emulator could never have found it --
// its taps are exact coordinates, so a cell that is one pixel tall still passes
// every synthetic test. A finger is not exact, and the drawn cells are 44 px
// tall with an 8 px dead gap between the rows and nothing live beneath them.
//
// So the thing worth asserting is not "a tap in the middle works", it is that
// there are NO DEAD PIXELS between the cells and that the bottom row is not
// smaller than the top row.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "i18n.h"
#include "quiz.h"
#include <cstdio>
uint32_t g_seed=4; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}

void setup(); void render(); void battleTap(int16_t,int16_t);
void updateQuiz(uint32_t now);
bool beginBattleQuiz(uint8_t moveSlot);
extern Pet pet;
extern QuizRuntime quiz;
extern bool battleOpen;
extern uint8_t choiceKind;
extern uint8_t btlMenu;
extern Combatant btlYou;
void startBattle(int16_t dex, uint8_t lvl);
int btlCellIndexAt(int16_t x, int16_t y);
void uiButtonHeights(int *out, int max, int *n);
void gymHeaderRects(int *pillTop, int *pillBot, int *rowTop);
void gymPickerFooterRects(int *rowBottom, int *lanTop, int *lanBottom,
                          int *dotsTop, int *dotsBottom, int *backTop);
void moveRowVerticals(int *rowBottom, int *nameTop, int *nameBottom,
                      int *chipTop, int *chipBottom,
                      int *metaTop, int *metaBottom);
void choiceDialogVerticals(int *titleBottom, int *costTop, int *costBottom,
                           int *button1Top, int *button1Bottom,
                           int *button2Top, int *button2Bottom);
int uiSleepButton(int *cx, int *cy);
void uiEggPillRect(int *x, int *y, int *w, int *h, bool hitArea);
bool uiButtonDisabled(int i);
void uiButtonAt(int i, int *cx, int *cy, int *half);
void quizOptionRect(uint8_t option, int *x, int *y, int *w, int *h);
void quizKeyRect(uint8_t row, uint8_t column, int *x, int *y, int *w, int *h);
extern Pet pet;
void onTap(int16_t x, int16_t y);

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static bool answerActiveQuiz() {
  if (!quiz.active) return false;
  uint32_t now = millis();
  quiz.markRendered(now);
  bool answered = false;
  if (quiz.kind == QUIZ_QUESTION_CHOICE) {
    answered = quiz.choose(quiz.choice.correctIndex, now);
  } else {
    snprintf(quiz.input, sizeof(quiz.input), "%s", quiz.expected);
    answered = quiz.submit(now);
  }
  if (!answered) return false;
  updateQuiz(quiz.feedbackUntil);
  return !quiz.active;
}

static bool rectInsideRoundPanel(int x, int y, int w, int h) {
  const int points[4][2] = {{x, y}, {x + w, y}, {x, y + h}, {x + w, y + h}};
  for (const auto &point : points) {
    int dx = point[0] - 233, dy = point[1] - 233;
    if (dx * dx + dy * dy > 231 * 231) return false;
  }
  return true;
}

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 50UL*MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  quiz.config.choiceWeight = 0;

  // Sweep the whole grid area and record which cell each pixel belongs to.
  const int X0 = 40, X1 = 430, Y0 = 258, Y1 = 410;
  int area[4] = {0,0,0,0};
  int overlap = 0;
  int gapRow = 0, gapCol = 0;
  for (int y = Y0; y <= Y1; y++)
    for (int x = X0; x <= X1; x++) {
      int hits = 0, which = -1;
      for (int i = 0; i < 4; i++)
        if (btlCellIndexAt(x, y) == i) { hits++; which = i; }
      // btlCellIndexAt returns the FIRST match, so overlap is found by asking
      // each cell directly instead
      (void)hits;
      if (which >= 0) area[which]++;
    }
  ck(area[0] > 0 && area[1] > 0 && area[2] > 0 && area[3] > 0,
     "all four cells are reachable");
  printf("      areas: %d %d %d %d px\n", area[0], area[1], area[2], area[3]);

  // THE POINT: the bottom row must be at least as easy to hit as the top row.
  ck(area[2] >= area[0] && area[3] >= area[1],
     "the bottom row is not smaller than the top row");

  // no dead horizontal seam between the rows
  int seam = 0;
  for (int y = 316; y <= 328; y++)
    if (btlCellIndexAt(200, y) < 0) seam++;
  ck(seam == 0, "no dead gap between the two rows");
  (void)gapRow; (void)gapCol; (void)overlap;

  // no dead vertical seam between the columns
  int vseam = 0;
  for (int x = 226; x <= 240; x++)
    if (btlCellIndexAt(x, 300) < 0) vseam++;
  ck(vseam == 0, "no dead gap between the two columns");

  // a tap just below the bottom row still counts -- that is where a finger
  // reaching for the lowest button actually lands
  ck(btlCellIndexAt(150, 372) == 2 && btlCellIndexAt(320, 372) == 3,
     "a low tap still reaches the bottom row");

  // The four boxes must TILE: the seam between them is one pixel wide, owned by
  // exactly one side. Checked at the boundaries rather than by re-deriving the
  // rectangles here -- a test that copies the geometry drifts from it.
  ck(btlCellIndexAt(232, 300) == 0 && btlCellIndexAt(233, 300) == 1,
     "the column seam belongs to exactly one side");
  ck(btlCellIndexAt(200, 321) == 0 && btlCellIndexAt(200, 322) == 2,
     "and so does the row seam");

  // finally, drive a real tap low in the bottom-left cell through battleTap
  startBattle(9, 50);
  btlYou.moves[2] = btlYou.moves[0];
  btlMenu = 1;
  uint8_t before = btlMenu;
  battleTap(150, 372);
  ck(btlMenu != before, "a low tap in the move grid is actually accepted");
  ck(quiz.active, "an accepted battle move opens its question before resolving");
  ck(answerActiveQuiz(), "answer feedback resumes the pending battle move");

  startBattle(9, 50);
  quiz.config.questionTypes = 0;
  ck(beginBattleQuiz(0) && !quiz.active,
     "a disabled battle question resolves the move without a popup");
  quiz.config.questionTypes = QUIZ_TYPE_ARITHMETIC;

  // Three separate "hard to hit" reports came in from the board, all the same
  // mistake: a button sized to fit its label rather than a finger. This holds
  // every primary control to one minimum so the fourth report does not happen.
  {
    int h[8], n = 0;
    uiButtonHeights(h, 8, &n);
    int small = 0;
    for (int i = 0; i < n; i++) if (h[i] < 44) small++;
    printf("      button heights:");
    for (int i = 0; i < n; i++) printf(" %d", h[i]);
    printf(" px\n");
    ck(small == 0, "every primary button is at least 44 px tall");
  }

  {
    bool safe = true;
    bool largeEnough = true;
    for (uint8_t row = 0; row < 4; row++)
      for (uint8_t column = 0; column < 4; column++) {
        int x, y, w, h;
        quizKeyRect(row, column, &x, &y, &w, &h);
        safe &= rectInsideRoundPanel(x, y, w, h);
        largeEnough &= w >= 44 && h >= 44;
      }
    for (uint8_t option = 0; option < 4; option++) {
      int x, y, w, h;
      quizOptionRect(option, &x, &y, &w, &h);
      safe &= rectInsideRoundPanel(x, y, w, h);
      largeEnough &= w >= 44 && h >= 44;
    }
    ck(safe, "quiz controls stay inside the round panel");
    ck(largeEnough, "quiz controls remain full-size touch targets");
  }

  {
    int pt, pb, rt;
    gymHeaderRects(&pt, &pb, &rt);
    ck(pb < rt, "the gym difficulty pill does not sit on the first leader row");
    printf("      pill %d..%d, first row at %d\n", pt, pb, rt);
  }

  {
    int rb, lt, lb, dt, db, bt;
    gymPickerFooterRects(&rb, &lt, &lb, &dt, &db, &bt);
    ck(rb < lt, "the gym rows leave room for the LAN button");
    ck(lb < dt, "the LAN button does not overlap the page dots");
    ck(db < bt, "the page dots do not overlap BACK");
    printf("      gym footer: rows..%d, LAN %d..%d, dots %d..%d, BACK %d\n",
           rb, lt, lb, dt, db, bt);
  }

  {
    setLang((Lang)uiFindLocale("zh-CN"));
    int rb, nt, nb, ct, cb, mt, mb;
    moveRowVerticals(&rb, &nt, &nb, &ct, &cb, &mt, &mb);
    ck(nb <= ct, "the move name does not collide with its metadata");
    ck(ct <= mt && mb <= cb, "the Chinese type label stays inside its chip");
    ck(cb <= rb, "the move metadata stays inside its row");
    printf("      move row: name %d..%d, chip %d..%d, meta %d..%d, row..%d\n",
           nt, nb, ct, cb, mt, mb, rb);
    setLang((Lang)uiFindLocale("en-US"));
  }

  {
    int tb, ct, cb, b1t, b1b, b2t, b2b;
    choiceDialogVerticals(&tb, &ct, &cb, &b1t, &b1b, &b2t, &b2b);
    ck(tb <= ct, "the retirement explanation starts below its title");
    ck(cb < b1t, "the retirement explanation does not overlap its first button");
    ck(b1b < b2t, "the retirement buttons do not overlap");
    printf("      choice dialog: title..%d, cost %d..%d, buttons %d..%d %d..%d\n",
           tb, ct, cb, b1t, b1b, b2t, b2b);
  }

  // While the pet sleeps only ONE home icon works, and it must be the LIGHT.
  // Removing the ball icon shifted every index by one and left drawButtons()
  // lighting index 2, which had become the BATH -- so the wash button looked
  // like the wake-up button.
  {
    battleOpen = false;        // the battle above owns every tap until it closes
    const int BTN_LIGHT_IDX = uiSleepButton(nullptr, nullptr);
    int lx = 0, ly = 0;
    uiSleepButton(&lx, &ly);
    if (pet.awaitingStarter()) pet.chooseStarter(4);
    if (pet.isEgg()) pet.dbgHatchAs(6, false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    if (!pet.sleeping) pet.toggleLight();
    ck(pet.sleeping, "the pet is asleep");
    // the bath icon must NOT wake it
    int bx = 0, by = 0;
    uiButtonAt(2, &bx, &by, nullptr);
    onTap((int16_t)bx, (int16_t)by);
    ck(pet.sleeping, "the bath icon does not wake a sleeping pet");
    onTap(233, 200);
    ck(!quiz.active, "a sleeping pet does not open a caress question");
    // and what is drawn greyed is exactly what is refused: one answer, so the
    // dimming can never point at a different icon than the tap handler does
    bool grey[4];
    for (int i = 0; i < 4; i++) grey[i] = uiButtonDisabled(i);
    ck(!grey[BTN_LIGHT_IDX] && grey[0] && grey[2] && grey[3],
       "and it is the only icon drawn lit while asleep");

    // the light icon must
    onTap((int16_t)lx, (int16_t)ly);
    ck(!pet.sleeping, "the light icon does");
    for (int i = 0; i < 4; i++)
      if (uiButtonDisabled(i)) bad++, printf("FAIL  icon %d still greyed awake\n", i);
    ck(true, "and awake, every icon is live again");
  }

  // the home icons must not overlap each other now that they are bigger
  {
    int worst = 9999;
    for (int i = 0; i + 1 < 4; i++) {
      int ax, ay, ah, bx2, by2, bh;
      uiButtonAt(i, &ax, &ay, &ah);
      uiButtonAt(i + 1, &bx2, &by2, &bh);
      int gap = (bx2 - bh) - (ax + ah);
      if (gap < worst) worst = gap;
    }
    printf("      smallest gap between home icons: %d px\n", worst);
    ck(worst >= 0, "the home icons do not overlap");
  }

  // THE EGG REGION PILL. Missing it fell through to pet.eggTap(), and three
  // taps hatch -- so fumbling at the region selector hatched the egg you were
  // trying to re-aim. Reported from a board: "i wasnt able to change egg
  // setting. it kept hatching the egg".
  {
    int gx, gy, gw, gh, hx, hy, hw, hh;
    uiEggPillRect(&gx, &gy, &gw, &gh, false);
    uiEggPillRect(&hx, &hy, &hw, &hh, true);
    printf("      pill %dx%d, hit area %dx%d\n", gw, gh, hw, hh);
    ck(hw > gw && hh > gh, "the pill's hit area is bigger than the pill");
    ck(hh >= 44 && hw >= 44, "and is at least UI_TAP_MIN across");

    // A fresh egg, then taps in the DEAD GUARD BAND -- past the hit area but
    // inside the swallow zone, which is where a fumbled re-aim actually lands.
    //
    // These offsets used to be 8 px, which is INSIDE the hit area (EGGREG_PAD
    // is 16), so every "near miss" was a direct hit that cycled the region.
    // The region assertion passed only because 12 taps over regionCount() 4
    // came full circle back to the start; Sinnoh made it 5 and the coincidence
    // died. It never once exercised the guard band it claims to protect --
    // CLAUDE.md trap 3, a test proving the arithmetic rather than the firmware.
    //
    // Derived from the two rects above, NOT from EGGREG_PAD/EGGREG_GUARD: those
    // are #defines inside the sketch and copying them here would put a second
    // copy of a firmware constant in the test, which is the same mistake in a
    // different place. (gx - hx) IS the pad, so a few px past it is outside the
    // hit area and inside the swallow zone beyond it.
    const int band = (gx - hx) + 4;
    pet.newEgg();
    while (!pet.isEgg()) pet.newEgg();
    uint8_t wasRegion = pet.region;
    int changed = 0, hatched = 0;
    for (int i = 0; i < 6; i++) {
      // checked after EVERY tap, so no number of taps can cancel out again
      onTap((int16_t)(gx + gw / 2), (int16_t)(gy + gh + band));  // just below
      if (pet.region != wasRegion) changed++;
      if (!pet.isEgg()) hatched++;
      onTap((int16_t)(gx - band), (int16_t)(gy + gh / 2));       // just left
      if (pet.region != wasRegion) changed++;
      if (!pet.isEgg()) hatched++;
    }
    ck(hatched == 0, "a near miss on the pill does NOT hatch the egg");
    ck(changed == 0, "and does not silently change the region either");
    ck(band > (gx - hx) && band > (gy - hy),
       "and those taps really were outside the hit area, not on the pill");

    // on the pill: it cycles
    onTap((int16_t)(gx + gw / 2), (int16_t)(gy + gh / 2));
    ck(pet.region != wasRegion, "tapping the pill really does change the region");
    ck(pet.isEgg(), "and never cracks the egg while doing it");

    // and the egg itself still hatches when you actually tap the egg
    for (int i = 0; i < 4 && pet.isEgg(); i++) onTap(233, 200);
    ck(!pet.isEgg(), "tapping the egg still hatches it");
  }

  // The runaway fires on the tap, with NO dialog, and that is deliberate: a
  // creature you have to authorise to leave is not at stake, and neglect
  // having teeth is the premise. What must never happen is REACHING this state
  // by going to sleep -- night_test covers that end of it.
  {
    battleOpen = false;
    if (pet.awaitingStarter()) pet.chooseStarter(4);
    if (pet.isEgg()) pet.dbgHatchAs(147, false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    if (pet.sleeping) pet.toggleLight();
    pet.dbgRunawayReady();
    ck(pet.canRunawayNow(), "total neglect really does make it ready to leave");
    onTap(233, 200);
    ck(pet.ceremony != CER_NONE, "and the tap lets it go, without asking");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
