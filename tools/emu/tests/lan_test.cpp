// The LAN battle as the sketch actually wires it: what squad you fight with,
// what the guest is allowed to do, and whether a result off the wire lands on
// the right creature. The protocol itself is link_test's job; this is the half
// that lives in TamaPoke.ino and that link_test cannot reach.
//
// The radio is still untested -- it needs two boards. Everything on either side
// of it is checked here.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "i18n.h"
#include "inventory.h"
#include "link.h"
#include "quiz.h"
#include <chrono>
#include <cstdio>
#include <thread>
uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}

void setup(); void render(); void battleTap(int16_t,int16_t);
extern Pet pet;
extern Link lan;
extern QuizRuntime quiz;
extern bool battleOpen, btlLink, btlLinkHost, btlOver, btlWon, lanOpen;
extern Combatant btlYou, btlFoe;
extern Combatant btlSquad[];
extern uint8_t btlSquadN, btlSquadAt, btlFoeAt, btlMenu, btlMsgCount;
extern char btlMsg[6][64];
extern uint8_t btlMyAct, btlMyPercent, btlFoeSquadN;
extern Combatant btlFoeSquad[];
extern BattleSideMechanics btlYourMechanics, btlFoeMechanics;
extern BattleField btlField;
extern ItemRef btlLastConsumedItem;
extern uint16_t squadMask;
struct BtlTurnBeat {
  char text[96];
  uint16_t hp[2];
  uint8_t kind, actor, target, moveType, moveStyle, sfx;
  bool hit, faint;
};
extern BtlTurnBeat btlTurnBeats[];
extern uint8_t btlTurnBeatCount;
extern bool btlTurnAnimating, btlTurnShowingRound;
extern uint32_t btlTurnBeatStartedAt;
void btlUpdateTurnPresentation(uint32_t now);
extern bool pickOpen, lanWantHost;
extern uint8_t pickTrainer, pickPage;
extern bool pickHard;
void startLinkBattle();
void updateQuiz(uint32_t now);
void pickTap(int16_t x, int16_t y);
void pickDefault(uint8_t cap);
uint8_t squadCap(uint8_t, bool);
uint8_t pickChosen();
#define PICK_LAN 0xFF

// The sketch never sets a transport itself -- linkNowBegin does, and there is
// no radio here -- so one is wired in to see what the UI actually sends.
static int sent=0; static uint8_t lastPkt[64]; static uint8_t lastLen=0;
static void capture(void*, const uint8_t*b, uint8_t n){
  sent++; lastLen = n < sizeof(lastPkt) ? n : sizeof(lastPkt); memcpy(lastPkt,b,lastLen);
}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void openBattleRound() {
  if (btlTurnAnimating && btlTurnShowingRound)
    btlUpdateTurnPresentation(btlTurnBeatStartedAt + 700UL);
}

static bool answerBattleQuiz(uint32_t elapsedMs) {
  if (!quiz.active) return false;
  uint32_t start = 1000;
  quiz.markRendered(start);
  bool answered = false;
  if (quiz.kind == QUIZ_QUESTION_CHOICE) {
    answered = quiz.choose(quiz.choice.correctIndex, start + elapsedMs);
  } else {
    snprintf(quiz.input, sizeof(quiz.input), "%s", quiz.expected);
    answered = quiz.submit(start + elapsedMs);
  }
  if (!answered) return false;
  updateQuiz(quiz.feedbackUntil);
  return !quiz.active;
}

static LinkMon mon(int16_t dex, uint8_t lvl, const char *nm){
  Pet p; p.dbgHatchAs(dex,false);
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=25;
  p.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  Combatant c; combatantFromPet(c,p);
  snprintf(c.name,sizeof(c.name),"%s",nm);
  LinkMon m; linkMonFrom(m,c); return m;
}

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  // a party that is deliberately NOT the squad we advertise
  for (int i=0;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=20+i; m.level=30;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; party.replaceAt(i,m); }
  squadMask = 0xFFFF;

  lan.begin(true,"HOST");
  snprintf(lan.peerName,sizeof(lan.peerName),"%s","RIVAL");
  lan.addMon(mon(9,50,"SHELL"));
  lan.addMon(mon(143,50,"SNORE"));
  lan.theirs[0]=mon(6,50,"BLAZE");
  lan.theirs[1]=mon(65,50,"SPOON");
  lan.theirsN=2;
  lan.state=LINK_READY;
  lan.send=capture;

  startLinkBattle();
  openBattleRound();
  quiz.config.questionTypes = QUIZ_TYPE_ARITHMETIC;
  quiz.config.choiceWeight = 0;
  ck(battleOpen && btlLink && btlLinkHost, "the host starts a linked battle");
  // The party above is full of level-30 nobodies and the live pet is level 1.
  // If the squad were rebuilt from squadMask we would fight with those instead
  // of what the peer was told we have -- which is exactly the desync to avoid.
  ck(btlSquadN==2, "the squad is the advertised one, not the current party");
  ck(btlYou.dex==9 && btlYou.level==50, "and leads with what we sent");
  ck(!strcmp(btlSquad[1].name,"SNORE"), "including the rest of it");
  ck(btlFoe.dex==6 && btlFoe.hp==btlFoe.maxHp, "the rival leads with theirs, at full health");

  // --- the peer's team is LIVE, not rebuilt each time it is looked at
  ck(btlFoeSquadN==2, "the peer's whole team is held as combatants");
  btlFoeSquad[1].hp = 7;
  ck(btlFoeSquad[1].hp==7 && btlFoeSquad[1].maxHp>7,
     "so a creature that switches out can stay hurt");

  // --- the guest ASKS to switch; it never switches on its own, because the
  // host is the only side that may spend a turn
  btlLinkHost = false;
  lan.isHost = false;
  lan.state = LINK_READY;
  btlMenu = 2;                       // the POKEMON list
  uint8_t was = btlSquadAt;
  sent = 0;
  battleTap(69 + 168 + 10, 286 + 10);   // cell 1 of the switch grid
  ck(btlSquadAt==was, "the guest does not switch locally");
  ck(sent==1 && lastPkt[0]==LM_ACT, "it sends the request instead");
  ck(LINK_ACT_IS_SWITCH(lastPkt[3]) && LINK_ACT_SLOT(lastPkt[3])==1,
     "and the request names the slot it wants");
  ck(lastPkt[1]==5 && lastPkt[4]==100 && lastPkt[5]==BMECH_NONE &&
     lastPkt[6]==MEGA_FORM_NONE,
     "a switch carries the fixed full-effect percentage");

  // --- a guest answers locally before its move is sent. The answer ratio is
  // part of that turn's action, while LM_WAIT keeps a long question alive.
  std::this_thread::sleep_for(std::chrono::milliseconds(310));
  lan.state = LINK_READY;
  startLinkBattle();
  openBattleRound();
  btlMenu = 1;
  sent = 0;
  battleTap(69 + 10, 286 + 10);
  ck(quiz.active && sent==1 && lastPkt[0]==LM_WAIT,
     "the guest sends only a keepalive while answering");
  ck(answerBattleQuiz(15000), "the guest can finish its local battle question");
  ck(lan.state==LINK_WAITING && lastPkt[0]==LM_ACT && lastPkt[1]==5,
     "the guest sends its move only after feedback");
  ck(lastPkt[4]==50, "the guest's 50 percent answer travels with that move");

  // --- the host LATCHES its own action instead of throwing it away
  btlLinkHost = true;
  lan.isHost = true;
  lan.state = LINK_READY;
  startLinkBattle();
  openBattleRound();
  btlLink = true;
  btlOver = false;
  btlMsgCount = 0;
  lan.pendingAct = 0;
  btlMyAct = 0;
  btlMyPercent = 0;
  btlMenu = 1;                       // the move grid
  uint16_t hpWas = btlFoe.hp;
  sent = 0;
  std::this_thread::sleep_for(std::chrono::milliseconds(310));
  battleTap(69 + 10, 286 + 10);      // move slot 0
  ck(quiz.active && !btlMyAct && sent==1 && lastPkt[0]==LM_WAIT,
     "the host also answers before latching its move");
  ck(btlFoe.hp == hpWas, "and nothing is resolved yet");
  ck(answerBattleQuiz(15000), "the host can finish its local battle question");
  ck(btlMyAct != 0 && btlMyPercent==50,
     "the host latches its move with the local answer percentage");
  lan.pendingAct = LINK_ACT_MOVE(0);
  lan.pendingPercent = 100;
  lan.pendingMechanic = BMECH_NONE;
  render();                          // btlLinkPoll spots that both are in
  ck(btlMyAct == 0 && !btlMyPercent && !lan.pendingAct && !lan.pendingPercent,
     "and the turn goes as soon as the rival's action lands");

  // --- asymmetric rules are legal: the side with questions disabled submits
  // 100 percent immediately, while the answering peer keeps the turn pending.
  lan.isHost = false;
  lan.state = LINK_READY;
  startLinkBattle();
  openBattleRound();
  quiz.config.questionTypes = 0;
  btlMenu = 1;
  sent = 0;
  battleTap(69 + 10, 286 + 10);
  ck(!quiz.active && sent == 1 && lastPkt[0] == LM_ACT && lastPkt[4] == 100,
     "a guest with questions disabled submits 100 percent immediately");

  lan.isHost = true;
  lan.state = LINK_READY;
  startLinkBattle();
  openBattleRound();
  quiz.config.questionTypes = 0;
  btlMenu = 1;
  btlMyAct = 0;
  btlMyPercent = 0;
  lan.pendingAct = 0;
  lan.pendingPercent = 0;
  lan.pendingMechanic = BMECH_NONE;
  uint16_t asymmetricHpWas = btlFoe.hp;
  battleTap(69 + 10, 286 + 10);
  ck(!quiz.active && btlMyAct != 0 && btlMyPercent == 100 &&
     btlFoe.hp == asymmetricHpWas,
     "a host with questions disabled waits for an answering guest");
  lan.pendingAct = LINK_ACT_MOVE(0);
  lan.pendingPercent = 50;
  lan.pendingMechanic = BMECH_NONE;
  render();
  ck(!btlMyAct && !btlMyPercent && !lan.pendingAct && !lan.pendingPercent,
     "the asymmetric turn resolves once after the answering guest submits");
  quiz.config.questionTypes = QUIZ_TYPE_ARITHMETIC;

  // --- a peer that goes quiet ends the fight rather than hanging on it
  btlOver = false; btlMsgCount = 0;
  lan.state = LINK_LOST;
  render();
  ck(btlOver, "a lost peer ends the battle instead of waiting forever");
  lan.state = LINK_READY;
  btlOver = false; btlMsgCount = 0;

  // --- a result off the wire moves the guest's battle, and only once
  btlLink = true; btlLinkHost = false; btlMenu = 0; btlMsgCount = 0;
  btlYou = btlSquad[btlSquadAt];
  LinkResult r{};
  uint16_t guestNormalMaxHp = btlYou.maxHp;
  r.hostHp = btlFoe.maxHp/2; r.guestHp = guestNormalMaxHp/2;
  r.hostMaxHp = btlFoe.maxHp; r.guestMaxHp = guestNormalMaxHp*2;
  r.hostMove = 1; r.guestMove = 1; r.hostDmg = 5; r.guestDmg = 7;
  r.hostMoveType = moveEntry(r.hostMove).type;
  r.guestMoveType = moveEntry(r.guestMove).type;
  const GmaxMoveEntry *wireWildfire = gmaxMoveFor(6, T_FIRE);
  if (wireWildfire) r.hostGmaxMove = wireWildfire->id;
  r.hostIdx = 0; r.guestIdx = 0;
  r.guestAil = AIL_BURN;
  r.guestAilTurns = 2;
  r.guestConfuseTurns = 3;
  r.hostType1 = btlFoe.type1; r.hostType2 = btlFoe.type2;
  r.guestType1 = btlYou.type1; r.guestType2 = btlYou.type2;
  r.hostActive = BMECH_MEGA;
  r.hostMegaForm = MEGA_FORM_X;
  r.guestActive = BMECH_DYNAMAX;
  r.guestGigantamax = 1;
  r.guestDynamaxTurns = 2;
  r.hostForm = BFORM_AEGISLASH_BLADE;
  r.guestForm = BFORM_PALAFIN_HERO;
  r.guestFormPrimed = 1;
  r.hostUsedMask = battleMechanicBit(BMECH_MEGA);
  r.guestUsedMask = battleMechanicBit(BMECH_DYNAMAX);
  r.baseWeather = BWEATHER_RAIN;
  r.weather = BWEATHER_SUN;
  r.weatherTurns = 3;
  r.baseTerrain = BTERRAIN_ELECTRIC;
  r.terrain = BTERRAIN_GRASSY;
  r.terrainTurns = 2;
  r.gravityTurns = 4;
  r.guestStage[SI_ATK] = 2;
  r.hostStage[SI_DEF] = -1;
  r.guestAccuracyStage = 1;
  r.hostEvasionStage = 2;
  r.sideReflectTurns[1] = 4;
  r.sideSpikesLayers[0] = 3;
  r.sideToxicSpikesLayers[1] = 2;
  r.sideHazardFlags[0] = 7;
  r.sideCritStages[1] = 2;
  r.sideGmaxResidualEffect[1] = GMAX_EFFECT_WILDFIRE;
  r.sideGmaxResidualTurns[1] = 3;
  r.guestStatPercent = 60;
  r.hostStatPercent = 80;
  r.guestBindTurns = 4;
  r.guestDrowsyTurns = 2;
  r.guestVolatileFlags = 7;
  r.guestLastMove = 1;
  const ItemEntry *replenished = itemCount() ? itemAt(0) : nullptr;
  uint8_t replenishedBefore = 0;
  if (replenished) {
    inventory.add(replenished->key);
    inventory.consume(replenished->key);
    replenishedBefore = inventory.count(replenished->key);
    btlLastConsumedItem = { replenished->key, MOVE_NONE };
    r.flags |= LINK_RESULT_GUEST_REPLENISH;
  }
  r.hostMemberMechanic[0] = BMECH_MEGA;
  r.guestMemberMechanic[0] = BMECH_DYNAMAX;
  r.hostMemberForm[0] = BFORM_AEGISLASH_BLADE;
  r.guestMemberForm[0] = BFORM_PALAFIN_HERO;
  r.guestMemberFormPrimed[0] = 1;
  for (uint8_t i=0;i<SI_COUNT;i++) { r.hostBase[i]=btlFoe.base[i]; r.guestBase[i]=btlYou.base[i]; }
  memcpy(lan.result,&r,sizeof(r)); lan.resultN=sizeof(r); lan.resultNew=true;
  render();
  ck(btlYou.hp==r.guestHp && btlFoe.hp==r.hostHp, "the guest takes the host's numbers");
  ck(btlYou.ailment==AIL_BURN && btlYou.ailTurns==2 && btlYou.confuseTurns==3,
     "and the ailment timers it was handed");
  ck(btlYou.maxHp==guestNormalMaxHp*2 && btlYou.normalMaxHp==guestNormalMaxHp &&
     btlYou.activeMechanic==BMECH_DYNAMAX && btlYou.dynamaxTurns==2,
     "and restores the guest's absolute Dynamax state");
  ck(btlYou.gigantamax && btlFoe.megaForm==MEGA_FORM_X,
     "and restores exact Gigantamax and Mega forms");
  ck(btlYou.form==BFORM_PALAFIN_HERO && btlYou.formPrimed &&
     btlFoe.form==BFORM_AEGISLASH_BLADE,
     "and restores active and primed exclusive forms");
  ck(btlYourMechanics.used(BMECH_DYNAMAX) &&
     btlFoeMechanics.used(BMECH_MEGA) && btlYou.usedMechanic==BMECH_DYNAMAX,
     "and restores the per-team and per-creature mechanic limits");
  ck(btlField.baseWeather==BWEATHER_RAIN && btlField.weather==BWEATHER_SUN &&
     btlField.weatherTurns==3 && btlField.baseTerrain==BTERRAIN_ELECTRIC &&
     btlField.terrain==BTERRAIN_GRASSY && btlField.terrainTurns==2 &&
     btlField.gravityTurns==4,
     "and restores the host-authoritative field state");
  ck(btlYou.stage[SI_ATK]==2 && btlFoe.stage[SI_DEF]==-1 &&
     btlYou.accuracyStage==1 && btlFoe.evasionStage==2,
     "and restores ordinary, accuracy and evasion stages");
  ck(btlField.sides[0].reflectTurns==4 &&
     btlField.sides[0].toxicSpikesLayers==2 &&
     btlField.sides[0].critStages==2 &&
     btlField.sides[0].gmaxResidualEffect==GMAX_EFFECT_WILDFIRE &&
     btlField.sides[0].gmaxResidualTurns==3 &&
     btlField.sides[1].spikesLayers==3 &&
     btlField.sides[1].stealthRock && btlField.sides[1].stickyWeb &&
     btlField.sides[1].steelsurge,
     "and maps host and guest side conditions to the local perspective");
  ck(btlYou.statPercent==60 && btlFoe.statPercent==80 &&
     btlYou.bindTurns==4 && btlYou.drowsyTurns==2 && btlYou.trapped &&
     btlYou.tormented && btlYou.infatuated && btlYou.lastMove==1,
     "and restores Gigantamax volatile combatant state");
  ck(!replenished ||
     (inventory.count(replenished->key)==replenishedBefore+1 &&
      !btlLastConsumedItem),
     "and applies a guest G-Max Replenish result exactly once");
  bool narratedField = false;
  bool narratedGmax = false;
  for (uint8_t i=0;i<btlTurnBeatCount;i++)
    narratedField |= strstr(btlTurnBeats[i].text, T(S_FIELD_SUN)) ||
                     strstr(btlTurnBeats[i].text, T(S_FIELD_GRASSY));
  for (uint8_t i=0;i<btlTurnBeatCount;i++)
    narratedGmax |= wireWildfire &&
        strstr(btlTurnBeats[i].text, gmaxMoveName(wireWildfire->id));
  ck(narratedField, "and narrates a field transition in the guest's locale");
  ck(narratedGmax, "and narrates the official G-Max move name on the guest");
  ck(!lan.resultNew, "a result is consumed once");
  uint16_t hpNow = btlYou.hp;
  uint8_t replenishedAfter = replenished
      ? inventory.count(replenished->key) : 0;
  render();
  ck(btlYou.hp==hpNow &&
     (!replenished || inventory.count(replenished->key)==replenishedAfter),
     "so a second frame does not replay the turn or item restoration");
  btlUpdateTurnPresentation(btlTurnBeatStartedAt + 60000);
  ck(btlTurnAnimating && btlTurnShowingRound,
     "the guest presentation advances to the next round title");
  openBattleRound();

  // --- the rival's next creature arrives when the host says so
  r.hostIdx = 1; r.hostHp = 60;
  memcpy(lan.result,&r,sizeof(r)); lan.resultNew=true;
  render();
  ck(btlFoeAt==1 && btlFoe.dex==65, "the rival's next creature comes off the wire");
  btlUpdateTurnPresentation(btlTurnBeatStartedAt + 60000);
  openBattleRound();

  // --- and the guest never decides the ending
  lan.youWon = true; lan.state = LINK_DONE;
  render();
  ck(btlOver && btlWon, "the guest takes the ending from the host");

  // a short or empty result must be dropped, not read past the end
  btlOver=false; lan.state=LINK_READY;
  uint16_t before = btlYou.hp;
  lan.resultN = 3; lan.resultNew = true;
  render();
  ck(!lan.resultNew && btlYou.hp==before, "a runt result is dropped, not half-applied");

  // --- the team you pick is the team that gets offered
  {
    lanOpen = false; battleOpen = false; btlLink = false;
    for (int i=0;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=30+i*5; m.level=45;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; party.replaceAt(i,m); }
    lanWantHost = true;
    pickTrainer = PICK_LAN; pickHard = false; pickPage = 0;
    pickDefault(squadCap(PICK_LAN, false));
    ck(squadCap(PICK_LAN,false)==TRAINER_TEAM_MAX,
       "a LAN battle is uncapped: bring what you like");
    pickOpen = true;

    // keep cultivation slots 0 and 2
    squadMask = (1 << 0) | (1 << 2);
    ck(pickChosen()==2, "two chosen");
    lan.begin(true,"T");
    pickTap(300, 366);                    // FIGHT (right of BACK now)
    ck(!pickOpen && lanOpen, "confirming the team opens the LAN screen");
    ck(lan.mineN==2, "and offers exactly what was chosen, not the whole party");
    ck(lan.mine[1].dex==40, "including the right cultivation slot");
    // the emulator has no radio, so the offer cannot go anywhere -- but the
    // squad must still have been built, which is the half that matters here
    ck(lan.state==LINK_REFUSED, "with no radio it says so rather than hanging");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
