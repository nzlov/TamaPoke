// TamaPoke desktop emulator: runs the real sketch, draws the real framebuffer,
// and feeds mouse clicks in as touch events. Serial commands come from stdin.
#include <SDL.h>   // sdl2-config puts the SDL2 dir on the include path
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "input_coords.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "inventory.h"
#include "content.h"
#include "i18n.h"
#include "quiz.h"
#include <chrono>
#include <string>
#include <deque>
#include <unistd.h>
#include <sys/select.h>

// --- Arduino runtime globals ---
uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

// millis() lives in clock.cpp so the headless tests share it
void FakeESP::restart() { Serial.println("emu: ESP.restart() -> exiting"); exit(0); }

// --- stdin-backed serial ---
static std::deque<std::string> g_lines;
static std::string g_partial;
static void pumpStdin() {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(0, &fds);
  struct timeval tv = { 0, 0 };
  while (select(1, &fds, nullptr, nullptr, &tv) > 0) {
    char buf[512];
    ssize_t n = read(0, buf, sizeof(buf));
    if (n <= 0) break;
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\n') { g_lines.push_back(g_partial); g_partial.clear(); }
      else g_partial += buf[i];
    }
    FD_ZERO(&fds); FD_SET(0, &fds); tv = { 0, 0 };
  }
}
// --- simulating a crash ---
//
// The board keeps its breadcrumb in RTC memory, which survives a panic but not
// a power cycle. A process has no such thing, so the emulator parks the crumb
// in a file beside the save and re-execs itself: the window closes and reopens
// reporting the crash, which is exactly what a player sees.
//
// PANIC is handled HERE and never reaches the firmware, because a serial
// command that fakes a crash has no business existing on real hardware.
static std::string g_crashFile;
void emuSetResetReason(int r);
extern uint32_t gCrumbMagic, gCrumbScreen, gCrumbHeap;
static char **g_argv = nullptr;

static void crashArmAndReexec(int reason) {
  FILE *f = fopen(g_crashFile.c_str(), "wb");
  if (f) {
    fprintf(f, "%d %u %u %u\n", reason, gCrumbMagic, gCrumbScreen, gCrumbHeap);
    fclose(f);
  }
  printf("\nemu: simulating a %s -- restarting the way the board would\n\n",
         reason == ESP_RST_PANIC ? "PANIC" : "TASK WATCHDOG");
  fflush(stdout);
  execv("/proc/self/exe", g_argv);      // linux
  execv(g_argv[0], g_argv);             // macos and anything else
  perror("emu: could not re-exec");
  exit(1);
}

// Reads back what the "crash" left behind, so the firmware's bootReport() sees
// exactly the state a real panic would have left in RTC memory.
static void crashRestore() {
  FILE *f = fopen(g_crashFile.c_str(), "rb");
  if (!f) return;
  unsigned magic = 0, screen = 0, heap = 0; int reason = 0;
  if (fscanf(f, "%d %u %u %u", &reason, &magic, &screen, &heap) == 4) {
    emuSetResetReason(reason);
    gCrumbMagic = magic; gCrumbScreen = screen; gCrumbHeap = heap;
  }
  fclose(f);
  remove(g_crashFile.c_str());
}

int FakeSerial::available() {
  // intercept the emulator-only commands before the firmware ever sees them
  while (!g_lines.empty()) {
    const std::string &l = g_lines.front();
    if (l == "PANIC") { g_lines.pop_front(); crashArmAndReexec(ESP_RST_PANIC); }
    else if (l == "WDT") { g_lines.pop_front(); crashArmAndReexec(ESP_RST_TASK_WDT); }
    else break;
  }
  return g_lines.empty() ? 0 : 1;
}
String FakeSerial::readStringUntil(char) {
  if (g_lines.empty()) return String("");
  String s(g_lines.front());
  g_lines.pop_front();
  return s;
}

// --- NVS persistence ---
void nvsLoad(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return;
  uint32_t n = 0;
  if (fread(&n, 4, 1, f) != 1) { fclose(f); return; }
  for (uint32_t i = 0; i < n; i++) {
    uint32_t kl = 0, vl = 0;
    if (fread(&kl, 4, 1, f) != 1 || kl > 64) break;
    std::string k(kl, 0);
    if (fread(&k[0], 1, kl, f) != kl) break;
    if (fread(&vl, 4, 1, f) != 1 || vl > 4096) break;
    std::vector<uint8_t> v(vl);
    if (vl && fread(v.data(), 1, vl, f) != vl) break;
    nvs()[k] = v;
  }
  fclose(f);
}
void nvsSave(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  uint32_t n = nvs().size();
  fwrite(&n, 4, 1, f);
  for (auto &kv : nvs()) {
    uint32_t kl = kv.first.size(), vl = kv.second.size();
    fwrite(&kl, 4, 1, f); fwrite(kv.first.data(), 1, kl, f);
    fwrite(&vl, 4, 1, f); if (vl) fwrite(kv.second.data(), 1, vl, f);
  }
  fclose(f);
}

// the sketch
void startBattle(int16_t dex, uint8_t lvl);
extern Combatant btlYou, btlFoe;
extern uint32_t btlLungeUntil[2], btlHitUntil[2];
extern uint8_t btlMenu, btlMsgCount;
extern char btlMsg[6][64];
extern uint16_t btlHpShown[2];
extern BattleSideMechanics btlYourMechanics;
extern BattleMechanic btlPendingMechanic;
extern BattleField btlField;
extern int8_t btlTrainer;
extern uint32_t btlWinUntil;
extern uint16_t btlRewardTraining[3];
extern ItemRef btlRewardItems[4];
extern uint8_t btlRewardItemCount;
void startTrainerBattle(uint8_t idx, bool hard);
void onTap(int16_t x, int16_t y);   // the first-boot shots tap their way in
void onSwipe(int dir);
void onSwipeV(int dir);
void bagTap(int16_t x, int16_t y);
void refreshUiFont();
extern bool gymOpen, playerOpen, navMenuOpen;
extern uint8_t galleryRegion;
extern uint8_t gymRegion;
extern bool gymPick, galleryPick;
int wavMain(const char *path, const char *demo);
extern bool lanOpen;
extern uint8_t btlMyAct;
extern GymIvReward btlIvReward;
extern uint8_t btlIvWhich;
extern bool lanWantHost;
extern bool gShowAllAvatars;
#define PICK_LAN 0xFF
uint8_t squadCap(uint8_t, bool);
#include "link.h"
extern Link lan;
void startLinkBattle();
extern uint8_t playerPage;
extern bool gymHard, pickOpen;
extern bool boxOpen;
extern bool btlNewBadge; extern uint32_t btlWinUntil;
extern uint8_t pickTrainer, pickPage;
void pickDefault(uint8_t cap);
extern bool pickHard;
void startSpeedGame();
void startGame();
void startSack();
void startBathAnimation(uint32_t now);
void openKeyboard();
void openKeyboardFor(uint8_t target);
void setup();
void renderBootSplash();
void loop();
void render();
void ensureMon();
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool cardOpen, natureInfoOpen, galleryOpen, clockOpen, kbOpen, menuOpen,
            partyPick, trainOpen, movePickOpen;
extern bool bagOpen;
extern bool battleOpen, btlWild;
extern bool btlTurnAnimating, btlTurnShowingRound;
extern uint32_t btlTurnBeatStartedAt;
void commitBattleMove(uint8_t moveSlot, uint8_t percent);
void btlUpdateTurnPresentation(uint32_t now);
extern bool btlFoeDetailOpen;
extern uint8_t btlFoeDetailPage;
extern bool btlCaptureAnimating, btlCaptureSuccess, btlCaptureCuePlayed;
extern uint32_t btlCaptureStartedAt;
extern ItemKey btlCaptureItem;
extern bool btlThrowArmed;
extern uint32_t btlThrowStartedAt;
extern ItemKey btlThrowItem;
extern PartyMon capturedMon, btlWildMon;
extern uint8_t cardPage;
extern int16_t galleryDetail;
extern bool moveInfoOpen;
extern uint8_t movePickSlot;
extern QuizRuntime quiz;
extern uint32_t feedMenuUntil, choiceUntil, bathUntil;
extern uint8_t choiceKind;
extern uint32_t partyBannerUntil;
extern char partyBannerName[32];
extern bool gameOpen, gameNewHi, sackOpen, sackNewHi, spdOpen, spdNewHi;
extern uint32_t gameOverUntil, sackOverUntil, spdOverUntil;
extern uint8_t gameScore, gameMisses, sackGain, spdGain;
extern uint16_t sackHits, spdHits;
extern ItemKey bagSelectedKey, bagDetailKey, btlPendingItem;
extern MoveId bagSelectedMove, bagDetailMove;
extern uint8_t bagView, bagDiscardAmount, boxSel, btlItemPage, btlSquadN;
extern Combatant btlSquad[];
#define PANEL 466

// Headless capture, so the layout can be checked without a display. Writes a
// PPM (no image library needed); convert with `sips -s format png`.
static void writePPM(const char *path) {
  const uint16_t *fb = gfx->buffer();
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
  fprintf(f, "P6\n%d %d\n255\n", PANEL, PANEL);
  for (int i = 0; i < PANEL * PANEL; i++) {
    uint16_t c = fb[i];
    int x = i % PANEL, y = i / PANEL, dx = x - 233, dy = y - 233;
    uint8_t r = ((c >> 11) & 31) * 255 / 31;
    uint8_t g = ((c >> 5) & 63) * 255 / 63;
    uint8_t b = (c & 31) * 255 / 31;
    if (dx * dx + dy * dy > 233 * 233) { r /= 4; g /= 4; b /= 4; }  // off-panel
    fputc(r, f); fputc(g, f); fputc(b, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

static void expireShotCelebrations() {
  emuSetTimeScale(1000);
  usleep(5000);
  millis();
  emuSetTimeScale(1);
}

static int shotMode(const char *screen, const char *out, int lvl, int iv, int dex,
                    const char *locale) {
  setup();
  if (!strcmp(screen, "bootsplash")) {
    renderBootSplash();
    writePPM(out);
    return 0;
  }
  if (locale) {
    int8_t selected = uiFindLocale(locale);
    if (selected < 0) { fprintf(stderr, "unknown installed locale: %s\n", locale); return 1; }
    setLang((Lang)selected);
    refreshUiFont();
  }
  for (int i = 0; i < 4; i++) loop();          // let the sketch settle
  bool firstBoot = !strcmp(screen, "language") || !strcmp(screen, "starter") ||
                   !strcmp(screen, "starterj") || !strcmp(screen, "region");
  if (pet.awaitingStarter() && !firstBoot) pet.chooseStarter(4);
  if (!strcmp(screen, "egg")) {
    // a few species registered, so the lottery is past the starter case and the
    // region pill has something to switch between
    for (int d = 1; d <= 40; d++) pet.dbgHatchAs(d, false);
    pet.newEgg();
  }
  // dbgHatchAs() clears starterPick, so hatching here would skip the very
  // screen the first-boot shots are trying to photograph.
  if (pet.isEgg() && !firstBoot && strcmp(screen, "egg")) pet.dbgHatchAs(dex, false);
  if (lvl > 0) pet.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  if (iv >= 0) {
    pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = iv;
    pet.trAtk = pet.trMaxAtk(); pet.trDef = pet.trMaxDef(); pet.trSpe = pet.trMaxSpe();
  }
  // The learn prompt is modal and would cover whatever screen was asked for.
  // It started firing for every shot once dex_moves.py gained the cheap early
  // attacks, because a creature now genuinely has moves waiting.
  // First-boot shots advance through the same language and region taps as the
  // player. The language page itself is left untouched for its screenshot.
  if (strcmp(screen, "language") &&
      (!strcmp(screen, "region") || !strcmp(screen, "starter") ||
       !strcmp(screen, "starterj"))) {
    onTap(74 + (gLang % 2) * 168 + 75, 104 + (gLang / 2) * 70 + 27);
  }
  if (!strcmp(screen, "starter")) onTap(233, 108 + 30);        // KANTO
  if (!strcmp(screen, "starterj")) onTap(233, 108 + 72 + 30);  // JOHTO
  for (int i = 0; i < 2; i++) loop();          // pick up the sprite for the new species
  cardOpen = natureInfoOpen = galleryOpen = clockOpen = kbOpen = false;
  menuOpen = navMenuOpen = partyPick = trainOpen = movePickOpen = false;
  boxOpen = false;
  bagOpen = false;
  capturedMon = PartyMon();
  if (!strcmp(screen, "main")) {
    pet.ageMinutes = 0;
    expireShotCelebrations();
  }
  else if (!strcmp(screen, "sparkle")) {
    pet.shiny = true;
    pet.ageMinutes = 0;
    ensureMon();
    expireShotCelebrations();
  }
  else if (!strcmp(screen, "color")) {
    pet.shiny = true;
    pet.ageMinutes = 0;
    ensureMon();
    expireShotCelebrations();
  }
  else if (!strcmp(screen, "bothrare")) {
    pet.shiny = true;
    pet.ageMinutes = 0;
    ensureMon();
    expireShotCelebrations();
  }
  else if (!strcmp(screen, "mainroster")) {
    pet.ageMinutes = 0;
    party.captureActive(pet, false);
    PartyMon second; second.dex = 25; second.level = 18;
    second.ageMinutes = 17UL * MINUTES_PER_LEVEL;
    PartyMon third; third.dex = 143; third.level = 32;
    third.ageMinutes = 31UL * MINUTES_PER_LEVEL;
    party.replaceAt(1, second);
    party.replaceAt(2, third);
    expireShotCelebrations();
  }
  else if (!strcmp(screen, "evolvecta")) {
    pet.dbgHatchAs(1, false);
    pet.ageMinutes = 19UL * MINUTES_PER_LEVEL;
  }
  else if (!strcmp(screen, "farewellcta")) {
    pet.dbgHatchAs(3, false);
    expireShotCelebrations();
    pet.ageMinutes = 73UL * MINUTES_PER_LEVEL;
    pet.raisedMinutes = FAREWELL_AGE_MIN;
    menuOpen = true;
  }
  else if (!strcmp(screen, "runawaycta")) {
    pet.dbgHatchAs(3, false);
    expireShotCelebrations();
    pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
    pet.dbgRunawayReady();
  }
  else if (!strcmp(screen, "battle"))      { cardOpen = true; cardPage = 1; }
  else if (!strcmp(screen, "profile")){ cardOpen = true; cardPage = 0; }
  else if (!strcmp(screen, "natureinfo") || !strcmp(screen, "naturetraining")) {
    pet.nature = !strcmp(screen, "naturetraining") ? NATURE_BASHFUL
                                                    : NATURE_ADAMANT;
    cardOpen = true;
    cardPage = 0;
    natureInfoOpen = true;
  }
  else if (!strcmp(screen, "medals")) { cardOpen = true; cardPage = 3; }
  else if (!strcmp(screen, "progress")){cardOpen = true; cardPage = 4; }
  else if (!strcmp(screen, "gallery") || !strcmp(screen, "gallerypage2")) {
    galleryOpen = true;
    for (int d = 1; d <= 200; d++) pet.dbgHatchAs(d, false);
  }
  else if (!strcmp(screen, "gallery2")) {   // the second region
    galleryOpen = true;
    galleryRegion = 1;
    for (int d = 1; d <= 200; d++) pet.dbgHatchAs(d, false);
  }
  else if (!strcmp(screen, "dexdetail")) {
    galleryOpen = true;
    galleryDetail = dex;
  }
  else if (!strcmp(screen, "clock"))   clockOpen = true;
  else if (!strcmp(screen, "menu"))    menuOpen = true;
  else if (!strcmp(screen, "navmenu")) navMenuOpen = true;
  else if (!strcmp(screen, "train"))   trainOpen = true;
  // GLUE: screenshot fixtures set the same UI state that touch handlers would;
  // remove these assignments if the emulator gains scripted touch journeys.
  else if (!strcmp(screen, "sleep"))   pet.sleeping = true;
  else if (!strcmp(screen, "feedmenu")) feedMenuUntil = millis() + 5000;
  else if (!strcmp(screen, "releaseconfirm") || !strcmp(screen, "choicerelease") ||
           !strcmp(screen, "choiceretire")) {
    choiceKind = 3; choiceUntil = millis() + 5000;
  }
  else if (!strcmp(screen, "choiceevolve")) { choiceKind = 1; choiceUntil = millis() + 5000; }
  else if (!strcmp(screen, "choicepoweroff")) { choiceKind = 4; choiceUntil = millis() + 5000; }
  else if (!strcmp(screen, "choicefarewell")) {
    pet.dbgHatchAs(3, false);
    pet.raisedMinutes = FAREWELL_AGE_MIN;
    choiceKind = 3; choiceUntil = millis() + 5000;
  }
  else if (!strcmp(screen, "bath")) startBathAnimation(millis());
  else if (!strcmp(screen, "joined")) {
    snprintf(partyBannerName, sizeof(partyBannerName), "%s", speciesName(pet.speciesId));
    partyBannerUntil = millis() + 5000;
  }
  else if (!strcmp(screen, "ceremonyfarewell")) {
    pet.dbgHatchAs(3, false);
    pet.raisedMinutes = FAREWELL_AGE_MIN;
    pet.startFarewell();
  }
  else if (!strcmp(screen, "ceremonyrunaway")) pet.startRunaway();
  else if (!strcmp(screen, "ceremonyrelease")) pet.release();
  else if (!strcmp(screen, "keyboard")) openKeyboard();
  else if (!strcmp(screen, "trainerkeyboard")) openKeyboardFor(1);
  else if (!strcmp(screen, "ballgame") || !strcmp(screen, "ballresult")) {
    startGame();
    gameScore = 12; gameMisses = 1;
    if (!strcmp(screen, "ballresult")) {
      gameNewHi = true;
      gameOverUntil = millis() + 5000;
    }
  }
  else if (!strcmp(screen, "sack") || !strcmp(screen, "sackresult")) {
    startSack();
    sackHits = 28;
    if (!strcmp(screen, "sackresult")) {
      sackGain = 5; sackNewHi = true;
      sackOverUntil = millis() + 5000;
    }
  }
  else if (!strcmp(screen, "speedresult")) {
    startSpeedGame();
    spdHits = 19; spdGain = 4; spdNewHi = true;
    spdOverUntil = millis() + 5000;
  }
  else if (!strcmp(screen, "quiz")) {
    quiz.config.choiceWeight = 0;
    quiz.begin(locale ? locale : "en-US");
  }
  else if (!strcmp(screen, "quizcorrect") || !strcmp(screen, "quizwrong")) {
    quiz.config.questionTypes = QUIZ_TYPE_ARITHMETIC;
    quiz.config.choiceWeight = 0;
    if (quiz.begin(locale ? locale : "en-US")) {
      quiz.markRendered(millis());
      snprintf(quiz.input, sizeof(quiz.input), "%s",
               !strcmp(screen, "quizcorrect") ? quiz.expected
                 : (strcmp(quiz.expected, "0") ? "0" : "1"));
      quiz.submit(millis());
    }
  }
  else if (!strcmp(screen, "moves"))   { cardOpen = true; cardPage = 2; }
  else if (!strcmp(screen, "moveinfo")) { movePickSlot = 0; moveInfoOpen = true; }
  else if (!strcmp(screen, "movepick")) { movePickOpen = true; }
  else if (!strcmp(screen, "bag") || !strcmp(screen, "bagscroll") ||
           !strcmp(screen, "bagactions") ||
           !strcmp(screen, "bagdetail") || !strcmp(screen, "bagtarget") ||
           !strcmp(screen, "bagquantity") || !strcmp(screen, "bagconfirm")) {
    const ItemEntry *selected = nullptr;
    for (uint16_t i = 0; i < itemCount(); i++) {
      const ItemEntry *item = itemAt(i);
      if (!item) continue;
      inventory.add(item->key, (uint8_t)(i + 3));
      if (!selected && item->effect != ITEM_EFFECT_TEACH_MOVE &&
          itemUsableOutsideBattle(*item)) selected = item;
    }
    if (strcmp(screen, "bag") && strcmp(screen, "bagscroll")) {
      for (uint16_t i = 0; i < itemCount(); i++) {
        const ItemEntry *item = itemAt(i);
        while (item && inventory.consume(item->key)) {}
      }
      if (selected) inventory.add(selected->key, 3);
    }
    bagSelectedKey = selected ? selected->key : ITEM_KEY_NONE;
    bagSelectedMove = MOVE_NONE;
    bagDetailKey = !strcmp(screen, "bagdetail") ? bagSelectedKey : ITEM_KEY_NONE;
    bagDetailMove = MOVE_NONE;
    bagDiscardAmount = !strcmp(screen, "bagconfirm") ? 2 : 1;
    if (!strcmp(screen, "bagactions")) bagView = 1;
    else if (!strcmp(screen, "bagdetail")) bagView = 2;
    else if (!strcmp(screen, "bagtarget")) {
      pet.dbgHatchAs(1, false);
      PartyMon second = pet.toPartyMon();
      second.dex = 6;
      second.nick[0] = 0;
      second.trMinAtk = second.trMinDef = second.trMinSpe = 0;
      party.replaceAt(1, second);
      party.captureActive(pet, false);
      bagView = 3;
    } else if (!strcmp(screen, "bagquantity")) bagView = 4;
    else if (!strcmp(screen, "bagconfirm")) bagView = 5;
    else {
      bagView = 0;
      bagSelectedKey = ITEM_KEY_NONE;
    }
    bagOpen = true;
  }
  else if (!strcmp(screen, "stonedetail") ||
           !strcmp(screen, "stonetarget") ||
           !strcmp(screen, "stoneconfirm") ||
           !strcmp(screen, "stoneincompatible")) {
    const ItemEntry *stone = nullptr;
    for (uint16_t i = 0; i < itemCount(); i++) {
      const ItemEntry *item = itemAt(i);
      if (item && item->effect == ITEM_EFFECT_TEACH_MOVE) { stone = item; break; }
    }
    MoveId move = MOVE_NONE;
    for (MoveId candidate = 1; candidate < moveCount(); candidate++) {
      bool compatible = speciesCanLearnMove(pet.speciesId, candidate);
      if (((!strcmp(screen, "stonedetail") || !strcmp(screen, "stonetarget") ||
            !strcmp(screen, "stoneconfirm")) &&
           compatible && !pet.knowsMove(candidate)) ||
          (!strcmp(screen, "stoneincompatible") && !compatible)) {
        move = candidate;
        break;
      }
    }
    if (!stone || !moveValid(move)) {
      fprintf(stderr, "move-stone screenshot fixture has no suitable move\n");
      return 1;
    }
    inventory.add(stone->key, 1, move);
    bagSelectedKey = stone->key;
    bagSelectedMove = move;
    bagDetailKey = ITEM_KEY_NONE;
    bagDetailMove = MOVE_NONE;
    bagView = 1;
    bagOpen = true;
    bagTap(233, !strcmp(screen, "stonedetail") ? 172 : 230);
    if (!strcmp(screen, "stonetarget")) {
      PartyMon second = pet.toPartyMon();
      second.dex = 25;
      second.nick[0] = 0;
      for (MoveId &known : second.moves) known = MOVE_NONE;
      for (MoveId &known : second.reserveMoves) known = MOVE_NONE;
      party.replaceAt(party.activeIndex() == 0 ? 1 : 0, second);
    }
    if (strcmp(screen, "stonedetail") && strcmp(screen, "stonetarget")) {
      uint8_t slot = party.activeIndex();
      bagTap(78 + (slot % 2) * 160 + 75, 82 + (slot / 2) * 84 + 35);
    }
  }
  else if (!strcmp(screen, "capture") || !strcmp(screen, "reward") ||
           !strcmp(screen, "rewardscroll")) {
    if (!strcmp(screen, "capture")) {
      Pet caught;
      caught.dbgHatchAs(25, true);
      caught.ageMinutes = 41UL * MINUTES_PER_LEVEL;
      caught.ivAtk = 41;
      caught.ivDef = 34;
      caught.ivSpe = 39;
      caught.ivHp = 37;
      caught.relearnFromLevel();
      capturedMon = caught.toPartyMon();
    }
    btlTrainer = -1;
    btlRewardTraining[0] = 4;
    btlRewardTraining[1] = 3;
    btlRewardTraining[2] = 3;
    btlRewardItemCount = 0;
    for (uint16_t i = 0; i < itemCount() && btlRewardItemCount < 3; i++) {
      const ItemEntry *item = itemAt(i);
      if (!item || !item->dropWeight) continue;
      btlRewardItems[btlRewardItemCount++] = { item->key, MOVE_NONE };
    }
    for (uint16_t i = 0; i < itemCount() && btlRewardItemCount < 4; i++) {
      const ItemEntry *item = itemAt(i);
      if (!item || item->effect != ITEM_EFFECT_BATTLE_MECHANIC) continue;
      btlRewardItems[btlRewardItemCount++] = { item->key, MOVE_NONE };
      break;
    }
    btlWinUntil = 60000;
  }
  else if (!strcmp(screen, "battle2")) { startBattle(9, 50); }
  else if (!strcmp(screen, "btlmenu")) { startTrainerBattle(3, false); }
  else if (!strcmp(screen, "btlswitch")) {
    for (int i = 0; i < 2; i++) {
      PartyMon m; m.dex = i ? 25 : 9; m.level = 48 + i * 3;
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25;
      party.replaceAt(i, m);
    }
    startTrainerBattle(3, false); btlMenu = 2;
  }
  else if (!strcmp(screen, "btlmoves")) { startTrainerBattle(3, false); btlMenu = 1; }
  else if (!strcmp(screen, "btlitems2")) {
    startTrainerBattle(3, false);
    for (uint16_t i = 0; i < itemCount(); i++) {
      const ItemEntry *item = itemAt(i);
      if (item) inventory.add(item->key, 3);
    }
    btlMenu = 3; btlItemPage = 1;
  }
  else if (!strcmp(screen, "btlmechanics")) {
    pet.dbgHatchAs(3, false);
    pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(25, 42);
    for (MoveId move = 1; move < moveCount(); move++)
      if (moveEntry(move).cat != MC_STATUS) {
        btlYou.moves[0] = move;
        break;
      }
    for (uint16_t i = 0; i < itemCount(); i++) {
      const ItemEntry *item = itemAt(i);
      if (!item) continue;
      while (item->effect != ITEM_EFFECT_BATTLE_MECHANIC && inventory.count(item->key))
        inventory.consume(item->key);
      if (item->effect == ITEM_EFFECT_BATTLE_MECHANIC)
        inventory.add(item->key, 3);
    }
    btlMenu = 3;
    btlItemPage = 0;
  }
  else if (!strcmp(screen, "btlzmove")) {
    startTrainerBattle(3, false);
    btlPendingMechanic = BMECH_Z_MOVE;
    btlMenu = 1;
  }
  else if (!strcmp(screen, "btlnormal")) {
    pet.dbgHatchAs(6, false);
    pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(25, 42);
  }
  else if (!strcmp(screen, "btlfield")) {
    pet.dbgHatchAs(6, false);
    pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(25, 42);
    battleSetEnvironment(btlField, BWEATHER_RAIN, BTERRAIN_ELECTRIC);
    btlMsgCount = 1;
    snprintf(btlMsg[0], sizeof(btlMsg[0]), T(S_BTL_FIELD_BEGAN), T(S_FIELD_RAIN));
  }
  else if (!strcmp(screen, "btlconditions")) {
    pet.dbgHatchAs(6, false);
    pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(778, 50);
    btlFoe.ability = ABILITY_DISGUISE;
    battleInitializeForm(btlFoe);
    battleSetEnvironment(btlField, BWEATHER_SNOW);
    btlField.sides[0].reflectTurns = 4;
    btlField.sides[0].spikesLayers = 3;
    btlField.sides[0].toxicSpikesLayers = 2;
    btlField.sides[0].stealthRock = true;
    btlField.sides[0].stickyWeb = true;
    btlField.sides[1].lightScreenTurns = 3;
    btlField.sides[1].auroraVeilTurns = 2;
    btlField.sides[1].spikesLayers = 1;
    btlMsgCount = 0;
  }
  else if (!strcmp(screen, "btldynamax")) {
    pet.dbgHatchAs(6, false);
    pet.gigantamaxFactor = true;
    pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(25, 42);
    battleActivateMechanic(btlYourMechanics, btlYou, BMECH_DYNAMAX,
                           btlYou.moves[0]);
    btlHpShown[0] = btlYou.hp;
  }
  else if (!strcmp(screen, "btlmega")) {
    pet.dbgHatchAs(6, false);
    pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(25, 42);
    battleActivateMechanic(btlYourMechanics, btlYou, BMECH_MEGA,
                           btlYou.moves[0], MEGA_FORM_X);
    btlHpShown[0] = btlYou.hp;
  }
  else if (!strcmp(screen, "btlrevive")) {
    static const int fill[] = { 9, 25, 143 };
    for (int i = 0; i < 3; i++) {
      PartyMon m; m.dex = fill[i]; m.level = 45 + i * 3;
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25;
      party.replaceAt(i, m);
    }
    startTrainerBattle(3, false);
    for (uint16_t i = 0; i < itemCount(); i++) {
      const ItemEntry *item = itemAt(i);
      if (!item || item->effect != ITEM_EFFECT_REVIVE) continue;
      inventory.add(item->key, 2);
      btlPendingItem = item->key;
      break;
    }
    if (btlSquadN > 1) btlSquad[1].hp = 0;
    btlMenu = 4;
  }
  else if (!strcmp(screen, "battleanim")) {
    startBattle(9, 50);
    btlFoe.hp = btlFoe.maxHp / 3;      // bar mid-drain
    btlLungeUntil[0] = millis() + 130; // you mid-lunge
    btlHitUntil[1] = millis() + 300;   // foe flinching
  }
  else if (!strcmp(screen, "btlturn") || !strcmp(screen, "btlaction") ||
           !strcmp(screen, "btlspell")) {
    pet.dbgHatchAs(1, false);
    pet.ageMinutes = 49UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(1, 42);
    quiz.config.questionTypes = 0;
    if (strcmp(screen, "btlturn")) {
      for (MoveId move = 1; move < moveCount(); move++) {
        const MoveEntry &entry = moveEntry(move);
        bool wanted = !strcmp(screen, "btlaction")
            ? entry.cat == MC_PHYS && entry.power && (entry.tags & MT_CONTACT)
            : entry.cat == MC_SPEC && entry.power && entry.type == T_FIRE;
        if (wanted) { btlYou.moves[0] = move; break; }
      }
    }
    commitBattleMove(0, 100);
    if (!strcmp(screen, "btlaction") || !strcmp(screen, "btlspell")) {
      usleep(720000);
      btlUpdateTurnPresentation(millis());
      usleep(280000);
    }
  }
  else if (!strcmp(screen, "gyms") || !strcmp(screen, "battlecenter") ||
           !strcmp(screen, "gympage2")) { gymOpen = true; }
  else if (!strcmp(screen, "wildfight") || !strcmp(screen, "wilditems")) {
    // Use two installed fixture sprites so the capture is visually complete;
    // wild mechanics themselves are exercised by wild_test.
    pet.dbgHatchAs(1, false);
    pet.ageMinutes = 41UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(25, 42);
    btlWild = true;
    if (!strcmp(screen, "wilditems")) {
      for (uint16_t i = 0; i < itemCount(); i++) {
        const ItemEntry *item = itemAt(i);
        if (item) inventory.add(item->key, (uint8_t)(i + 2));
      }
      btlMenu = 3;
    }
  }
  else if (!strcmp(screen, "wilddetail")) {
    pet.dbgHatchAs(1, false);
    pet.ageMinutes = 41UL * MINUTES_PER_LEVEL;
    pet.gender = GENDER_MALE;
    pet.relearnFromLevel();
    startBattle(1, 42);
    btlWild = true;
    btlFoe.gender = GENDER_FEMALE;
    btlWildMon = PartyMon();
    btlWildMon.dex = 1;
    btlWildMon.level = 42;
    btlWildMon.gender = GENDER_FEMALE;
    btlWildMon.nature = NATURE_HARDY;
    btlFoeDetailOpen = true;
    btlFoeDetailPage = 0;
  }
  else if (!strcmp(screen, "throwready")) {
    pet.dbgHatchAs(1, false);
    pet.ageMinutes = 41UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(1, 42);
    btlWild = true;
    for (uint16_t i = 0; i < itemCount(); i++) {
      const ItemEntry *item = itemAt(i);
      if (item && item->effect == ITEM_EFFECT_CATCH &&
          item->param == ITEM_CATCH_GUARANTEED) {
        btlThrowItem = item->key;
        break;
      }
    }
    btlThrowArmed = btlThrowItem != ITEM_KEY_NONE;
    btlThrowStartedAt = millis() - 600UL;
  }
  else if (!strcmp(screen, "catchcenter") || !strcmp(screen, "catchthrow") ||
           !strcmp(screen, "catchabsorb") ||
           !strcmp(screen, "catchshake") || !strcmp(screen, "catchsuccess") ||
           !strcmp(screen, "catchresult") || !strcmp(screen, "catchfail") ||
           !strcmp(screen, "catchreturn") || !strcmp(screen, "catchresume") ||
           !strcmp(screen, "catchangry")) {
    pet.dbgHatchAs(1, false);
    pet.ageMinutes = 41UL * MINUTES_PER_LEVEL;
    pet.relearnFromLevel();
    startBattle(25, 42);
    if (!battleOpen) {
      fprintf(stderr, "capture screenshots require installed Kanto data packs\n");
      return 2;
    }
    btlWildMon = PartyMon();
    btlWildMon.dex = 25;
    btlWildMon.level = 42;
    btlWildMon.ageMinutes = 41UL * MINUTES_PER_LEVEL;
    btlWildMon.ivAtk = 14; btlWildMon.ivDef = 18;
    btlWildMon.ivSpe = 20; btlWildMon.ivHp = 16;
    btlWildMon.nature = NATURE_HARDY;
    for (uint8_t i = 0; i < MOVE_SLOTS; i++) btlWildMon.moves[i] = btlFoe.moves[i];
    btlWild = true;
    for (uint16_t i = 0; i < itemCount(); i++) {
      const ItemEntry *item = itemAt(i);
      if (item && item->effect == ITEM_EFFECT_CATCH &&
          item->param == ITEM_CATCH_GUARANTEED) {
        btlCaptureItem = item->key;
        break;
      }
    }
    bool angryScene = !strcmp(screen, "catchangry");
    btlCaptureAnimating = !angryScene;
    btlCaptureSuccess = strcmp(screen, "catchfail") && strcmp(screen, "catchreturn") &&
                        strcmp(screen, "catchresume") && !angryScene;
    btlCaptureCuePlayed = true;
    if (angryScene) {
      btlFoe.angry = true;
      btlMenu = 0;
      btlMsgCount = 0;
    }
    uint32_t elapsed = 300UL;
    if (!strcmp(screen, "catchthrow")) elapsed = 600UL + 325UL;
    else if (!strcmp(screen, "catchabsorb")) elapsed = 600UL + 650UL + 100UL;
    else if (!strcmp(screen, "catchshake")) elapsed = 600UL + 650UL + 350UL + 500UL;
    else if (!strcmp(screen, "catchsuccess") || !strcmp(screen, "catchfail"))
      elapsed = 600UL + 650UL + 350UL + 1350UL + 180UL;
    else if (!strcmp(screen, "catchreturn"))
      elapsed = 600UL + 650UL + 350UL + 1350UL + 700UL + 300UL;
    else if (!strcmp(screen, "catchresult"))
      elapsed = 600UL + 650UL + 350UL + 1350UL + 700UL + 50UL;
    else if (!strcmp(screen, "catchresume"))
      elapsed = 600UL + 650UL + 350UL + 1350UL + 700UL + 600UL + 50UL;
    if (!angryScene) btlCaptureStartedAt = millis() - elapsed;
  }
  else if (!strcmp(screen, "gympick") || !strcmp(screen, "gympickpage2")) {
    gymOpen = true; gymPick = true;
  }
  else if (!strcmp(screen, "dexpick") || !strcmp(screen, "dexpickpage2")) {
    for (int d = 1; d <= 200; d++) pet.dbgHatchAs(d, false);
    galleryOpen = true; galleryPick = true;
  }
  else if (!strcmp(screen, "gymsj")) { gymOpen = true; gymRegion = 1; }
  else if (!strcmp(screen, "gymsh")) { gymOpen = true; gymRegion = 2; }
  else if (!strcmp(screen, "lan")) { lanOpen = true; lan.state = LINK_OFF; }
  else if (!strcmp(screen, "lanpick")) {
    static const int f[]={9,25,143,94,131,3};
    for(int i=0;i<6;i++){ PartyMon m; m.dex=f[i]; m.level=40+i*5;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=25; party.replaceAt(i,m); }
    lanWantHost = true;
    pickTrainer = PICK_LAN; pickHard = false; pickPage = 0;
    pickDefault(squadCap(PICK_LAN, false));
    pickOpen = true;
  }
  else if (!strcmp(screen, "lanlost")) { lanOpen = true; lan.begin(true,"D"); lan.state = LINK_LOST; }
  else if (!strcmp(screen, "lanready") || !strcmp(screen, "lanbattle") ||
           !strcmp(screen, "landone") || !strcmp(screen, "lanwait")) {
    // No radio here, so the pairing is faked at the point the radio would have
    // finished it: both squads present, state READY. Everything past this --
    // layout, the battle screen, the guest's restrictions -- is the real code.
    player.renameTrainer("DYLAN");
    lan.begin(true, "DYLAN");
    snprintf(lan.peerName, sizeof(lan.peerName), "%s", "MISTY");
    static const int mineDex[] = {9, 25, 143}, theirDex[] = {6, 65, 131};
    for (int i = 0; i < 3; i++) {
      Pet t; t.dbgHatchAs(mineDex[i], false);
      t.ivAtk = t.ivDef = t.ivSpe = t.ivHp = 25;
      t.ageMinutes = 49UL * MINUTES_PER_LEVEL; t.relearnFromLevel();
      PartyMon m; m.dex = mineDex[i]; m.level = 50;
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25;
      for (int k = 0; k < MOVE_SLOTS; k++) m.moves[k] = t.moves[k];
      party.replaceAt(i, m);
      Combatant mc; combatantFromParty(mc, m);
      LinkMon mine; linkMonFrom(mine, mc); lan.addMon(mine);

      Pet u; u.dbgHatchAs(theirDex[i], false);
      u.ivAtk = u.ivDef = u.ivSpe = u.ivHp = 25;
      u.ageMinutes = 49UL * MINUTES_PER_LEVEL; u.relearnFromLevel();
      Combatant c; combatantFromPet(c, u);
      LinkMon lm; linkMonFrom(lm, c);
      lan.theirs[i] = lm;
    }
    lan.theirsN = 3;
    lan.state = LINK_READY;
    if (!strcmp(screen, "lanbattle")) { startLinkBattle(); }
    else if (!strcmp(screen, "landone")) { lan.state = LINK_DONE; lan.youWon = true; lanOpen = true; }
    else if (!strcmp(screen, "lanwait")) {
      startLinkBattle();
      btlMyAct = LINK_ACT_MOVE(0);      // tapped, rival has not chosen yet
    }
    else lanOpen = true;
  }
  else if (!strcmp(screen, "box")) {
    static const int f[]={9,25,143,94,131,3};
    for(int i=0;i<6;i++){ PartyMon m; m.dex=f[i]; m.level=40+i*5;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=25; party.replaceAt(i,m); }
    static const int b[]={6,65,68,143,12,131,94,25};
    for(int i=0;i<4;i++){ PartyMon m; m.dex=b[i]; m.level=30+i*4;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; m.shiny=(i==3);
      party.box[i]=m; }
    party.boxSave();
    boxOpen=true; boxSel=0;
  }
  else if (!strcmp(screen, "boxreplace")) {
    static const int fill[] = { 9, 25, 143, 94, 131, 3 };
    for (int i = 0; i < 6; i++) {
      PartyMon m; m.dex = fill[i]; m.level = 40 + i * 5;
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25;
      party.replaceAt(i, m);
    }
    PartyMon stored; stored.dex = 6; stored.level = 58;
    stored.ivAtk = stored.ivDef = stored.ivSpe = stored.ivHp = 27;
    party.box[0] = stored;
    boxOpen = true; boxSel = 1;
  }
  else if (!strcmp(screen, "boxwithdraw")) {
    static const int fill[] = { 9, 25, 143 };
    for (int i = 0; i < 3; i++) {
      PartyMon m; m.dex = fill[i]; m.level = 30 + i * 5;
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 24;
      party.replaceAt(i, m);
    }
    PartyMon stored; stored.dex = 6; stored.level = 42;
    stored.ivAtk = stored.ivDef = stored.ivSpe = stored.ivHp = 26;
    party.box[0] = stored;
    boxOpen = true; boxSel = 1;
  }
  else if (!strcmp(screen, "boxdeposit")) {
    static const int fill[] = { 9, 25, 143 };
    for (int i = 0; i < 3; i++) {
      PartyMon m; m.dex = fill[i]; m.level = 24 + i * 6;
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 22;
      party.replaceAt(i, m);
    }
    PartyMon stored; stored.dex = 6; stored.level = 36;
    party.box[0] = stored;
    boxOpen = true; boxSel = 2;
  }
  else if (!strcmp(screen, "win")) {
    startTrainerBattle(2, true);
    btlNewBadge = true; btlWinUntil = 60000; player.badgesHard = 0x07;
    btlIvReward = GYM_IV_GAINED; btlIvWhich = 2;
  }
  else if (!strcmp(screen, "pick")) {
    static const int fill[]={9,25,143,94,131,3};
    static const int fill6[]={9,25,143,94,131,3};
    for(int i=0;i<6;i++){ PartyMon m; m.dex=fill6[i]; m.level=40+i*6;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=25; party.replaceAt(i,m); }
    pickTrainer = 7; pickHard = false; pickPage = 0;
    pickDefault(6); pickOpen = true;
  }
  else if (!strcmp(screen, "gymshard")) { gymOpen = true; gymHard = true; player.badgesHard = 0x03; }
  else if (!strcmp(screen, "speed")) { startSpeedGame(); }
  else if (!strcmp(screen, "avatars")) {
    player.renameTrainer("DYLAN");
    playerOpen = true;
    gShowAllAvatars = true;
  }
  else if (!strcmp(screen, "player")) { player.renameTrainer("DYLAN"); player.badges = 0xBF; player.badgesHard = 0x0A; player.streak = 5; playerOpen = true; }
  else if (!strcmp(screen, "player2")) {
    player.renameTrainer("DYLAN");
    player.badgesX[0] = 0x3F; player.badgesHardX[0] = 0x05;   // Johto
    playerOpen = true; playerPage = 1;
  }
  else if (!strcmp(screen, "medals2") || !strcmp(screen, "playermedals")) {
    pet.medals = 0x5B; player.totalMedals = 12;
    playerOpen = true; playerPage = regionAll();
  }
  else if (!strcmp(screen, "gymfight")) { startTrainerBattle(0, false); }
  else if (!strcmp(screen, "learn")) {
    // fill the four slots, then cross a gate so the offer has to be answered
    pet.relearnFromLevel();
    pet.lastLearnLevel = 0;
    pet.checkLearnGates();
  }
  render();
  if (!strcmp(screen, "gallerypage2") || !strcmp(screen, "gympage2") ||
      !strcmp(screen, "gympickpage2") || !strcmp(screen, "dexpickpage2")) {
    onSwipe(-1);
    render();
  }
  if (!strcmp(screen, "rewardscroll") || !strcmp(screen, "bagscroll")) {
    onSwipeV(-1);
    onSwipeV(-1);
    render();
  }
  writePPM(out);
  return 0;
}

int main(int argc, char **argv) {
  int scale = 2;
  g_argv = argv;
  const char *save = "tamapoke.nvs";
  const char *shot = nullptr, *shotOut = "shot.ppm";
  const char *locale = nullptr;
  const char *wav = nullptr, *demo = nullptr;
  int shotLvl = 0, shotIv = -1, shotDex = 6;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--fast") && i + 1 < argc) emuSetTimeScale(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
    else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
    else if (!strcmp(argv[i], "--demo") && i + 1 < argc) demo = argv[++i];
    else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) shotOut = argv[++i];
    else if (!strcmp(argv[i], "--lang") && i + 1 < argc) locale = argv[++i];
    else if (!strcmp(argv[i], "--lvl") && i + 1 < argc) shotLvl = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--iv") && i + 1 < argc) shotIv = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--dex") && i + 1 < argc) shotDex = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--wipe")) { remove(save); }
  }
  // Audio preview: no SDL, no board -- just a file you can listen to.
  if (wav) return wavMain(wav, demo);
  if (shot) return shotMode(shot, shotOut, shotLvl, shotIv, shotDex, locale);  // headless: no SDL
  g_crashFile = std::string(save) + ".crash";
  crashRestore();     // a simulated panic left its breadcrumb here
  nvsLoad(save);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_Window *win = SDL_CreateWindow("TamaPoke (emulator)", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, PANEL * scale, PANEL * scale,
                                     SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888,
                                       SDL_TEXTUREACCESS_STREAMING, PANEL, PANEL);

  printf("TamaPoke emulator — click to touch, drag to swipe, hold 3s to release.\n");
  printf("Type serial commands here (STATS, IV 31 31 31 31, EGG 150 1, LVL 73, WIPE...)\n");
  printf("Time scale x%u (suspended while you touch). Ctrl-C or close the window to quit.\n\n",
         emuTimeScale());

  setup();
  if (locale) {
    int8_t selected = uiFindLocale(locale);
    if (selected >= 0) { setLang((Lang)selected); refreshUiFont(); }
    else fprintf(stderr, "unknown installed locale: %s\n", locale);
  }

  bool run = true;
  std::vector<uint32_t> px(PANEL * PANEL);
  auto updateTouchPosition = [&](int x, int y) {
    int windowW = 0, windowH = 0;
    SDL_GetWindowSize(win, &windowW, &windowH);
    g_touchX = emuPanelCoord(x, windowW, PANEL);
    g_touchY = emuPanelCoord(y, windowH, PANEL);
  };
  while (run) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) run = false;
      // Every mouse event pulses the touch INT, the way the CST9217 does on the
      // board. The sketch ignores the panel entirely until that fires.
      else if (e.type == SDL_MOUSEBUTTONDOWN) {
        updateTouchPosition(e.button.x, e.button.y);
        g_touchDown = true;
        emuFireInterrupt();
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        g_touchDown = false;
        emuFireInterrupt();
      } else if (e.type == SDL_MOUSEMOTION && g_touchDown) {
        updateTouchPosition(e.motion.x, e.motion.y);
        emuFireInterrupt();
      } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) run = false;
    }
    pumpStdin();
    loop();

    if (gfx && gfx->frameReady) {
      gfx->frameReady = false;
      const uint16_t *fb = gfx->buffer();
      for (int y = 0; y < PANEL; y++)
        for (int x = 0; x < PANEL; x++) {
          uint16_t c = fb[y * PANEL + x];
          uint32_t r = ((c >> 11) & 31) * 255 / 31;
          uint32_t g = ((c >> 5) & 63) * 255 / 63;
          uint32_t b = (c & 31) * 255 / 31;
          // dim what falls outside the round panel: those pixels exist in the
          // framebuffer but are not visible on the real hardware
          int dx = x - 233, dy = y - 233;
          if (dx * dx + dy * dy > 233 * 233) { r /= 4; g /= 4; b /= 4; }
          px[y * PANEL + x] = (r << 16) | (g << 8) | b;
        }
      SDL_UpdateTexture(tex, nullptr, px.data(), PANEL * 4);
      SDL_RenderCopy(ren, tex, nullptr, nullptr);
      SDL_RenderPresent(ren);
    }
    SDL_Delay(5);
  }

  nvsSave(save);
  printf("\nemu: state saved to %s\n", save);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
