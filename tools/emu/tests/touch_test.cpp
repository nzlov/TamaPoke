// Headless proof that a click reaches onTap(). Replaces main_sdl.cpp's main()
// and runtime globals, then drives the REAL setup()/loop()/handleTouch() by
// poking the same touch globals the SDL event loop pokes.
//
// Build twice: with and without -DNO_IRQ, to A/B the emuFireInterrupt() fix.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "input_coords.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "moves.h"
#include "battle.h"
#include "content.h"
#include "inventory.h"
#include "items.h"
#include "trainers.h"
#include "quiz.h"
#include <chrono>
#include <cstring>
#include <thread>
#include <string>
#include <deque>

uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

// millis() deliberately NOT defined here: clock.cpp is linked in so this
// exercises the real emulator clock, --fast handling included.
void FakeESP::restart() { exit(0); }

// No stdin console in this harness: the serial path is not what is under test.
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void loop();
extern Pet pet;   // defined in the sketch
extern bool trainOpen, sackOpen, gameOpen, menuOpen, cardOpen, moveInfoOpen, movePickOpen, spdOpen;
extern bool bagOpen, boxOpen, playerOpen, lanOpen, galleryOpen, kbOpen, clockOpen;
extern uint8_t movePickSlot, movePickPage;
extern bool battleOpen, btlOver, btlWon, btlWild;
extern bool btlFoeDetailOpen;
extern uint8_t btlFoeDetailPage;
extern PartyMon btlWildMon;
extern Combatant btlYou, btlFoe;
extern BattleField btlField;
extern QuizRuntime quiz;
extern uint8_t btlMsgCount;
extern uint8_t gymRegion, btlRegion;
void updateQuiz(uint32_t now);
void startBattle(int16_t dex, uint8_t lvl);
bool btlAttemptRun(uint8_t roll);
bool btlAttemptFoeRun(uint8_t roll);
void startTrainerBattle(uint8_t idx, bool hard);
void battleTap(int16_t x, int16_t y);
void onSwipe(int dir);
extern uint8_t btlFoeAt, btlSquadN, btlSquadAt, btlMenu;
extern uint8_t btlTargetPage;
extern ItemKey btlPendingItem;
extern Combatant btlSquad[7];
extern int8_t btlSwapWho;
extern bool btlHard;
extern bool gymOpen, gymHard; extern uint8_t gymPage;
extern bool pickOpen, pickHard; extern uint8_t pickTrainer; extern uint16_t squadMask;
uint8_t squadCap(uint8_t idx, bool hard);
uint8_t pickChosen(); uint8_t pickCandidates(); void pickDefault(uint8_t);
extern uint8_t pickPage;
#define PICK_X(i) (78 + ((i) % 2) * 160)
#define PICK_Y(i) (86 + ((i) / 2) * 80)
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
uint8_t learnableList(MoveId *out, uint8_t max);
#define MOVE_PICK_PER_PAGE 5
#define MOVE_PICK_Y(i) (76 + (i) * 58)
extern uint8_t cardPage;
extern uint16_t gRegionArt;

// handleTouch() self-gates to 50 Hz off millis(), so real time must pass
// between polls; a tight loop() spin would be swallowed by that gate.
static void pump(int times) {
  for (int i = 0; i < times; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop();
  }
}

static void fire() {
#ifndef NO_IRQ
  emuFireInterrupt();
#endif
}

// One press/release at (x,y), driven exactly as the SDL event loop drives it.
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true;
  fire();
  pump(5);                       // finger held: gesture starts
  g_touchDown = false;
  fire();
  pump(3);                       // finger up: gesture resolves as a tap
}

// Direct calls skip the physical press/release time represented by click().
static void directBattleTap(int x, int y) {
  std::this_thread::sleep_for(std::chrono::milliseconds(310));
  battleTap(x, y);
}

static MoveId findMove(const char *name) {
  for (MoveId id = 1; id < moveCount(); id++)
    if (!std::strcmp(moveEntry(id).name, name)) return id;
  return MOVE_NONE;
}

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

static int bestMoveSlot() {
  MoveId best = aiChooseMove(btlYou, btlFoe, btlField, true);
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (btlYou.moves[i] == best) return i;
  }
  return -1;
}

void uiButtonAt(int i, int *cx, int *cy, int *half);

int main(int argc, char **argv) {
  // Niri at 1.5x scale turns the requested 932px SDL window into 621 logical
  // pixels. Pointer coordinates must use that live extent, not `--scale 2`.
  if (emuPanelCoord(0, 621, 466) != 0 ||
      emuPanelCoord(311, 621, 466) != 233 ||
      emuPanelCoord(620, 621, 466) != 465 ||
      emuPanelCoord(466, 932, 466) != 233) {
    printf("FAIL: SDL window coordinates do not map onto the whole panel\n");
    return 1;
  }
  printf("PASS: SDL window coordinates cover the whole panel\n");

  if (argc > 1) emuSetTimeScale((uint32_t)atoi(argv[1]));
  printf("--- time scale x%u ---\n", emuTimeScale());
  setup();
  // This suite checks touch routing, while sprite availability is covered by
  // swipe_test. Keep first boot selectable with the empty-art pack fixture.
  gRegionArt = 0xFFFF;
  pump(4);
  quiz.config.choiceWeight = 0;

  if (!pet.awaitingStarter()) { printf("FAIL: not on the starter screen\n"); return 1; }
  printf("on starter screen, awaitingStarter=1\n");

  // First boot chooses a language, then a region, then a starter. The dedicated
  // language suite owns its content assertions; this touch journey selects row 0.
  click(149, 131);
  pump(2);
  if (!pet.awaitingStarter()) { printf("FAIL: the language tap chose a starter\n"); return 1; }

  // Pick KANTO so row 0 below is Bulbasaur.
  click(233, 108 + 30);
  pump(2);
  if (player.region != 0) { printf("FAIL: region tap did not land\n"); return 1; }
  if (!pet.awaitingStarter()) { printf("FAIL: the region tap chose a starter\n"); return 1; }
  printf("picked KANTO, still on the starter flow\n");

  // Negative control: below the three rows (they span y 110..344). If this
  // "selects" a starter the test proves nothing about coordinate routing.
  click(233, 420);
  if (!pet.awaitingStarter()) { printf("FAIL: off-row click selected a starter\n"); return 1; }
  printf("click at (233,420) off the rows: correctly ignored\n");

  // row 0 == Kanto's first starter == Bulbasaur. Centre of the row: x 70..396, y 110..180.
  click(233, 145);
  printf("click at (233,145) on row 0: awaitingStarter=%d\n", (int)pet.awaitingStarter());

  if (pet.awaitingStarter()) { printf("FAIL: click did not register\n"); return 1; }
  printf("PASS: tap reached onTap and picked a starter\n");

  // --fast must still do its job: with no finger down, game time has to run at
  // the scale. Otherwise the touch fix would have quietly neutered the flag.
  uint32_t t0 = millis();
  auto w0 = std::chrono::steady_clock::now();
  pump(20);                      // ~600 ms of real time, idle
  uint32_t gameMs = millis() - t0;
  uint64_t realMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - w0).count();
  double ratio = realMs ? (double)gameMs / (double)realMs : 0.0;
  printf("idle: %llu ms real -> %u ms game (x%.1f, want x%u)\n",
         (unsigned long long)realMs, gameMs, ratio, emuTimeScale());
  if (ratio < emuTimeScale() * 0.8 || ratio > emuTimeScale() * 1.2) {
    printf("FAIL: --fast no longer accelerates idle game time\n");
    return 1;
  }
  printf("PASS: --fast still accelerates idle game time\n");

  // ---- new UI routing: 5th icon -> training submenu -> the two live trainers,
  // and the menu's STATS row -> the stats card page. Both changed index
  // mappings inside existing handlers, which is where an off-by-one would hide.
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  pet.ageMinutes = 99 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  pump(4);
  pump(2);
  int tbx, tby; uiButtonAt(3, &tbx, &tby, nullptr);   // ask, do not hardcode:
  click(tbx, tby);                       // the dumbbell, the 4th of 4
  if (!trainOpen) { printf("FAIL: train icon did not open the submenu\n"); return 1; }
  printf("PASS: 5th icon opens the training submenu\n");

  click(233, 178);                       // row 0 == STRENGTH == TRAIN_ROW_Y(0)+28
  if (!sackOpen) { printf("FAIL: STRENGTH row did not start the sack\n"); return 1; }
  printf("PASS: STRENGTH routes to the punching bag\n");
  sackOpen = false;

  click(tbx, tby);
  click(233, 242);                       // row 1 == SPEED == TRAIN_ROW_Y(1)+28
  if (!spdOpen) { printf("FAIL: SPEED row did not start the reaction test\n"); return 1; }
  printf("PASS: SPEED routes to the reaction test\n");
  spdOpen = false;

  click(tbx, tby);
  click(233, 306);                       // row 2 == DEFENCE -> the ball game
  if (!gameOpen) { printf("FAIL: DEFENCE row did not start the ball game\n"); return 1; }
  printf("PASS: DEFENCE routes to the ball game\n");
  gameOpen = false;
  trainOpen = false;

  click(233, 82);                        // relocated name band opens the menu
  if (!menuOpen) { printf("FAIL: name band did not open the menu\n"); return 1; }
  click(233, 104 + 16 + 22);             // row 0 is disabled for the current lead
  if (!menuOpen) { printf("FAIL: LEADING row accepted a tap\n"); return 1; }
  printf("PASS: menu LEADING row is disabled\n");

  // ---- moves card page -> picker -> slot actually changes, with no duplicates
  menuOpen = false;
  cardOpen = true;
  cardPage = 2;
  MoveId before = pet.moves[1];
  click(233, 154);                       // slot 1 == MOVE_ROW_Y(1)
  if (!moveInfoOpen) { printf("FAIL: tapping a move slot did not show its description\n"); return 1; }
  printf("PASS: tapping a move slot opens its description\n");
  click(233, 346);                       // explicit CHANGE button
  if (!movePickOpen) { printf("FAIL: CHANGE did not open the picker\n"); return 1; }
  printf("PASS: CHANGE opens the picker\n");

  click(233, 100);                       // first row of the picker
  if (movePickOpen) { printf("FAIL: picking a move left the picker open\n"); return 1; }
  if (pet.moves[1] == before) { printf("FAIL: slot 1 did not change\n"); return 1; }
  printf("PASS: picking a move replaces the slot (%s -> %s)\n",
         before ? moveEntry(before).name : "-",
         pet.moves[1] ? moveEntry(pet.moves[1]).name : "-");

  // the swap must never leave the same move in two slots
  bool dupe = false;
  for (int i = 0; i < MOVE_SLOTS; i++)
    for (int j = i + 1; j < MOVE_SLOTS; j++)
      if (pet.moves[i] && pet.moves[i] == pet.moves[j]) dupe = true;
  if (dupe) {
    printf("FAIL: a move ended up in two slots\n");
    for (int i = 0; i < MOVE_SLOTS; i++)
      printf("      slot %d: %s\n", i, pet.moves[i] ? moveEntry(pet.moves[i]).name : "-");
    return 1;
  }
  printf("PASS: no duplicate move after the swap\n");

  // picking a move the pet already knows should TRADE slots, not clone it
  MoveId s0 = pet.moves[0], s2 = pet.moves[2];
  cardPage = 2;
  click(233, 212);                       // slot 2
  click(233, 346);                       // description -> CHANGE
  MoveId all[64];
  uint8_t n = learnableList(all, sizeof(all) / sizeof(all[0]));
  int page = -1, row = -1;
  for (uint8_t i = 0; i < n; i++)
    if (all[i] == s0) { page = i / MOVE_PICK_PER_PAGE; row = i % MOVE_PICK_PER_PAGE; }
  if (page >= 0) {
    movePickPage = page;
    click(233, MOVE_PICK_Y(row) + 10);
    bool ok = (pet.moves[2] == s0 && pet.moves[0] == s2);
    printf("%s: picking a known move trades the two slots (0=%s 2=%s)\n",
           ok ? "PASS" : "FAIL",
           pet.moves[0] ? moveEntry(pet.moves[0]).name : "-",
           pet.moves[2] ? moveEntry(pet.moves[2]).name : "-");
    if (!ok) return 1;
  }
  // A reserve move can be selected, and the displaced battle move moves into
  // that exact reserve slot rather than disappearing.
  MoveId reserveBefore = pet.reserveMoves[0], activeBefore = pet.moves[2];
  cardPage = 2;
  click(233, 212);
  click(233, 346);
  n = learnableList(all, sizeof(all) / sizeof(all[0]));
  page = row = -1;
  for (uint8_t i = 0; i < n; i++)
    if (all[i] == reserveBefore) { page = i / MOVE_PICK_PER_PAGE; row = i % MOVE_PICK_PER_PAGE; }
  if (page < 0) { printf("FAIL: reserve move is absent from picker\n"); return 1; }
  movePickPage = page;
  click(233, MOVE_PICK_Y(row) + 10);
  if (pet.moves[2] != reserveBefore || pet.reserveMoves[0] != activeBefore) {
    printf("FAIL: choosing a reserve did not swap active and reserve slots\n"); return 1;
  }
  printf("PASS: picker swaps a learned reserve into the battle set\n");

  // ---- battle: drive a whole fight through the real tap handler
  pet.ageMinutes = 50 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  startBattle(9, 50);
  if (!battleOpen) { printf("FAIL: battle did not start\n"); return 1; }
  printf("PASS: BATTLE opens (%s L%u vs %s L%u)\n",
         btlYou.name, btlYou.level, btlFoe.name, btlFoe.level);

  click(330, 100);
  if (btlFoeDetailOpen) {
    printf("FAIL: a non-wild opponent exposed the wild detail card\n");
    return 1;
  }
  btlWild = true;
  btlWildMon.nature = NATURE_ADAMANT;
  int16_t detailDex = btlFoe.dex;
  uint16_t detailHp = btlFoe.hp;
  MoveId detailMoves[MOVE_SLOTS];
  memcpy(detailMoves, btlFoe.moves, sizeof(detailMoves));
  click(330, 100);
  if (!btlFoeDetailOpen || btlFoeDetailPage != 0) {
    printf("FAIL: tapping the wild opponent did not open its Pokedex page\n");
    return 1;
  }
  printf("PASS: tapping the wild opponent opens its Pokedex description\n");
  onSwipe(-1);
  if (!btlFoeDetailOpen || btlFoeDetailPage != 1 ||
      btlWildMon.nature != NATURE_ADAMANT) {
    printf("FAIL: wild opponent stats page lost the individual nature\n");
    return 1;
  }
  printf("PASS: wild opponent stats include the individual's nature\n");
  onSwipe(-1);
  if (btlFoeDetailPage != 2) {
    printf("FAIL: wild opponent moves page was not reachable\n");
    return 1;
  }
  printf("PASS: wild opponent moves are reachable on the third page\n");
  pump(4);  // direct onSwipe calls omit the time a physical swipe would take
  click(233, 410);
  if (btlFoeDetailOpen || btlFoe.dex != detailDex || btlFoe.hp != detailHp ||
      memcmp(detailMoves, btlFoe.moves, sizeof(detailMoves)) != 0 || btlMenu != 0) {
    printf("FAIL: closing wild detail changed battle state\n");
    return 1;
  }
  printf("PASS: wild opponent detail is read-only and returns to the same turn\n");
  btlWild = false;

  uint16_t foeHp0 = btlFoe.hp;
  int turns = 0;
  while (battleOpen && turns < 60) {
    turns++;
    if (quiz.active) {
      if (!answerActiveQuiz()) { printf("FAIL: battle question could not be answered\n"); return 1; }
      continue;
    }
    if (btlMsgCount) { click(233, 320); continue; }   // clear narration
    if (btlMenu == 0) { click(BTL_CELL_X(0) + 40, BTL_CELL_Y(0) + 20); continue; }
    int slot = bestMoveSlot();
    if (slot < 0) break;
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
  printf("     after %d taps: you %u/%u, foe %u/%u, over=%d won=%d\n",
         turns, btlYou.hp, btlYou.maxHp, btlFoe.hp, btlFoe.maxHp,
         (int)btlOver, (int)btlWon);
  if (btlFoe.hp >= foeHp0) { printf("FAIL: tapping a move did no damage\n"); return 1; }
  printf("PASS: tapping a move resolves a turn and deals damage\n");
  if (!btlOver) { printf("FAIL: fight never concluded\n"); return 1; }
  printf("PASS: the fight concludes and closes\n");

  // ---- a trainer fight: the foe's squad must chain, and winning awards a badge
  pet.dbgHatchAs(9, false);                   // a stable favourable matchup for Brock
  pet.ageMinutes = 100 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  bool hadBadge = player.hasBadge(0, 0, false);
  startTrainerBattle(0, false);
  if (!battleOpen) { printf("FAIL: trainer battle did not start\n"); return 1; }
  printf("PASS: gym battle starts (squad of %u vs BROCK)\n", btlSquadN);

  int taps = 0;
  uint8_t sawFoeAt = 0;
  while (battleOpen && taps < 200) {
    taps++;
    if (quiz.active) {
      if (!answerActiveQuiz()) { printf("FAIL: gym question could not be answered\n"); return 1; }
      continue;
    }
    if (btlMsgCount) { click(233, 320); continue; }
    if (btlMenu == 0) { click(BTL_CELL_X(0) + 40, BTL_CELL_Y(0) + 20); continue; }
    int slot = bestMoveSlot();
    if (slot < 0) break;
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
    if (btlFoeAt > sawFoeAt) sawFoeAt = btlFoeAt;
  }
  printf("     %d taps, foe reached team index %u, won=%d\n", taps, sawFoeAt, (int)btlWon);
  if (sawFoeAt == 0) { printf("FAIL: the second gym creature never came out\n"); return 1; }
  printf("PASS: the trainer sends out its next creature on a faint\n");
  if (!btlWon) { printf("FAIL: a L100 creature lost to Brock\n"); return 1; }
  if (!player.hasBadge(0, 0, false) || hadBadge) { printf("FAIL: no badge awarded\n"); return 1; }
  printf("PASS: beating a leader awards its badge (%u/8)\n", player.badgeCount(false));

  // A trainer battle snapshots the selected region. Before that handoff was
  // added, the first opponent came from gymRegion but every later one came
  // from region 0; Katy therefore continued with Brock's team. Walk every
  // member of every packed trainer so the third through sixth slots are
  // covered as well as the originally reported second slot.
  uint16_t regionalTrainers = 0, regionalMembers = 0;
  for (uint8_t region = 0; region < regionAll(); region++) {
    const RegionBattleInfo &battle = regionBattleInfo(region);
    for (uint8_t trainer = 0; trainer < battle.trainerCount; trainer++) {
      battleOpen = false;
      gymRegion = region;
      const Trainer &expected = trainerInfo(region, trainer);
      startTrainerBattle(trainer, false);
      if (btlRegion != region || btlFoeAt != 0 ||
          btlFoe.dex != expected.team[0].dex ||
          btlFoe.level != expected.team[0].level) {
        printf("FAIL: region %u trainer %u member 0 crossed battle context\n",
               region, trainer);
        return 1;
      }
      for (uint8_t member = 1; member < expected.count; member++) {
        btlFoe.hp = 0;
        btlSwapWho = 1;
        btlMsgCount = 1;
        directBattleTap(0, 0);            // dismiss faint text and send the next one
        if (btlRegion != region || btlFoeAt != member ||
            btlFoe.dex != expected.team[member].dex ||
            btlFoe.level != expected.team[member].level) {
          printf("FAIL: region %u trainer %u member %u became dex %u L%u "
                 "instead of dex %u L%u (battle region %u)\n",
                 region, trainer, member, btlFoe.dex, btlFoe.level,
                 expected.team[member].dex, expected.team[member].level, btlRegion);
          return 1;
        }
      }
      regionalTrainers++;
      regionalMembers += expected.count;
    }
  }
  printf("PASS: all %u members of %u trainer teams stay in their own region\n",
         regionalMembers, regionalTrainers);
  battleOpen = false;
  btlMsgCount = 0;
  btlSwapWho = -1;
  gymRegion = 0;

  // ---- hard mode caps the team to the opponent's size AND level
  battleOpen = false;
  pet.ageMinutes = 100 * MINUTES_PER_LEVEL;   // a level 100 creature
  startTrainerBattle(0, true);                // BROCK: 2 mons, top level 14
  printf("     hard vs BROCK: squad %u, your lead is L%u (pet is L%u)\n",
         btlSquadN, btlYou.level, pet.level());
  if (btlYou.level != 14) {
    printf("FAIL: level not capped to the leader's top (got %u, want 14)\n", btlYou.level);
    return 1;
  }
  printf("PASS: hard mode caps your level to the leader's best\n");
  if (btlSquadN > 2) { printf("FAIL: squad not capped to 2\n"); return 1; }
  printf("PASS: hard mode caps your team size to the leader's\n");
  if (pet.level() != 100) { printf("FAIL: the stored pet was modified\n"); return 1; }
  printf("PASS: the stored creature is untouched (still L%u)\n", pet.level());
  battleOpen = false;

  // ---- switching mid-fight
  battleOpen = false; pickOpen = false; player.badges = 0;
  for (int i = 0; i < 3; i++) { PartyMon m; m.dex = 9 + i * 20; m.level = 40;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25; party.replaceAt(i, m); }
  squadMask = 0xFFFF;
  startTrainerBattle(0, false);
  if (btlSquadN < 2) { printf("FAIL: squad too small to test switching\n"); return 1; }
  printf("PASS: battle starts with a squad of %u\n", btlSquadN);

  btlMenu = 0;
  click(150, 274 + 52 + 22);            // POKEMON: lower-LEFT cell now (RUN is right)
  if (btlMenu != 2) { printf("FAIL: POKEMON did not open the switch list\n"); return 1; }
  printf("PASS: POKEMON opens the switch list\n");

  uint8_t was = btlSquadAt;
  uint16_t swFoeHp0 = btlFoe.hp;
  click(69 + 40, 286 + 22);             // slot 0 = the one already out: inert
  if (btlSquadAt != was) { printf("FAIL: switching to the active creature was allowed\n"); return 1; }
  printf("PASS: you cannot switch to the one already out\n");

  btlMenu = 2;
  click(69 + 168 + 40, 286 + 22);       // slot 1
  if (btlSquadAt == was) { printf("FAIL: the switch did not happen\n"); return 1; }
  printf("PASS: switching brings on another creature (slot %u -> %u)\n", was, btlSquadAt);
  bool costTurn = (btlYou.hp < btlYou.maxHp) || (btlFoe.hp == swFoeHp0);
  printf("     foe hp %u -> %u, your new one %u/%u\n", swFoeHp0, btlFoe.hp, btlYou.hp, btlYou.maxHp);
  if (!costTurn) { printf("FAIL: switching was free -- the foe never acted\n"); return 1; }
  printf("PASS: switching spends the turn\n");
  battleOpen = false; btlMenu = 0;
  for (int i = 0; i < 3; i++) party.releaseAt(i);

  // ---- the gym ladder is sequential: only the next one may be entered
  battleOpen = false; pickOpen = false;
  player.badges = 0;                       // nothing beaten yet
  gymOpen = true; gymHard = false; gymPage = 0;
  click(233, 110 + 2 * 50 + 20);        // MISTY, after the WILD row and BROCK
  if (pickOpen || battleOpen) { printf("FAIL: a locked leader was enterable\n"); return 1; }
  printf("PASS: a locked leader cannot be entered\n");
  click(233, 110 + 1 * 50 + 20);        // BROCK, after the WILD row
  if (!pickOpen) { printf("FAIL: the first leader was not enterable\n"); return 1; }
  printf("PASS: the first leader is always open\n");
  pickOpen = false;
  player.badges = 1;                       // Brock beaten
  gymOpen = true; gymPage = 0;
  click(233, 110 + 2 * 50 + 20);        // MISTY again
  if (!pickOpen) { printf("FAIL: beating one did not unlock the next\n"); return 1; }
  printf("PASS: beating a leader unlocks the next\n");
  pickOpen = false;
  // and the hard ladder is its own run
  gymOpen = true; gymHard = true; gymPage = 0;
  click(233, 110 + 2 * 50 + 20);
  if (pickOpen) { printf("FAIL: easy progress unlocked the hard ladder\n"); return 1; }
  printf("PASS: hard mode keeps its own unlock order\n");
  gymOpen = true;
  click(327, 61);
  if (!gymOpen) { printf("FAIL: tapping the cleared battle-centre corner changed context\n"); return 1; }
  printf("PASS: battle-centre top-right team entry is removed\n");
  gymHard = false; gymOpen = false; player.badges = 0;

  // ---- the six cultivation slots are the complete candidate pool. The old
  // live-pet-plus-party model accidentally produced a seventh candidate.
  battleOpen = false; pickOpen = false;
  for (int i = 0; i < PARTY_SLOTS; i++) { PartyMon m; m.dex = 9 + i * 10; m.level = 40;
    m.ageMinutes = 39UL * MINUTES_PER_LEVEL;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25; party.replaceAt(i, m); }
  pickTrainer = 0; pickHard = false; pickPage = 0;
  pickDefault(squadCap(0, false));
  printf("     candidates=%u chosen=%u cap=%u\n",
         pickCandidates(), pickChosen(), squadCap(0, false));
  if (pickCandidates() != PARTY_SLOTS) { printf("FAIL: expected 6 candidates\n"); return 1; }
  printf("PASS: a full cultivation team is exactly 6 candidates\n");
  if (pickChosen() > squadCap(0, false)) {
    printf("FAIL: the picker opens over its own cap\n"); return 1; }
  printf("PASS: it opens with a valid selection, not everything\n");
  { uint8_t pages = (pickCandidates() + 6 - 1) / 6;
    if (pages != 1) { printf("FAIL: 6 candidates must fit one page\n"); return 1; }
    printf("PASS: all 6 candidates fit on one page\n"); }
  for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  squadMask = 0xFFFF;

  // ---- team select: cap enforcement and toggling
  battleOpen = false;
  for (int i = 0; i < PARTY_SLOTS; i++) { PartyMon m; m.dex = 9 + i; m.level = 40;
    m.ageMinutes = 39UL * MINUTES_PER_LEVEL;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25; party.replaceAt(i, m); }
  squadMask = 0xFFFF;
  pickTrainer = 0; pickHard = true; pickOpen = true;   // BROCK: cap of 2
  uint8_t cap = squadCap(0, true);
  // x=300: FIGHT sits right of centre now, with BACK to its left. Tapping 233
  // lands in the gap between them and hits nothing, which made the next check
  // pass for the wrong reason.
  click(300, 366);                                     // FIGHT with 6 chosen
  if (battleOpen || !pickOpen) { printf("FAIL: FIGHT fired while over the cap\n"); return 1; }
  printf("PASS: FIGHT is inert while over the cap (%u chosen, cap %u)\n", 6, cap);

  // deselect down to the cap, then it should start
  for (int slot = 5; slot >= (int)cap; slot--)
    click(PICK_X(slot) + 40, PICK_Y(slot) + 30);
  click(300, 366);
  if (!battleOpen) { printf("FAIL: FIGHT did not start at the cap\n"); return 1; }
  printf("PASS: trimming to the cap lets the fight start (squad %u)\n", btlSquadN);
  if (btlSquadN > cap) { printf("FAIL: squad exceeded the cap\n"); return 1; }
  printf("PASS: the squad respects the chosen team\n");
  battleOpen = false;
  squadMask = 0xFFFF;
  for (int i = 0; i < 5; i++) party.releaseAt(i);

  // easy caps the level too -- it just does not cap the team size
  startTrainerBattle(0, false);
  if (btlYou.level != 14) { printf("FAIL: easy mode did not cap the level\n"); return 1; }
  printf("PASS: easy mode caps the level as well\n");

  // ---- failed escape turn, persistent team death, revive, and burial
  battleOpen = false; btlMenu = 0; btlMsgCount = 0;
  for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  for (int i = 0; i < BOX_SLOTS; i++) party.boxReleaseAt(i);
  pet.dbgHatchAs(9, false);
  pet.ageMinutes = 19 * MINUTES_PER_LEVEL;       // level 20
  PartyMon active;
  pet.exportState(active);
  uint8_t oldActive = party.activeIndex();
  party.replaceAt(0, active);
  if (oldActive != 0) {
    party.activate(0, pet);
    party.releaseAt(oldActive);
  }
  PartyMon reserve;
  reserve.dex = 25; reserve.level = 40;
  reserve.ageMinutes = 39UL * MINUTES_PER_LEVEL;
  reserve.ivAtk = reserve.ivDef = reserve.ivSpe = reserve.ivHp = 20;
  uint8_t reserveSlot = 1;
  party.replaceAt(reserveSlot, reserve);
  startBattle(150, 100);
  if (btlSquadN != 2 || btlYou.level != 20) {
    printf("FAIL: death test did not start with the intended two-member team\n");
    return 1;
  }
  btlWild = true;
  btlFoe.maxHp = 100; btlFoe.hp = 41;
  if (btlAttemptFoeRun(0)) {
    printf("FAIL: a healthy wild foe escaped above the HP threshold\n"); return 1;
  }
  btlFoe.hp = 40;
  if (btlAttemptFoeRun(10) || !btlAttemptFoeRun(9) || !btlOver || btlWon ||
      pet.isDead()) {
    printf("FAIL: wild foe escape did not respect its ten-percent boundary\n");
    return 1;
  }
  printf("PASS: a low-HP wild foe can escape without killing the player's pet\n");
  directBattleTap(0, 0);
  startBattle(150, 100);
  btlWild = true;
  MoveId tackle = findMove("TACKLE");
  if (!tackle) {
    printf("FAIL: death test could not find TACKLE\n"); return 1;
  }
  for (MoveId &move : btlFoe.moves) move = MOVE_NONE;
  uint16_t hpBeforeRun = btlYou.hp;
  if (btlAttemptRun(99) || pet.isDead() || btlOver || btlYou.hp != hpBeforeRun) {
    printf("FAIL: a failed escape killed the active pet without an enemy attack\n");
    return 1;
  }
  printf("PASS: a failed escape consumes the turn without forcing death\n");
  btlYou.hp = 1;
  btlFoe.moves[0] = tackle;
  if (btlAttemptRun(99) || !pet.isDead() || btlOver || btlSwapWho != 0) {
    printf("FAIL: the enemy attack after a failed escape did not queue the dead pet's reserve\n");
    return 1;
  }
  printf("PASS: a real enemy attack after a failed escape can still kill the active pet\n");
  directBattleTap(0, 0);                     // faint text -> reserve enters
  directBattleTap(0, 0);                     // clear GO text
  if (btlSquadAt != 1 || btlYou.dex != reserve.dex) {
    printf("FAIL: the living reserve did not replace the dead pet\n"); return 1;
  }

  const ItemEntry *revive = nullptr;
  for (uint16_t i = 0; i < itemCount(); i++) {
    const ItemEntry *item = itemAt(i);
    if (item && item->effect == ITEM_EFFECT_REVIVE) { revive = item; break; }
  }
  if (!revive || !inventory.add(revive->key)) {
    printf("FAIL: no revive item available for the battle test\n"); return 1;
  }
  uint8_t reviveBefore = inventory.count(revive->key);
  for (MoveId &move : btlFoe.moves) move = MOVE_NONE;
  btlPendingItem = revive->key; btlTargetPage = 0; btlMenu = 4;
  directBattleTap(BTL_CELL_X(0) + 40, BTL_CELL_Y(0) + 20);
  if (pet.isDead() || inventory.count(revive->key) != reviveBefore - 1 ||
      btlSquad[0].fainted()) {
    printf("FAIL: an in-battle revive did not restore persistent life\n"); return 1;
  }
  printf("PASS: a battle item revives a dead team member persistently\n");
  while (btlMsgCount && !btlOver) directBattleTap(0, 0);

  btlYou.hp = 1;
  btlFoe.moves[0] = tackle;
  if (btlAttemptRun(99) || !party.slots[reserveSlot].dead() || btlOver) {
    printf("FAIL: enemy damage after the reserve's failed escape did not persist its death\n"); return 1;
  }
  directBattleTap(0, 0);                     // reserve faints -> revived pet enters
  directBattleTap(0, 0);                     // clear GO text
  btlYou.hp = 1;
  btlFoe.moves[0] = tackle;
  if (btlSquadAt != 0 || btlAttemptRun(99) || !pet.isDead() || !btlOver) {
    printf("FAIL: battle did not end only after every team member died\n"); return 1;
  }
  printf("PASS: battle ends only after the whole selected team is dead\n");
  directBattleTap(0, 0);                     // close the concluded fight
  if (battleOpen) { printf("FAIL: concluded death battle did not close\n"); return 1; }

  if (!inventory.add(revive->key)) {
    printf("FAIL: could not add an outside-battle revive\n"); return 1;
  }
  trainOpen = sackOpen = gameOpen = menuOpen = cardOpen = false;
  moveInfoOpen = movePickOpen = spdOpen = false;
  bagOpen = playerOpen = lanOpen = galleryOpen = false;
  kbOpen = clockOpen = gymOpen = boxOpen = pickOpen = false;
  reviveBefore = inventory.count(revive->key);
  click(74 + 75, 346 + 24);
  if (pet.isDead() || inventory.count(revive->key) != reviveBefore - 1) {
    printf("FAIL: the main-screen revive left dead=%d and item count %u -> %u\n",
           (int)pet.isDead(), reviveBefore, inventory.count(revive->key));
    return 1;
  }
  printf("PASS: a corpse can be revived outside battle with an item\n");

  pet.setDead(true);
  click(242 + 75, 346 + 24);                 // BURY
  click(93 + 140, 216 + 26);                 // confirm
  if (pet.speciesId != reserve.dex || !pet.isDead() || !party.slots[0].empty()) {
    printf("FAIL: burial did not promote the stored corpse and free its slot\n");
    return 1;
  }
  printf("PASS: burial frees the slot and promotes the next stored creature\n");
  click(242 + 75, 346 + 24);
  click(93 + 140, 216 + 26);
  if (!pet.isEgg()) {
    printf("FAIL: burying the final creature did not grant a new egg\n"); return 1;
  }
  printf("PASS: burying the final creature grants a new random egg\n");
  return 0;
}
