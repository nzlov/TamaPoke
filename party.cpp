#include "party.h"
#include "pet.h"
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

struct LegacyPartyMonV3 {
  LegacyPartyMonV1 mon;
  uint8_t gymIvRewards[GYM_IV_REWARD_SLOTS];
  NatureId nature;
};
static_assert(sizeof(LegacyPartyMonV3) == 164,
              "combat-only party layout must stay byte-exact");

constexpr uint16_t ROSTER_VERSION = 3;
constexpr uint16_t ROSTER_VERSION_WITH_SEPARATE_CLOCK = 2;
constexpr uint16_t ROSTER_VERSION_WITHOUT_DEATH = 1;
constexpr uint32_t ROSTER_SNAPSHOT_MAGIC = 0x33534B54UL;  // "TKS3"
static const char *const BOX_KEYS[BOX_SLOTS / BOX_PAGE_SLOTS] = {
  "box10", "box11", "box12", "box13"
};

struct RosterSnapshot {
  uint32_t magic;
  uint32_t seenEpoch;
  uint8_t active;
  uint8_t lead;
  uint8_t reserved[2];
  PartyMon slots[PARTY_SLOTS];
};
static_assert(sizeof(RosterSnapshot) <= 4096,
              "one atomic roster snapshot must fit one NVS blob");
static_assert(sizeof(RosterSnapshot) == 12 + sizeof(PartyMon) * PARTY_SLOTS,
              "roster snapshot header must stay byte-exact");

// GLUE: maps the common prefix of both previous raw NVS records into today's
// PartyMon. Remove only when saves made before natures are unsupported.
static void migrateMon(PartyMon &out, const LegacyPartyMonV1 &old) {
  out = PartyMon();
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
  out.stateVersion = 0;
}

template <size_t N>
static void loadLegacyMons(Preferences &prefs, const char *key, PartyMon (&out)[N]) {
  size_t stored = prefs.getBytesLength(key);
  if (stored == sizeof(LegacyPartyMonV3) * N) {
    LegacyPartyMonV3 old[N];
    prefs.getBytes(key, old, sizeof(old));
    for (size_t i = 0; i < N; i++) {
      migrateMon(out[i], old[i].mon);
      memcpy(out[i].gymIvRewards, old[i].gymIvRewards,
             sizeof(out[i].gymIvRewards));
      out[i].nature = old[i].nature;
    }
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

void Party::begin() {
  for (auto &s : slots) s = PartyMon();
  for (auto &s : box) s = PartyMon();
  prefs.begin("tamapoke", false);
  uint16_t rosterVersion = prefs.getUShort("rostv", 0);
  // GLUE: v1/v2 stored team, active slot and RTC baseline in separate keys.
  // Remove this branch when split roster saves are no longer supported.
  bool snapshotLoaded = rosterVersion == ROSTER_VERSION && loadSnapshot();
  savedSeenEpoch = snapshotLoaded ? savedSeenEpoch : prefs.getUInt("seen", 0);
  if (snapshotLoaded) {
    legacyMigration = false;
  } else if (rosterVersion == ROSTER_VERSION_WITH_SEPARATE_CLOCK ||
             rosterVersion == ROSTER_VERSION_WITHOUT_DEATH) {
    loadRoster();
    active = prefs.getUChar("active", 0);
    legacyMigration = false;
  } else {
    loadLegacyMons(prefs, "party", slots);
    PartyMon oldBox[18];
    loadLegacyMons(prefs, "box", oldBox);
    for (uint8_t i = 0; i < 18; i++) box[i] = oldBox[i];
    active = prefs.getUChar("active", 0);
    legacyMigration = true;
  }
  for (auto &s : slots) sanitize(s, false);
  for (auto &s : box) sanitize(s, true);
  if (active >= PARTY_SLOTS || slots[active].empty()) active = 0;
  normalizeLead();
  lastRosterTick = millis();
  if (!legacyMigration && rosterVersion != ROSTER_VERSION) {
    saveTeam();
    if (rosterVersion == ROSTER_VERSION_WITHOUT_DEATH) boxSave();
    prefs.putUShort("rostv", ROSTER_VERSION);
  }
}

template <size_t N>
static void loadRosterMons(Preferences &prefs, const char *key,
                           PartyMon (&out)[N]) {
  size_t stored = prefs.getBytesLength(key);
  if (stored == sizeof(out)) {
    prefs.getBytes(key, out, sizeof(out));
    return;
  }
  constexpr size_t OLD_MON_SIZE = sizeof(PartyMon) - sizeof(uint32_t);
  if (stored != OLD_MON_SIZE * N) return;
  uint8_t raw[OLD_MON_SIZE * N];
  prefs.getBytes(key, raw, sizeof(raw));
  for (size_t i = 0; i < N; i++)
    memcpy(&out[i], raw + i * OLD_MON_SIZE, OLD_MON_SIZE);
}

void Party::loadRoster() {
  loadRosterMons(prefs, "team1", slots);
  loadBox();
}

void Party::loadBox() {
  for (uint8_t page = 0; page < BOX_SLOTS / BOX_PAGE_SLOTS; page++) {
    PartyMon (&pageMons)[BOX_PAGE_SLOTS] =
        *reinterpret_cast<PartyMon (*)[BOX_PAGE_SLOTS]>(box + page * BOX_PAGE_SLOTS);
    loadRosterMons(prefs, BOX_KEYS[page], pageMons);
  }
}

bool Party::loadSnapshot() {
  if (prefs.getBytesLength("team2") != sizeof(RosterSnapshot)) return false;
  RosterSnapshot snapshot;
  if (prefs.getBytes("team2", &snapshot, sizeof(snapshot)) != sizeof(snapshot) ||
      snapshot.magic != ROSTER_SNAPSHOT_MAGIC) return false;
  memcpy(slots, snapshot.slots, sizeof(slots));
  active = snapshot.active;
  lead = snapshot.lead;
  savedSeenEpoch = snapshot.seenEpoch;
  loadBox();
  return true;
}

void Party::sanitize(PartyMon &m, bool boxed) {
  if (m.empty()) { m = PartyMon(); return; }
  if (m.dex < -1) { m = PartyMon(); return; }
  if (boxed && m.isEgg()) { m = PartyMon(); return; }
  // A save remains valid when its regional pack is temporarily absent. The
  // loaded catalogue is availability, not the persistence boundary.
  if (m.dex > CONTENT_MAX_SPECIES) { m = PartyMon(); return; }
  for (MoveId &move : m.moves) if (!moveValid(move)) move = MOVE_NONE;
  if (m.learnQCount > sizeof(m.learnQueue) / sizeof(m.learnQueue[0]))
    m.learnQCount = sizeof(m.learnQueue) / sizeof(m.learnQueue[0]);
  uint8_t queued = 0;
  for (uint8_t i = 0; i < m.learnQCount; i++)
    if (m.learnQueue[i] != MOVE_NONE && moveValid(m.learnQueue[i]))
      m.learnQueue[queued++] = m.learnQueue[i];
  for (uint8_t i = queued; i < sizeof(m.learnQueue) / sizeof(m.learnQueue[0]); i++)
    m.learnQueue[i] = MOVE_NONE;
  m.learnQCount = queued;
  for (uint8_t &reward : m.gymIvRewards)
    if (reward > GYM_IV_REWARD_HP &&
        reward != GYM_IV_REWARD_LEGACY_CLAIMED) reward = 0;
  if (m.dex > 0 && !natureValid(m.nature))
    m.nature = natureForLegacy(m.dex, m.ivAtk, m.ivDef, m.ivSpe, m.ivHp);
  m.state &= (PARTY_MON_DEAD | PARTY_MON_GIGANTAMAX_FACTOR |
              PARTY_MON_ABILITY_MASK);
  if (m.dex <= 0) m.state = 0;
  m.nick[sizeof(m.nick) - 1] = 0;
  uint8_t version = m.stateVersion;
  if (version != 1 && version != 2 && version != 3 && version != 4 && version != 5) {
    m.fullness = m.joy = m.energy = 80; m.hygiene = 100;
    m.ageMinutes = (uint32_t)(m.level ? m.level - 1 : 0) * MINUTES_PER_LEVEL;
    m.lastLearnLevel = (uint8_t)(m.level > MAX_LEVEL ? MAX_LEVEL : m.level);
    m.eggTarget = 1;
    version = 0;
  }
  if (version < 2)
    m.trMinAtk = m.trMinDef = m.trMinSpe = 0;
  if (version < 3) {
    // v2 used these exact bytes for early-retirement state. Existing pets were
    // all raised from eggs, so their age is valid cultivated time; future wild
    // captures explicitly start raisedMinutes at zero.
    m.sparkle = 0;
    m.raisedMinutes = m.ageMinutes;
  }
  // Old saves allowed color and sparkle to exist independently. Preserve any
  // rare creature, then mirror the combined state for backward compatibility.
  m.shiny = (m.shiny || m.sparkle) ? 1 : 0;
  m.sparkle = m.shiny;
  if (m.dex > 0 && (version < 4 || !genderValid(m.gender))) {
    m.gender = dexValid(m.dex)
        ? genderForLegacy(m.dex, dexEntry(m.dex).femaleRate,
                          m.ivAtk, m.ivDef, m.ivSpe, m.ivHp)
        : GENDER_UNKNOWN;
  }
  if (m.dex <= 0) m.gender = GENDER_UNKNOWN;
  if (m.dex > 0 && (version < 5 || !abilitySlotValid(m.abilitySlot()) ||
                    (dexValid(m.dex) && !speciesAbility(m.dex, m.abilitySlot())))) {
    AbilitySlot slot = dexValid(m.dex)
        ? abilitySlotForLegacy(m.dex, m.ivAtk, m.ivDef, m.ivSpe, m.ivHp)
        : ABILITY_SLOT_ONE;
    m.setAbilitySlot(slot);
  }
  if (m.dex <= 0) m.setAbilitySlot(ABILITY_SLOT_UNKNOWN);
  m.stateVersion = 5;
  auto sanitizeTraining = [](uint8_t &training, uint8_t &floor, uint8_t iv) {
    uint8_t cap = Pet::trMaxFor(iv);
    if (floor > cap) floor = cap;
    if (training > cap) training = cap;
    if (training < floor) training = floor;
  };
  sanitizeTraining(m.trAtk, m.trMinAtk, m.ivAtk);
  sanitizeTraining(m.trDef, m.trMinDef, m.ivDef);
  sanitizeTraining(m.trSpe, m.trMinSpe, m.ivSpe);
  if (m.fullness > 100) m.fullness = 100;
  if (m.joy > 100) m.joy = 100;
  if (m.energy > 100) m.energy = 100;
  if (m.hygiene > 100) m.hygiene = 100;
  if (m.weight > 100) m.weight = 100;
  if (m.dex > 0) {
    uint32_t level = 1 + m.ageMinutes / MINUTES_PER_LEVEL;
    m.level = (uint16_t)(level > MAX_LEVEL ? MAX_LEVEL : level);
  }
}

void Party::attach(Pet &pet) {
  if (legacyMigration) {
    PartyMon old[PARTY_SLOTS];
    memcpy(old, slots, sizeof(old));
    pet.exportState(slots[0]);
    pet.frozen = false;
    for (uint8_t i = 0; i < PARTY_SLOTS - 1; i++) slots[i + 1] = old[i];
    if (!old[PARTY_SLOTS - 1].empty()) {
      int free = boxFirstFree();
      if (free >= 0) box[free] = old[PARTY_SLOTS - 1];
    }
    active = 0;
    for (auto &s : slots) sanitize(s, false);
    for (auto &s : box) sanitize(s, true);
    saveTeam();
    boxSave();
    prefs.putUShort("rostv", ROSTER_VERSION);  // commit marker written last
    legacyMigration = false;
  } else {
    if (active >= PARTY_SLOTS || slots[active].empty()) {
      active = 0;
      while (active < PARTY_SLOTS && slots[active].empty()) active++;
      if (active >= PARTY_SLOTS) active = 0;
    }
    if (!slots[active].empty()) pet.importState(slots[active]);
  }
  normalizeLead();
  boundPet = &pet;
  pet.attachRoster(this);
  lastRosterTick = millis();
}

void Party::saveTeam() {
  if (boundPet && boundPet->lastSeenEpoch)
    savedSeenEpoch = boundPet->lastSeenEpoch;
  RosterSnapshot snapshot = {};
  snapshot.magic = ROSTER_SNAPSHOT_MAGIC;
  snapshot.seenEpoch = savedSeenEpoch;
  snapshot.active = active;
  snapshot.lead = lead;
  memcpy(snapshot.slots, slots, sizeof(slots));
  prefs.putBytes("team2", &snapshot, sizeof(snapshot));
}

void Party::saveBoxPage(uint8_t page) {
  if (page >= BOX_SLOTS / BOX_PAGE_SLOTS) return;
  prefs.putBytes(BOX_KEYS[page], box + page * BOX_PAGE_SLOTS,
                 sizeof(PartyMon) * BOX_PAGE_SLOTS);
}

void Party::save() { saveTeam(); }

void Party::boxSave() {
  for (uint8_t page = 0; page < BOX_SLOTS / BOX_PAGE_SLOTS; page++)
    saveBoxPage(page);
}

void Party::captureActive(const Pet &pet, bool persist) {
  if (active >= PARTY_SLOTS) return;
  pet.exportState(slots[active]);
  if (persist) saveTeam();
}

bool Party::activate(uint8_t index, Pet &pet) {
  if (index >= PARTY_SLOTS || index == active || slots[index].empty()) return false;
  captureActive(pet, false);
  active = index;
  pet.importState(slots[active]);
  saveTeam();
  return true;
}

bool Party::activateNext(int direction, Pet &pet) {
  if (direction == 0) return false;
  for (uint8_t step = 1; step < PARTY_SLOTS; step++) {
    int next = (int)active + (direction > 0 ? step : -step);
    while (next < 0) next += PARTY_SLOTS;
    next %= PARTY_SLOTS;
    if (!slots[next].empty()) return activate((uint8_t)next, pet);
  }
  return false;
}

bool Party::setLead(uint8_t index) {
  if (index >= PARTY_SLOTS || slots[index].empty() || index == lead) return false;
  lead = index;
  saveTeam();
  return true;
}

void Party::normalizeLead() {
  if (lead < PARTY_SLOTS && !slots[lead].empty()) return;
  lead = 0;
  while (lead < PARTY_SLOTS && slots[lead].empty()) lead++;
  if (lead >= PARTY_SLOTS) lead = 0;
}

void Party::update(Pet &pet, uint32_t nowMs) {
  while (nowMs - lastRosterTick >= PET_TICK_MS) {
    lastRosterTick += PET_TICK_MS;
    captureActive(pet, false);
    for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
      if (i == active || slots[i].empty()) continue;
      Pet background;
      background.copySharedFrom(pet);
      background.importState(slots[i]);
      background.advanceBackgroundMinute();
      background.exportState(slots[i]);
      pet.mergeSharedFrom(background);
    }
    if (++ticksSinceSave >= 5) { ticksSinceSave = 0; pendingSave = true; }
  }
}

void Party::syncClock(Pet &pet, uint32_t nowEpoch) {
  uint32_t seen = savedSeenEpoch;
  captureActive(pet, false);
  pet.syncClockFrom(nowEpoch, seen, false);
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
    if (i == active || slots[i].empty()) continue;
    Pet background;
    background.copySharedFrom(pet);
    background.importState(slots[i]);
    background.syncClockFrom(nowEpoch, seen, false);
    background.exportState(slots[i]);
    pet.mergeSharedFrom(background);
  }
  pet.lastSeenEpoch = nowEpoch;
  captureActive(pet, false);
  saveTeam();
  pet.saveNow();
}

void Party::flushSave(Pet &pet) {
  captureActive(pet, false);
  saveTeam();
  pendingSave = false;
  ticksSinceSave = 0;
}

void Party::saveSnapshot(Pet &pet, uint32_t nowEpoch) {
  if (nowEpoch) pet.lastSeenEpoch = nowEpoch;
  pet.saveNow();
  pendingSave = false;
  ticksSinceSave = 0;
}

uint8_t Party::boxCount() const {
  uint8_t n = 0;
  for (auto &s : box)
    if (!s.empty()) n++;
  return n;
}

bool Party::hasUnavailableSpecies() const {
  for (const auto &m : slots)
    if (m.dex > 0 && !dexValid(m.dex)) return true;
  for (const auto &m : box)
    if (m.dex > 0 && !dexValid(m.dex)) return true;
  return false;
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
  sanitize(box[i], true);
  if (box[i].empty()) return false;
  saveBoxPage((uint8_t)i / BOX_PAGE_SLOTS);
  return true;
}

void Party::boxReleaseAt(uint8_t i) {
  if (i >= BOX_SLOTS) return;
  box[i] = PartyMon();
  saveBoxPage(i / BOX_PAGE_SLOTS);
}

void Party::swapPartyBox(uint8_t partyIdx, uint8_t boxIdx) {
  if (partyIdx >= PARTY_SLOTS || boxIdx >= BOX_SLOTS) return;
  if (boundPet) captureActive(*boundPet, false);
  if (partyIdx == active && box[boxIdx].empty() && count() <= 1) return;
  PartyMon t = slots[partyIdx];
  slots[partyIdx] = box[boxIdx];
  box[boxIdx] = t;
  if (partyIdx == active && boundPet) {
    if (!slots[active].empty()) boundPet->importState(slots[active]);
    else {
      for (uint8_t i = 0; i < PARTY_SLOTS; i++)
        if (!slots[i].empty()) { active = i; boundPet->importState(slots[i]); break; }
    }
  }
  normalizeLead();
  saveTeam();
  saveBoxPage(boxIdx / BOX_PAGE_SLOTS);
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
  sanitize(slots[i], false);
  if (slots[i].empty()) return false;
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
  sanitize(slots[i], false);
  if (i == active && boundPet && !slots[i].empty()) boundPet->importState(slots[i]);
  normalizeLead();
  saveTeam();
}

void Party::releaseAt(uint8_t i) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = PartyMon();
  if (i == active && boundPet)
    for (uint8_t n = 0; n < PARTY_SLOTS; n++)
      if (!slots[n].empty()) { active = n; boundPet->importState(slots[n]); break; }
  normalizeLead();
  saveTeam();
}

void Party::setDeadAt(uint8_t i, bool dead) {
  if (i >= PARTY_SLOTS || slots[i].empty()) return;
  slots[i].setDead(dead);
  save();
}

void Party::rewardRandomTraining(uint8_t slotMask, Pet &pet, uint8_t points) {
  captureActive(pet, false);
  for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
    if (!(slotMask & (1u << i)) || !slots[i].battleReady()) continue;
    PartyMon &m = slots[i];
    uint8_t *training[] = { &m.trAtk, &m.trDef, &m.trSpe };
    const uint8_t caps[] = {
      Pet::trMaxFor(m.ivAtk), Pet::trMaxFor(m.ivDef), Pet::trMaxFor(m.ivSpe)
    };
    for (uint8_t point = 0; point < points; point++) {
      uint8_t available[3], count = 0;
      for (uint8_t stat = 0; stat < 3; stat++)
        if (*training[stat] < caps[stat]) available[count++] = stat;
      if (!count) break;
      (*training[available[random(count)]])++;
    }
  }
  if (slotMask & (1u << active)) {
    pet.trAtk = slots[active].trAtk;
    pet.trDef = slots[active].trDef;
    pet.trSpe = slots[active].trSpe;
  }
  saveTeam();
}

void Party::removeActiveAndEnsurePlayable(Pet &pet) {
  if (active < PARTY_SLOTS) slots[active] = PartyMon();
  normalizeLead();

  for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
    if (slots[i].empty()) continue;
    active = i;
    pet.importState(slots[i]);
    saveTeam();
    pet.saveNow();
    return;
  }

  for (uint8_t i = 0; i < BOX_SLOTS; i++) {
    if (box[i].empty()) continue;
    active = 0;
    slots[active] = box[i];
    box[i] = PartyMon();
    pet.importState(slots[active]);
    normalizeLead();
    saveTeam();
    saveBoxPage(i / BOX_PAGE_SLOTS);
    pet.saveNow();
    return;
  }

  active = 0;
  pet.newEgg();
  pet.exportState(slots[active]);
  normalizeLead();
  saveTeam();
}

// Mirrors calcStat() in pet.cpp: base + level + IV contribution + training.
// Kept in step with it by hand; there is no shared home for it that both the
// live pet and a frozen party member could use without dragging Pet in here.
static uint16_t calcStat(uint8_t base, uint8_t iv, uint16_t lvl, uint8_t tr,
                         NatureId nature, PetGender gender, NatureStat stat) {
  uint16_t untrained = (uint16_t)base + lvl + (uint32_t)iv * lvl / 100;
  return genderStatValue(gender, stat,
                         natureStatValue(nature, stat, untrained, tr));
}

uint16_t Party::atkOf(const PartyMon &m) const {
  return !m.battleReady() ? 0 : calcStat(dexEntry(m.dex).bAtk, m.ivAtk, m.level, m.trAtk,
                                  m.nature, m.gender, NATURE_STAT_ATK);
}
uint16_t Party::defOf(const PartyMon &m) const {
  return !m.battleReady() ? 0 : calcStat(dexEntry(m.dex).bDef, m.ivDef, m.level, m.trDef,
                                  m.nature, m.gender, NATURE_STAT_DEF);
}
uint16_t Party::speOf(const PartyMon &m) const {
  return !m.battleReady() ? 0 : calcStat(dexEntry(m.dex).bSpe, m.ivSpe, m.level, m.trSpe,
                                  m.nature, m.gender, NATURE_STAT_SPE);
}
uint16_t Party::vitOf(const PartyMon &m) const {
  return !m.battleReady() ? 0 : calcStat(dexEntry(m.dex).bHp, m.ivHp, m.level, 10,
                                  m.nature, m.gender, NATURE_STAT_NONE);
}
// Special reuses the physical IV and training, same rule as Pet::spaStat().
uint16_t Party::spaOf(const PartyMon &m) const {
  return !m.battleReady() ? 0 : calcStat(dexEntry(m.dex).bSpA, m.ivAtk, m.level, m.trAtk,
                                  m.nature, m.gender, NATURE_STAT_SPA);
}
uint16_t Party::spdOf(const PartyMon &m) const {
  return !m.battleReady() ? 0 : calcStat(dexEntry(m.dex).bSpD, m.ivDef, m.level, m.trDef,
                                  m.nature, m.gender, NATURE_STAT_SPD);
}
