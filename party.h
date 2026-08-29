#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>
#include "abilities.h"
#include "moves.h"
#include "nature.h"
#include "gender.h"
#include "player.h"

// The six cultivation slots. Every occupied slot keeps the complete durable
// state needed to become the pet shown on the main screen.
#define PARTY_SLOTS 6
// Four pages of six. Boxed creatures keep the same record shape but do not
// advance until they return to the cultivation team.
#define BOX_SLOTS 24
#define BOX_PAGE_SLOTS 6
#define MOVE_SLOTS 4
#define RESERVE_MOVE_SLOTS 4
#define LEARNED_MOVE_SLOTS (MOVE_SLOTS + RESERVE_MOVE_SLOTS)

class Pet;

enum PartyStoreResult : uint8_t {
  PARTY_STORE_PARTY = 1,
  PARTY_STORE_BOX,
  PARTY_STORE_FULL,
};

enum BreedingJobStatus : uint8_t {
  BREEDING_IDLE = 0,
  BREEDING_RUNNING,
  BREEDING_READY,
};

// One byte per real gym, stored with the creature. 0 means unclaimed; the
// other values say which IV that win raised, so the same array is both the
// claim record and the reward history shown by the gym screen.
constexpr uint8_t GYM_IV_GYMS_PER_REGION = 8;
constexpr size_t GYM_IV_REWARD_SLOTS =
    (size_t)CONTENT_MAX_REGIONS * GYM_IV_GYMS_PER_REGION;
enum : uint8_t {
  GYM_IV_REWARD_UNCLAIMED = 0,
  GYM_IV_REWARD_ATK = 1,
  GYM_IV_REWARD_DEF = 2,
  GYM_IV_REWARD_SPE = 3,
  GYM_IV_REWARD_HP = 4,
  // Compatibility marker written by builds that refused rewards at IV 31.
  // New rewards always choose a stat and increment it, so this is read-only.
  GYM_IV_REWARD_LEGACY_CLAIMED = 0xFF,
};

// One persistent creature. The original combat-only prefix stays byte-exact so
// saves from every released PartyMon layout can be migrated. The cultivation
// fields are appended and are shared by team and box; location, not shape,
// decides whether time advances.
enum PartyMonState : uint32_t {
  PARTY_MON_DEAD = 1u << 0,
  PARTY_MON_GIGANTAMAX_FACTOR = 1u << 1,
  PARTY_MON_ABILITY_SHIFT = 2,
  PARTY_MON_ABILITY_MASK = 3u << PARTY_MON_ABILITY_SHIFT,
};
struct PartyMon {
  int16_t dex = 0;      // 0 = empty, -1 = egg, positive = Pokedex number
  uint16_t level = 1;   // cached from ageMinutes for lists and combat
  uint16_t medals = 0;  // what it earned in life
  uint8_t ivAtk = 0, ivDef = 0, ivSpe = 0, ivHp = 0;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  uint8_t shiny = 0;  // combined rare state
  char nick[12] = "";
  // Moves travel with the creature through team and Box exchanges. 0 is an
  // empty move slot (moveEntry(0) is the "-" filler).
  MoveId moves[MOVE_SLOTS] = { 0, 0, 0, 0 };
  uint8_t gymIvRewards[GYM_IV_REWARD_SLOTS] = { 0 };
  // Appended to preserve every previous field offset in the raw NVS record.
  NatureId nature = NATURE_UNKNOWN;
  // Full cultivation state. v0 identifies a migrated combat-only record; v1
  // predates permanent training floors, v2 predates wild sparkle and
  // player-raised time, v3 predates persisted gender, v4 predates ability, and
  // v5 predates four reserve move slots.
  uint8_t stateVersion = 6;
  uint8_t fullness = 80, joy = 80, energy = 80, hygiene = 100;
  uint8_t poops = 0, weight = 0;
  uint8_t berryKnown = 0;
  uint8_t careMistakes = 0;
  uint8_t sleeping = 0, sleepAuto = 0;
  uint8_t lastEnd = 0;
  uint8_t bond = 0;
  uint32_t ageMinutes = 0;
  int16_t eggTarget = 1;
  uint8_t eggShiny = 0, eggTaps = 0;
  // Gender reuses the byte formerly reserved for removed early-retirement debt,
  // keeping every later field and the raw roster size byte-compatible.
  uint8_t starterPick = 0;
  PetGender gender = GENDER_UNKNOWN;
  // Legacy mirror retained at its original offset for raw-save compatibility.
  uint8_t sparkle = 0;
  uint8_t evoDeclinedLv = 0;
  uint32_t raisedMinutes = 0;
  uint8_t mistakeCooldown = 0, neglectTicks = 0, bondToday = 0;
  // These occupy alignment bytes from the v1 raw record, keeping its size and
  // every later field offset stable while making training floors per-creature.
  uint8_t trMinAtk = 0;
  // Existing 16-bit persistence slot: low byte is maintained minutes and high
  // byte is low-state minutes. Their sum is always in the range 0..59.
  uint16_t trainingTicks = 0;
  uint8_t lastLearnLevel = 0;
  uint8_t trMinDef = 0;
  // v6 reuses the first half of the former eight-entry pending-learn queue for
  // reserve moves. The tail and count byte remain at their old offsets so the
  // complete record and every later field stay byte-compatible.
  MoveId reserveMoves[RESERVE_MOVE_SLOTS] = { 0, 0, 0, 0 };
  MoveId legacyLearnQueueTail[4] = { 0, 0, 0, 0 };
  uint8_t legacyLearnQCount = 0;
  uint8_t trMinSpe = 0;
  int16_t eggByRegion[CONTENT_MAX_REGIONS + 1] = { 0 };
  // Appended so pre-death six-slot records remain a byte-exact prefix and can
  // be migrated without remapping cultivation or permanent training floors.
  uint32_t state = 0;

  bool empty() const { return dex == 0; }
  bool isEgg() const { return dex < 0; }
  bool battleReady() const { return dex > 0; }
  bool dead() const { return (state & PARTY_MON_DEAD) != 0; }
  bool gigantamaxFactor() const { return (state & PARTY_MON_GIGANTAMAX_FACTOR) != 0; }
  AbilitySlot abilitySlot() const {
    return (AbilitySlot)((state & PARTY_MON_ABILITY_MASK) >> PARTY_MON_ABILITY_SHIFT);
  }
  void setDead(bool value) {
    if (value) state |= PARTY_MON_DEAD;
    else state &= ~((uint32_t)PARTY_MON_DEAD);
  }
  void setGigantamaxFactor(bool value) {
    if (value) state |= PARTY_MON_GIGANTAMAX_FACTOR;
    else state &= ~((uint32_t)PARTY_MON_GIGANTAMAX_FACTOR);
  }
  void setAbilitySlot(AbilitySlot slot) {
    state = (state & ~((uint32_t)PARTY_MON_ABILITY_MASK)) |
            ((uint32_t)(abilitySlotValid(slot) ? slot : ABILITY_SLOT_UNKNOWN)
             << PARTY_MON_ABILITY_SHIFT);
  }
};
static_assert(offsetof(PartyMon, state) == 252,
              "the pre-death roster record must remain a byte-exact prefix");
static_assert(sizeof(PartyMon) == 256,
              "death state must remain the final roster field");
static_assert(sizeof(PartyMon) * PARTY_SLOTS <= 4096,
              "one roster page must stay within the backup/NVS blob bound");

struct BreedingCenterState {
  PartyMon parents[2];
  PartyMon offspring;
  uint32_t readyEpoch = 0;
  uint8_t status = BREEDING_IDLE;
  uint8_t reserved[3] = { 0, 0, 0 };
};

class Party {
public:
  PartyMon slots[PARTY_SLOTS];
  PartyMon box[BOX_SLOTS];
  BreedingCenterState breeding;

  void begin();                 // load roster or stage a legacy migration
  void attach(Pet &pet);        // bind the main-screen runtime and finish migration
  void update(Pet &pet, uint32_t nowMs);
  void syncClock(Pet &pet, uint32_t nowEpoch);
  bool activate(uint8_t index, Pet &pet);
  bool activateNext(int direction, Pet &pet);
  uint8_t activeIndex() const { return active; }
  bool setLead(uint8_t index);
  uint8_t leadIndex() const { return lead; }
  bool savePending() const { return pendingSave; }
  void flushSave(Pet &pet);
  void saveSnapshot(Pet &pet, uint32_t nowEpoch);
  void captureActive(const Pet &pet, bool persist = true);
  uint8_t count() const;
  bool hasUnavailableSpecies() const;
  bool isFull() const { return count() >= PARTY_SLOTS; }
  int firstFree() const;        // index of the first empty slot, -1 if full
  bool add(const PartyMon &m);  // into the first free slot; false if full
  PartyStoreResult store(const PartyMon &m);  // party first, then box
  void replaceAt(uint8_t i, const PartyMon &m);
  void releaseAt(uint8_t i);    // free a slot again
  void setDeadAt(uint8_t i, bool dead);
  bool retainObservedMove(uint8_t i, Pet &pet, MoveId move);
  // Randomly distributes current (decayable) training without raising floors.
  void rewardRandomTraining(uint8_t slotMask, Pet &pet, uint8_t points);
  // Removes the active member after farewell/release/runaway. Another team
  // member takes over, then the first Box member, and only an empty total
  // roster receives a safety egg.
  void removeActiveAndEnsurePlayable(Pet &pet);
  void save();
  uint8_t boxCount() const;
  int boxFirstFree() const;
  bool boxAdd(const PartyMon &m);     // into the first free box slot
  void boxReleaseAt(uint8_t i);
  void boxSave();
  // Swaps a party slot with a box slot. Either may be empty, so this doubles as
  // deposit and withdraw rather than needing three separate operations.
  void swapPartyBox(uint8_t partyIdx, uint8_t boxIdx);

  // Parents are physically moved into two durable nursery slots. Their state
  // is frozen there, and every mutation is refused while the job is running.
  bool breedingSwapParty(uint8_t parent, uint8_t partyIdx, Pet &pet);
  bool breedingSwapBox(uint8_t parent, uint8_t boxIdx, Pet &pet);
  PartyStoreResult breedingRemoveParent(uint8_t parent, Pet &pet);
  bool breedingStart(uint32_t nowEpoch);
  bool breedingUpdate(uint32_t nowEpoch, uint8_t wildRareBonus);
  PartyStoreResult breedingTakeOffspring(Pet &pet);

  // combat stats of a party member, same formula as the live pet's
  uint16_t atkOf(const PartyMon &m) const;
  uint16_t defOf(const PartyMon &m) const;
  uint16_t speOf(const PartyMon &m) const;
  uint16_t vitOf(const PartyMon &m) const;
  uint16_t spaOf(const PartyMon &m) const;
  uint16_t spdOf(const PartyMon &m) const;

private:
  Preferences prefs;
  Pet *boundPet = nullptr;
  uint8_t active = 0;
  uint8_t lead = 0;
  uint32_t lastRosterTick = 0;
  uint8_t ticksSinceSave = 0;
  bool pendingSave = false;
  bool legacyMigration = false;
  bool rosterUpgradePending = false;
  bool playerSnapshotLoaded = false;
  uint32_t savedSeenEpoch = 0;
  PlayerSnapshot savedPlayer;

  void saveTeam();
  void saveBoxPage(uint8_t page);
  void loadRoster();
  void loadBox();
  bool loadSnapshot();
  void normalizeLead();
  void sanitize(PartyMon &mon, bool boxed);
};

extern Party party;
