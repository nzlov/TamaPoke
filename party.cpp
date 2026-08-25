#include "party.h"
#include "dex.h"

Party party;

struct LegacyPartyMonV1 {
  int16_t dex;
  uint16_t level;
  uint16_t medals;
  uint8_t ivAtk, ivDef, ivSpe, ivHp;
  uint8_t trAtk, trDef, trSpe;
  uint8_t shiny;
  char nick[12];
  MoveId moves[MOVE_SLOTS];
};
static_assert(sizeof(LegacyPartyMonV1) == 34,
              "legacy party layout must stay byte-exact");

struct LegacyPartyMonV2 {
  LegacyPartyMonV1 mon;
  uint8_t gymIvRewards[GYM_IV_REWARD_SLOTS];
};
static_assert(sizeof(LegacyPartyMonV2) == 162,
              "pre-nature party layout must stay byte-exact");

// GLUE: maps the common prefix of both previous raw NVS records into today's
// PartyMon. Remove only when saves made before natures are unsupported.
static void migrateMon(PartyMon &out, const LegacyPartyMonV1 &old) {
  out.dex = old.dex;
  out.level = old.level;
  out.medals = old.medals;
  out.ivAtk = old.ivAtk; out.ivDef = old.ivDef;
  out.ivSpe = old.ivSpe; out.ivHp = old.ivHp;
  out.trAtk = old.trAtk; out.trDef = old.trDef; out.trSpe = old.trSpe;
  out.shiny = old.shiny;
  memcpy(out.nick, old.nick, sizeof(out.nick));
  memcpy(out.moves, old.moves, sizeof(out.moves));
  out.nature = natureForLegacy(out.dex, out.ivAtk, out.ivDef, out.ivSpe, out.ivHp);
}

template <size_t N>
static void loadMons(Preferences &prefs, const char *key, PartyMon (&out)[N]) {
  size_t stored = prefs.getBytesLength(key);
  if (stored == sizeof(out)) {
    prefs.getBytes(key, out, sizeof(out));
    return;
  }
  if (stored == sizeof(LegacyPartyMonV2) * N) {
    LegacyPartyMonV2 old[N];
    prefs.getBytes(key, old, sizeof(old));
    for (size_t i = 0; i < N; i++) {
      migrateMon(out[i], old[i].mon);
      memcpy(out[i].gymIvRewards, old[i].gymIvRewards,
             sizeof(out[i].gymIvRewards));
    }
    return;
  }
  if (stored != sizeof(LegacyPartyMonV1) * N) return;
  LegacyPartyMonV1 old[N];
  prefs.getBytes(key, old, sizeof(old));
  for (size_t i = 0; i < N; i++) migrateMon(out[i], old[i]);
}

// Same NVS namespace as the pet on purpose: WIPE (Pet::factoryReset) calls
// clear() on it, and a factory reset that left the party behind would be a lie.
void Party::begin() {
  // Start from empty: getBytes() leaves the destination untouched when the key
  // is missing, so without this a reload after a wipe would keep showing the
  // old party out of RAM.
  for (auto &s : slots) s = PartyMon();
  prefs.begin("tamapoke", false);
  loadMons(prefs, "party", slots);
  for (auto &s : slots) {
    if (s.dex < 1 || s.dex > dexCount()) s.dex = 0;
    for (MoveId &move : s.moves) if (!moveValid(move)) move = MOVE_NONE;
    for (uint8_t &reward : s.gymIvRewards)
      if (reward > GYM_IV_REWARD_HP && reward != GYM_IV_REWARD_MAXED) reward = 0;
    if (!natureValid(s.nature))
      s.nature = natureForLegacy(s.dex, s.ivAtk, s.ivDef, s.ivSpe, s.ivHp);
    s.nick[sizeof(s.nick) - 1] = 0;
  }
  for (auto &s : box) s = PartyMon();
  loadMons(prefs, "box", box);
  for (auto &s : box) {
    if (s.dex < 1 || s.dex > dexCount()) s.dex = 0;
    for (MoveId &move : s.moves) if (!moveValid(move)) move = MOVE_NONE;
    for (uint8_t &reward : s.gymIvRewards)
      if (reward > GYM_IV_REWARD_HP && reward != GYM_IV_REWARD_MAXED) reward = 0;
    if (!natureValid(s.nature))
      s.nature = natureForLegacy(s.dex, s.ivAtk, s.ivDef, s.ivSpe, s.ivHp);
    s.nick[sizeof(s.nick) - 1] = 0;
  }
}

void Party::save() {
  prefs.putBytes("party", slots, sizeof(slots));
}

void Party::boxSave() {
  prefs.putBytes("box", box, sizeof(box));
}

uint8_t Party::boxCount() const {
  uint8_t n = 0;
  for (auto &s : box)
    if (!s.empty()) n++;
  return n;
}

int Party::boxFirstFree() const {
  for (int i = 0; i < BOX_SLOTS; i++)
    if (box[i].empty()) return i;
  return -1;
}

bool Party::boxAdd(const PartyMon &m) {
  int i = boxFirstFree();
  if (i < 0) return false;
  box[i] = m;
  boxSave();
  return true;
}

void Party::boxReleaseAt(uint8_t i) {
  if (i >= BOX_SLOTS) return;
  box[i] = PartyMon();
  boxSave();
}

void Party::swapPartyBox(uint8_t partyIdx, uint8_t boxIdx) {
  if (partyIdx >= PARTY_SLOTS || boxIdx >= BOX_SLOTS) return;
  PartyMon t = slots[partyIdx];
  slots[partyIdx] = box[boxIdx];
  box[boxIdx] = t;
  save();
  boxSave();
}

uint8_t Party::count() const {
  uint8_t n = 0;
  for (auto &s : slots)
    if (!s.empty()) n++;
  return n;
}

int Party::firstFree() const {
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (slots[i].empty()) return i;
  return -1;
}

bool Party::add(const PartyMon &m) {
  int i = firstFree();
  if (i < 0) return false;
  slots[i] = m;
  save();
  return true;
}

PartyStoreResult Party::store(const PartyMon &m) {
  if (add(m)) return PARTY_STORE_PARTY;
  if (boxAdd(m)) return PARTY_STORE_BOX;
  return PARTY_STORE_FULL;
}

void Party::replaceAt(uint8_t i, const PartyMon &m) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = m;
  save();
}

void Party::releaseAt(uint8_t i) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = PartyMon();
  save();
}

// Mirrors calcStat() in pet.cpp: base + level + IV contribution + training.
// Kept in step with it by hand; there is no shared home for it that both the
// live pet and a frozen party member could use without dragging Pet in here.
static uint16_t calcStat(uint8_t base, uint8_t iv, uint16_t lvl, uint8_t tr,
                         NatureId nature, NatureStat stat) {
  uint16_t untrained = (uint16_t)base + lvl + (uint32_t)iv * lvl / 100;
  return natureStatValue(nature, stat, untrained, tr);
}

uint16_t Party::atkOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(dexEntry(m.dex).bAtk, m.ivAtk, m.level, m.trAtk,
                                  m.nature, NATURE_STAT_ATK);
}
uint16_t Party::defOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(dexEntry(m.dex).bDef, m.ivDef, m.level, m.trDef,
                                  m.nature, NATURE_STAT_DEF);
}
uint16_t Party::speOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(dexEntry(m.dex).bSpe, m.ivSpe, m.level, m.trSpe,
                                  m.nature, NATURE_STAT_SPE);
}
uint16_t Party::vitOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(dexEntry(m.dex).bHp, m.ivHp, m.level, 10,
                                  m.nature, NATURE_STAT_NONE);
}
// Special reuses the physical IV and training, same rule as Pet::spaStat().
uint16_t Party::spaOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(dexEntry(m.dex).bSpA, m.ivAtk, m.level, m.trAtk,
                                  m.nature, NATURE_STAT_SPA);
}
uint16_t Party::spdOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(dexEntry(m.dex).bSpD, m.ivDef, m.level, m.trDef,
                                  m.nature, NATURE_STAT_SPD);
}
