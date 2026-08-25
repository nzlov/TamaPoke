#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "moves.h"
#include "nature.h"

// The party: pets that finished their life and were kept, rather than being
// dissolved into a single Pokedex bit like every previous ending did.
//
// Only the two endings the player CHOOSES bank a pet -- farewell and release.
// A runaway does not: it is the game's one punishing outcome, and letting a
// neglected pet come back as a team member would take the sting out of it.
#define PARTY_SLOTS 6
// The box: storage beyond the six that fight, kept under a separate NVS key.
#define BOX_SLOTS 18
#define MOVE_SLOTS 4    // the same four every trainer gets in the real games

enum PartyStoreResult : uint8_t {
  PARTY_STORE_PARTY = 1,
  PARTY_STORE_BOX,
  PARTY_STORE_FULL,
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
  GYM_IV_REWARD_MAXED = 0xFF,
};

// A retired pet. Its level is frozen at the moment it joined; it does not keep
// ageing, and nothing about it can be trained any further.
struct PartyMon {
  int16_t dex = 0;      // Pokedex number, 0 = empty slot
  uint16_t level = 1;   // frozen at the moment it was banked
  uint16_t medals = 0;  // what it earned in life
  uint8_t ivAtk = 0, ivDef = 0, ivSpe = 0, ivHp = 0;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  uint8_t shiny = 0;
  char nick[12] = "";
  // Frozen with everything else: the moves you chose while it was alive are
  // what it fights with forever. 0 = empty slot (moveEntry(0) is the "-" filler).
  MoveId moves[MOVE_SLOTS] = { 0, 0, 0, 0 };
  uint8_t gymIvRewards[GYM_IV_REWARD_SLOTS] = { 0 };
  // Appended to preserve every previous field offset in the raw NVS record.
  NatureId nature = NATURE_UNKNOWN;

  bool empty() const { return dex < 1; }
};

class Party {
public:
  PartyMon slots[PARTY_SLOTS];
  PartyMon box[BOX_SLOTS];

  void begin();                 // load from NVS
  uint8_t count() const;
  bool isFull() const { return count() >= PARTY_SLOTS; }
  int firstFree() const;        // index of the first empty slot, -1 if full
  bool add(const PartyMon &m);  // into the first free slot; false if full
  PartyStoreResult store(const PartyMon &m);  // party first, then box
  void replaceAt(uint8_t i, const PartyMon &m);
  void releaseAt(uint8_t i);    // free a slot again
  void save();
  uint8_t boxCount() const;
  int boxFirstFree() const;
  bool boxAdd(const PartyMon &m);     // into the first free box slot
  void boxReleaseAt(uint8_t i);
  void boxSave();
  // Swaps a party slot with a box slot. Either may be empty, so this doubles as
  // deposit and withdraw rather than needing three separate operations.
  void swapPartyBox(uint8_t partyIdx, uint8_t boxIdx);

  // combat stats of a party member, same formula as the live pet's
  uint16_t atkOf(const PartyMon &m) const;
  uint16_t defOf(const PartyMon &m) const;
  uint16_t speOf(const PartyMon &m) const;
  uint16_t vitOf(const PartyMon &m) const;
  uint16_t spaOf(const PartyMon &m) const;
  uint16_t spdOf(const PartyMon &m) const;

private:
  Preferences prefs;
};

extern Party party;
