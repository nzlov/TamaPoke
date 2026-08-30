#pragma once
#include <stdint.h>
#include "battle.h"
#include "trainers.h"   // TRAINER_TEAM_MAX: a link team is the same size

// Peer-to-peer battles.
//
// ONE DEVICE IS AUTHORITATIVE. That is forced, not chosen: battle.cpp makes 11
// random() calls a turn -- crit, damage spread, accuracy, ailment procs,
// multi-hit count, confusion, thaw, wake, speed ties -- so two devices
// resolving independently desync inside a single turn. A shared seed only holds
// until the two builds differ by one call. The host runs battleAct(); the guest
// sends an action and renders the result it is handed, and therefore needs no
// game logic at all.
//
// THE RADIO DROPS FRAMES. ESP-NOW is best effort, so nothing here may assume a
// packet arrived. Three rules cover it:
//
//   1. Every exchange is stamped with a turn number. A repeat of a turn already
//      handled is ignored, so a resend can never be mistaken for a new choice.
//   2. Whatever we last said is resent until something supersedes it. That is
//      what makes a lost frame a delay rather than a permanent stall.
//   3. A LinkResult carries ABSOLUTE state, never deltas. A guest that misses a
//      turn entirely still lands on the right numbers from the next one -- the
//      loss costs narration, not correctness.
//
// And every wait has a deadline: a peer that goes out of range or powers off
// ends as LINK_LOST with a message, never as a hang.
//
// The transport is a function pointer rather than a direct ESP-NOW call so the
// protocol can be tested: two Links cross-wired in one process exercise the
// whole handshake without a radio, and a deliberately lossy transport exercises
// all of the above. Only the radio itself is unverifiable here.

#define LINK_PROTO 12       // bump on ANY wire change; a mismatch is refused
#define LINK_MAX_PAYLOAD 200
#define LINK_NAME_LEN 12

#define LINK_RESEND_MS 400
// Resends are jittered, and that is not decoration. A fixed interval can lock
// step with a periodic interferer -- a beacon, a microwave -- so that the same
// packet in the cycle is lost every single time and the resends never help.
// The lossy test reproduces exactly that: with one frame in three dropped and a
// fixed interval, one squad packet was destroyed on all ten attempts.
#define LINK_JITTER_MS 200
#define LINK_PAIR_TIMEOUT_MS 20000    // waiting for a peer that may not exist
#define LINK_BATTLE_TIMEOUT_MS 12000  // mid-fight, where one is known to be there

// An action is a move slot, or a switch with the high bit set. One message
// rather than two, so turn matching and resend have a single path -- which is
// the place a second message type would most easily drift out of step.
//
// A move is stored as slot+1 so that 0 can keep meaning "nothing chosen yet".
// Storing the slot raw would make move slot 0 -- the first move every creature
// has -- indistinguishable from silence.
#define LINK_ACT_SWITCH 0x80
#define LINK_ACT_MOVE(i) ((uint8_t)((i) + 1))
#define LINK_ACT_SWITCH_TO(i) ((uint8_t)(LINK_ACT_SWITCH | ((i) & 0x0F)))
#define LINK_ACT_IS_SWITCH(a) (((a) & LINK_ACT_SWITCH) != 0)
#define LINK_ACT_SLOT(a) \
  ((uint8_t)(LINK_ACT_IS_SWITCH(a) ? ((a) & 0x0F) : ((a) - 1)))

enum LinkState : uint8_t {
  LINK_OFF = 0,
  LINK_LISTENING,    // waiting for a peer
  LINK_HANDSHAKE,    // hello sent, waiting for theirs
  LINK_SQUADS,       // teams being exchanged
  LINK_READY,        // both known; an action may be chosen
  LINK_WAITING,      // acted, waiting on the other side
  LINK_DONE,         // somebody won
  LINK_REFUSED,      // incompatible -- loudly, never a silent desync
  LINK_LOST,         // peer went quiet, or said goodbye
};

enum LinkMsg : uint8_t {
  LM_HELLO = 1,
  LM_SQUAD,
  LM_ACT,        // guest -> host: turn, move/switch, and answer percentage
  LM_RESULT,     // host -> guest: what happened, stamped with the same turn
  LM_END,
  LM_BYE,        // leaving on purpose, so the peer need not wait for a timeout
  LM_REMATCH,    // go again with the same squads
  LM_WAIT,       // answer-time keepalive request/acknowledgement for one turn
};

// A creature on the wire. Deliberately not `Combatant` itself: that carries
// live battle state (stages, ailments, timers) which only the host owns.
struct LinkMon {
  int16_t dex;
  uint8_t level;
  uint16_t maxHp;
  uint16_t base[SI_COUNT];
  MoveId moves[MOVE_SLOTS];
  uint64_t moveUses[MOVE_SLOTS];
  uint8_t shiny;
  uint8_t sparkle;  // legacy mirror retained for packet compatibility
  uint8_t gender;
  uint8_t gigantamaxFactor;
  AbilityKey ability;
  char name[LINK_NAME_LEN];
};

// What the host tells the guest happened. Everything the guest needs to render
// a turn, and nothing it could use to resolve one -- it has no say in the
// outcome, which is the whole point of one side being authoritative.
//
// Every field is ABSOLUTE, not a delta. That is what lets a guest which missed
// a turn recover from the next one instead of drifting.
struct LinkResult {
  uint16_t hostHp, guestHp;
  uint16_t hostMaxHp, guestMaxHp;
  uint8_t hostAil, guestAil;
  uint8_t hostAilTurns, guestAilTurns;
  uint8_t hostConfuseTurns, guestConfuseTurns;
  MoveId hostMove, guestMove;
  uint16_t hostDmg, guestDmg;
  uint8_t hostIdx, guestIdx;   // which squad member is out on each side
  uint8_t flags;
  uint8_t moveProgressFlags;  // used/knockout bits for host and guest actions
  uint8_t hostType1, hostType2, guestType1, guestType2;
  BattleMechanic hostActive, guestActive;
  MegaFormKind hostMegaForm, guestMegaForm;
  BattleForm hostForm, guestForm;
  uint8_t hostFormPrimed, guestFormPrimed;
  uint8_t hostGigantamax, guestGigantamax;
  GmaxMoveId hostGmaxMove, guestGmaxMove;
  uint8_t hostMoveType, guestMoveType;
  BattleMechanic hostMoveMechanic, guestMoveMechanic;
  uint8_t hostDynamaxTurns, guestDynamaxTurns;
  uint8_t hostUsedMask, guestUsedMask;
  uint8_t baseWeather, weather, weatherTurns;
  uint8_t baseTerrain, terrain, terrainTurns;
  uint8_t gravityTurns;
  int8_t hostStage[SI_COUNT], guestStage[SI_COUNT];
  int8_t hostAccuracyStage, hostEvasionStage;
  int8_t guestAccuracyStage, guestEvasionStage;
  uint8_t sideReflectTurns[2], sideLightScreenTurns[2], sideAuroraVeilTurns[2];
  uint8_t sideSpikesLayers[2], sideToxicSpikesLayers[2];
  uint8_t sideHazardFlags[2];  // bit0 Stealth Rock, bit1 Sticky Web, bit2 Steelsurge
  uint8_t sideCritStages[2], sideGmaxResidualEffect[2], sideGmaxResidualTurns[2];
  uint8_t hostStatPercent, guestStatPercent;
  uint8_t hostBindTurns, guestBindTurns, hostDrowsyTurns, guestDrowsyTurns;
  uint8_t hostVolatileFlags, guestVolatileFlags;  // trapped, tormented, infatuated
  MoveId hostLastMove, guestLastMove;
  uint16_t hostBase[SI_COUNT], guestBase[SI_COUNT];
  BattleMechanic hostMemberMechanic[TRAINER_TEAM_MAX];
  BattleMechanic guestMemberMechanic[TRAINER_TEAM_MAX];
  MegaFormKind hostMemberMegaForm[TRAINER_TEAM_MAX];
  MegaFormKind guestMemberMegaForm[TRAINER_TEAM_MAX];
  BattleForm hostMemberForm[TRAINER_TEAM_MAX];
  BattleForm guestMemberForm[TRAINER_TEAM_MAX];
  uint8_t hostMemberFormPrimed[TRAINER_TEAM_MAX];
  uint8_t guestMemberFormPrimed[TRAINER_TEAM_MAX];
};

constexpr uint8_t LINK_RESULT_FAINT = 0x04;
constexpr uint8_t LINK_RESULT_HOST_FIRST = 0x08;
constexpr uint8_t LINK_RESULT_GUEST_REPLENISH = 0x10;
constexpr uint8_t LINK_PROGRESS_HOST_USED = 1u << 0;
constexpr uint8_t LINK_PROGRESS_GUEST_USED = 1u << 1;
constexpr uint8_t LINK_PROGRESS_HOST_KNOCKOUT = 1u << 2;
constexpr uint8_t LINK_PROGRESS_GUEST_KNOCKOUT = 1u << 3;
static_assert(sizeof(LinkResult) <= 0xFFu,
              "LinkResult must fit the one-byte protocol length");

struct Link {
  uint8_t state = LINK_OFF;
  bool isHost = false;
  uint8_t protoTheirs = 0;
  uint16_t buildTheirs = 0;
  uint16_t id = 0;             // breaks the tie when both sides offer to host
  uint16_t peerId = 0;
  char peerName[LINK_NAME_LEN] = "";

  LinkMon mine[TRAINER_TEAM_MAX];
  uint8_t mineN = 0;
  LinkMon theirs[TRAINER_TEAM_MAX];
  uint8_t theirsN = 0;
  uint8_t theirsExpected = 0;  // from their hello, so a lost SQUAD is detectable
  uint16_t theirsGot = 0;      // which slots have actually arrived

  uint8_t turn = 0;            // the exchange both sides are on
  uint8_t pendingAct = 0;      // host: the guest's action for `turn`, 0 = none
  uint8_t pendingPercent = 0;  // host: answer effect for that move, 0..100
  BattleMechanic pendingMechanic = BMECH_NONE;
  MegaFormKind pendingMegaForm = MEGA_FORM_NONE;
  bool youWon = false;

  // guest: the last turn the host resolved. `resultNew` is cleared by whoever
  // renders it, so a dropped or duplicated frame cannot replay a turn twice.
  uint8_t result[LINK_MAX_PAYLOAD];
  uint8_t resultN = 0;
  bool resultNew = false;

  // what we last said, kept so it can be said again until superseded
  uint8_t tx[2 + LINK_MAX_PAYLOAD];
  uint8_t txN = 0;
  bool txLive = false;
  uint32_t txAt = 0;
  uint32_t lastRx = 0;
  uint8_t resendSeq = 0;       // varies the resend interval, see LINK_JITTER_MS
  bool sawRx = false;          // onPacket has no clock; tick() stamps it
  bool armed = false;

  // set by whoever owns the radio; ctx lets a test route two Links to each other
  void (*send)(void *ctx, const uint8_t *buf, uint8_t len) = nullptr;
  void *ctx = nullptr;

  void begin(bool host, const char *myName);
  void addMon(const LinkMon &m);
  void start();                       // announce: hello, then squad
  void onPacket(const uint8_t *buf, uint8_t len);
  void tick(uint32_t now);            // resend + timeout; call once a frame

  void sendAct(uint8_t act, uint8_t percent = 100,
               BattleMechanic mechanic = BMECH_NONE,
               MegaFormKind megaForm = MEGA_FORM_NONE);  // guest -> host
  void sendWait();                    // request reciprocal proof of life while answering
  void sendResult(const uint8_t *blob, uint8_t len);   // host -> guest
  void sendEnd(bool hostWon);
  void sendBye();                     // leaving deliberately
  void sendRematch();
  void rearm();                       // same squads, fresh fight

  bool ready() const { return state == LINK_READY; }
  bool live() const {
    return state != LINK_OFF && state != LINK_REFUSED && state != LINK_LOST;
  }
  // the host's own action is its own business; this is the guest's, and it
  // only counts once it has actually arrived
  bool hasPeerAct() const { return isHost && pendingAct != 0; }
};

// Everything that must match for two builds to narrate the same fight. NOT the
// firmware version: a build differing only in, say, a colour is perfectly
// compatible and refusing it would be noise. These are the things that would
// actually make one device read a packet differently from the other.
uint16_t linkBuildTag();

// Builds the wire form of a combatant, so both ends agree on what a creature is.
// linkMonTo VALIDATES: everything it produces is safe to index a table with,
// whatever actually arrived over the air.
void linkMonFrom(LinkMon &out, const Combatant &c);
void linkMonTo(Combatant &out, const LinkMon &m);
MoveId linkSafeMove(MoveId m);        // 0 unless it is a real move
