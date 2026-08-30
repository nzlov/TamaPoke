#pragma once
#include <Arduino.h>
#include "pet.h"
#include "party.h"
#include "moves.h"
#include "items.h"
#include "content.h"

// Turn resolution. Deliberately free of any UI or hardware so the whole thing
// can be simulated headlessly -- see tools/emu.
//
// Integer maths only, like types.h: no floats anywhere on the MCU.

enum : uint8_t { SI_ATK = 0, SI_DEF, SI_SPA, SI_SPD, SI_SPE, SI_COUNT };
enum : uint8_t { BSI_ACC = SI_COUNT, BSI_EVA, BATTLE_STAGE_COUNT };
constexpr uint8_t BATTLE_ANGRY_STAT_PERCENT = 105;

enum BattleWeather : uint8_t {
  BWEATHER_NONE = 0,
  BWEATHER_SUN,
  BWEATHER_RAIN,
  BWEATHER_SAND,
  BWEATHER_SNOW,
};

enum BattleTerrain : uint8_t {
  BTERRAIN_NONE = 0,
  BTERRAIN_ELECTRIC,
  BTERRAIN_GRASSY,
  BTERRAIN_MISTY,
  BTERRAIN_PSYCHIC,
};

constexpr uint8_t BATTLE_FIELD_TURNS = 5;

struct BattleSideConditions {
  uint8_t reflectTurns = 0;
  uint8_t lightScreenTurns = 0;
  uint8_t auroraVeilTurns = 0;
  uint8_t spikesLayers = 0;
  uint8_t toxicSpikesLayers = 0;
  bool stealthRock = false;
  bool stickyWeb = false;
  bool steelsurge = false;
  uint8_t critStages = 0;
  uint8_t gmaxResidualEffect = GMAX_EFFECT_NONE;
  uint8_t gmaxResidualTurns = 0;
};

// Weather and terrain belong to the battle, not either creature. Wild battles
// may provide a persistent environment; moves temporarily cover that baseline
// and reveal it again when their five turns end.
struct BattleField {
  BattleWeather baseWeather = BWEATHER_NONE;
  BattleWeather weather = BWEATHER_NONE;
  uint8_t weatherTurns = 0;
  BattleTerrain baseTerrain = BTERRAIN_NONE;
  BattleTerrain terrain = BTERRAIN_NONE;
  uint8_t terrainTurns = 0;
  uint8_t gravityTurns = 0;
  BattleSideConditions sides[2];
};

struct FieldLog {
  BattleWeather weatherExpired = BWEATHER_NONE;
  BattleWeather weatherRestored = BWEATHER_NONE;
  BattleTerrain terrainExpired = BTERRAIN_NONE;
  BattleTerrain terrainRestored = BTERRAIN_NONE;
  uint8_t screensExpired[2] = { BSCREEN_NONE, BSCREEN_NONE };
};

struct EntryLog {
  uint16_t hazardDamage = 0;
  uint8_t inflicted = AIL_NONE;
  uint8_t stageMask = 0;
  int8_t stageDelta = 0;
  BattleWeather weatherSet = BWEATHER_NONE;
  BattleTerrain terrainSet = BTERRAIN_NONE;
  bool toxicSpikesAbsorbed = false;
  bool screensCleared = false;
  bool traced = false;
};

enum BattleSwitchRequest : uint8_t {
  BSWITCH_NONE = 0, BSWITCH_TARGET, BSWITCH_USER,
};

enum BattleForm : uint8_t {
  BFORM_BASE = 0,
  BFORM_CASTFORM_SUN, BFORM_CASTFORM_RAIN, BFORM_CASTFORM_SNOW,
  BFORM_CHERRIM_SUN,
  BFORM_DARMANITAN_ZEN, BFORM_AEGISLASH_BLADE,
  BFORM_WISHIWASHI_SCHOOL, BFORM_MINIOR_CORE,
  BFORM_MIMIKYU_DISGUISED, BFORM_MIMIKYU_BUSTED,
  BFORM_CRAMORANT_GULPING, BFORM_CRAMORANT_GORGING,
  BFORM_EISCUE_ICE, BFORM_EISCUE_NOICE,
  BFORM_MORPEKO_FULL, BFORM_MORPEKO_HANGRY,
  BFORM_PALAFIN_HERO,
};

enum BattleMechanic : uint8_t {
  BMECH_NONE = 0,
  BMECH_Z_MOVE = 1,
  BMECH_DYNAMAX = 2,
  BMECH_MEGA = 3,
};

constexpr uint8_t battleMechanicBit(BattleMechanic mechanic) {
  return mechanic == BMECH_NONE ? 0 : (uint8_t)(1u << (mechanic - 1));
}

struct BattleSideMechanics {
  uint8_t usedMask = 0;

  bool used(BattleMechanic mechanic) const {
    return (usedMask & battleMechanicBit(mechanic)) != 0;
  }
};

// A creature as it exists inside a battle. Built from the live pet or from a
// banked party member; nothing here is ever written back, which is what keeps
// ailments battle-only.
struct Combatant {
  int16_t dex = 0;
  uint8_t level = 1;
  uint16_t maxHp = 1, hp = 1;
  uint16_t base[SI_COUNT] = { 1, 1, 1, 1, 1 };  // before stat stages
  uint16_t nativeBase[SI_COUNT] = { 1, 1, 1, 1, 1 };
  uint8_t type1 = T_NORMAL, type2 = T_NONE;
  uint8_t nativeType1 = T_NORMAL, nativeType2 = T_NONE;
  AbilityKey ability = ABILITY_NONE;
  MoveId moves[MOVE_SLOTS] = { 0, 0, 0, 0 };
  uint64_t moveUses[MOVE_SLOTS] = { 0, 0, 0, 0 };
  // One move observed from an NPC opponent. It exists only for this battle and
  // deliberately stays outside the four-slot save and LAN protocol ABIs.
  MoveId observedMove = MOVE_NONE;
  uint8_t bond = 0;
  int8_t stage[SI_COUNT] = { 0, 0, 0, 0, 0 };   // -6..+6
  int8_t accuracyStage = 0;
  int8_t evasionStage = 0;
  uint8_t ailment = AIL_NONE;
  uint8_t ailTurns = 0;      // sleep/freeze countdown
  uint8_t confuseTurns = 0;  // confusion runs alongside a real ailment
  bool angry = false;        // wild capture failure; battle-only and non-stacking
  bool recharge = false;     // EF_RECHARGE spent this creature's next turn
  MoveId charging = 0;       // EF_CHARGE move already wound up
  bool protectedTurn = false;
  BattleMechanic usedMechanic = BMECH_NONE;
  BattleMechanic activeMechanic = BMECH_NONE;
  MegaFormKind megaForm = MEGA_FORM_NONE;
  uint8_t dynamaxTurns = 0;
  uint16_t normalMaxHp = 0;
  bool shiny = false;        // combined alternate sprite and particle effect
  PetGender gender = GENDER_UNKNOWN;
  bool gigantamaxFactor = false;
  bool gigantamax = false;
  uint8_t statPercent = 100;
  uint8_t bindTurns = 0;
  uint8_t drowsyTurns = 0;
  bool trapped = false;
  bool tormented = false;
  bool infatuated = false;
  MoveId lastMove = MOVE_NONE;
  bool flashFireActive = false;
  bool abilityTriggered = false;
  bool abilityCharged = false;
  BattleForm form = BFORM_BASE;
  bool formPrimed = false;
  char name[12] = "";

  bool fainted() const { return hp == 0; }
  uint64_t moveUseCount(MoveId move) const {
    for (uint8_t i = 0; i < MOVE_SLOTS; i++)
      if (moves[i] == move) return moveUses[i];
    return 0;
  }
  bool recordMoveUse(MoveId move, bool knockout) {
    if (!moveTracksProgress(move)) return false;
    for (uint8_t i = 0; i < MOVE_SLOTS; i++) {
      if (moves[i] != move) continue;
      moveUses[i] = moveProgressAfterUse(move, moveUses[i], knockout);
      return true;
    }
    return false;
  }
};

// The move which actually reaches the resolver. Normal moves are copied from
// the content catalogue; Z and Max moves replace only the fields their rules
// change, leaving the source MoveId available for UI and LAN narration.
struct BattleMove {
  MoveId source = MOVE_NONE;
  MoveEntry entry = {};
  BattleMechanic mechanic = BMECH_NONE;
  GmaxMoveId gmaxMove = GMAX_MOVE_NONE;
  GmaxEffect gmaxEffect = GMAX_EFFECT_NONE;
  uint8_t abilityPowerPercent = 100;
  uint8_t levelPowerBonus = 0;

  bool valid() const { return source != MOVE_NONE; }
};

void combatantFromPet(Combatant &c, const Pet &p);
void combatantFromParty(Combatant &c, const PartyMon &m);
void battleInitializeForm(Combatant &combatant);
bool battleObservesMove(uint8_t bond, uint8_t roll);

// What one action did, so the UI can narrate it without recomputing anything.
struct TurnLog {
  MoveId move = 0;
  BattleMechanic mechanic = BMECH_NONE;
  GmaxMoveId gmaxMove = GMAX_MOVE_NONE;
  uint8_t moveType = T_NORMAL;
  uint16_t damage = 0;
  uint16_t effPct = 100;   // type effectiveness, percent
  uint8_t hits = 0;        // >1 for EF_MULTI
  uint8_t inflicted = AIL_NONE;
  int8_t stageDelta = 0;   // for EF_STAGE, so the UI can say "rose sharply"
  uint8_t stageMask = 0;
  bool missed = false;
  bool crit = false;
  bool immune = false;     // type chart says it does nothing
  bool skipped = false;    // asleep, frozen, fully paralysed or recharging
  bool hurtSelf = false;   // confusion
  bool charged = false;    // spent the turn winding up
  bool healed = false;
  bool targetFainted = false;
  bool blockedByField = false;
  bool dancerCopied = false;
  uint16_t counterDamage = 0;
  BattleForm formChanged = BFORM_BASE;
  BattleWeather weatherSet = BWEATHER_NONE;
  BattleTerrain terrainSet = BTERRAIN_NONE;
  BattleWeather weatherDamage = BWEATHER_NONE;
  BattleTerrain terrainHeal = BTERRAIN_NONE;
  BattleScreen screenSet = BSCREEN_NONE;
  BattleHazard hazardSet = BHAZARD_NONE;
  bool fieldCleared = false;
  uint8_t bonusRewardItems = 0;
  bool restoreLastItem = false;
  bool statsWeakened = false;
  bool moveUsed = false;  // actually began; misses count, prevented turns do not
  BattleSwitchRequest switchRequest = BSWITCH_NONE;
};

void battleSetEnvironment(BattleField &field, BattleWeather weather,
                          BattleTerrain terrain = BTERRAIN_NONE);
void battleSetWeather(BattleField &field, BattleWeather weather);
void battleSetTerrain(BattleField &field, BattleTerrain terrain);
bool battleGrounded(const Combatant &combatant,
                    const BattleField *field = nullptr);
bool battleGuaranteedEscape(const Combatant &combatant);
bool battleCanSwitch(const Combatant &combatant, const Combatant &opponent,
                     const BattleField *field = nullptr);

BattleMove battleMove(MoveId move);
BattleMove battleMoveFor(const Combatant &attacker, MoveId move,
                         BattleMechanic requested = BMECH_NONE);
bool battleMegaEligible(SpeciesId species,
                        MegaFormKind form = MEGA_FORM_NONE);
bool battleDynamaxEligible(SpeciesId species);
bool battleGigantamaxEligible(SpeciesId species);
bool battleMechanicAvailable(const BattleSideMechanics &side,
                             const Combatant &combatant,
                             BattleMechanic mechanic, MoveId move = MOVE_NONE,
                             MegaFormKind megaForm = MEGA_FORM_NONE);
bool battleActivateMechanic(BattleSideMechanics &side, Combatant &combatant,
                            BattleMechanic mechanic, MoveId move = MOVE_NONE,
                            MegaFormKind megaForm = MEGA_FORM_NONE);
void battleAfterAction(Combatant &combatant);
void battleOnSwitchOut(Combatant &combatant, Combatant *opponent = nullptr);
void battleRefreshForms(BattleField &field, Combatant &a, Combatant &b);
void battleOnEnter(Combatant &combatant, Combatant &opponent,
                   BattleField &field, uint8_t side, EntryLog &log);
BattleMechanic wildBattleMechanic(uint8_t eventRoll, uint8_t choiceRoll,
                                  bool hard, bool megaEligible,
                                  bool zEligible = true,
                                  bool dynamaxEligible = true);

uint16_t stagedStat(uint16_t base, int8_t stage);
uint16_t battleEffectiveStat(const Combatant &combatant, uint8_t statIndex);
uint8_t battleAccuracy(const Combatant &attacker, const Combatant &defender,
                       const BattleField &field, const BattleMove &move);
uint16_t battleDamage(const Combatant &atk, const Combatant &def,
                      const BattleField &field, MoveId mv, bool crit, uint8_t roll,
                      const BattleSideConditions *defendingSide = nullptr);
uint16_t battleDamage(const Combatant &atk, const Combatant &def,
                      const BattleField &field, const BattleMove &move,
                      bool crit, uint8_t roll,
                      const BattleSideConditions *defendingSide = nullptr);

// True if `a` using `ma` acts before `b` using `mb`. Priority first, then the
// staged speed, and paralysis halves it.
bool battleMovesFirst(const Combatant &a, MoveId ma,
                      const Combatant &b, MoveId mb);
bool battleMovesFirst(const Combatant &a, const BattleMove &ma,
                      const Combatant &b, const BattleMove &mb);
bool battleMovesFirst(const Combatant &a, const BattleMove &ma,
                      const Combatant &b, const BattleMove &mb,
                      const BattleField &field);

// One creature's action. `effectPercent` comes from the local answer: damaging
// moves scale their final damage, while 0 makes every move category fail.
void battleAct(Combatant &atk, Combatant &def, BattleField &field, MoveId mv,
               TurnLog &log, uint8_t effectPercent = 100,
               uint8_t attackerSide = 0);
void battleAct(Combatant &atk, Combatant &def, BattleField &field,
               const BattleMove &move, TurnLog &log,
               uint8_t effectPercent = 100, uint8_t attackerSide = 0);

// Applies both combatants' status/weather chip and terrain healing, then ticks
// temporary field layers once after both actions.
void battleEndRound(BattleField &field, Combatant &a, Combatant &b,
                    TurnLog &aLog, TurnLog &bLog, FieldLog &fieldLog);

// Picks the attacker's move. `smart` is what separates hard mode from easy:
// easy picks uniformly at random, hard reads the field, type chart, accuracy,
// whether a move kills this turn, and whether setup is worth its turn.
MoveId aiChooseMove(const Combatant &self, const Combatant &foe,
                    const BattleField &field, bool smart);
