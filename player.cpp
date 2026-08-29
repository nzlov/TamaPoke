#include "player.h"
#include "avatars.h"
#include "party.h"
#include "pet.h"
#include "save.h"
#include "wild.h"
#include "perf.h"

PlayerProgress player;

void PlayerProgress::begin() {
  boundPet = nullptr;
  roster = nullptr;
  prefs.begin("tamapoke", false);
  uint16_t storedVersion = prefs.getUShort("savev", 0);
  if (storedVersion != SAVE_STATE_VERSION &&
      storedVersion != SAVE_STATE_VERSION_LEGACY) {
    if (prefs.isKey("init") || prefs.isKey("savev"))
      Serial.printf("save schema %u unsupported; resetting\n", storedVersion);
    prefs.clear();
    // Fresh and reset games bootstrap through the legacy scalar layout until
    // Party::attach can atomically create the first current roster snapshot.
    prefs.putUShort("savev", SAVE_STATE_VERSION_LEGACY);
    storedVersion = SAVE_STATE_VERSION_LEGACY;
  }
  restore(PlayerSnapshot());
  if (storedVersion == SAVE_STATE_VERSION_LEGACY) load();
}

void PlayerProgress::attach(Pet &pet, Party &party) {
  boundPet = &pet;
  roster = &party;
}

void PlayerProgress::saveKeys() {
  uint32_t started = perfNowUs();
  prefs.putString("tnam", trainerName);
  prefs.putUChar("avtr", avatar);
  prefs.putUChar("reg", region);
  prefs.putUShort("badg", badges);
  prefs.putUShort("badh", badgesHard);
  prefs.putBytes("badgX", badgesX, sizeof(badgesX));
  prefs.putBytes("badhX", badgesHardX, sizeof(badgesHardX));
  prefs.putBytes("dexreg", dexReg, sizeof(dexReg));
  prefs.putBytes("dexsh", dexShinyReg, sizeof(dexShinyReg));
  prefs.putUShort("strk", streak);
  prefs.putUShort("bstrk", bestStreak);
  prefs.putUChar("wrbon", wildRareBonus);
  prefs.putUInt("cday", lastCareDay);
  prefs.putUShort("tmedal", totalMedals);
  prefs.putUShort("mstone", lastMilestone);
  prefs.putUShort("ghi", gameHi);
  prefs.putUShort("shi", strHi);
  prefs.putUShort("qhi", spdHi);
  perfRecord(PERF_PLAYER_SAVE, perfNowUs() - started, 18);
}

void PlayerProgress::save() {
  if (boundPet && roster) {
    roster->captureActive(*boundPet, false);
    roster->save();
    return;
  }
  // This is only the pre-Party bootstrap/migration path. Normal firmware
  // runtime is attached and therefore writes the current snapshot above.
  saveKeys();
}

void PlayerProgress::load() {
  prefs.getBytes("dexreg", dexReg, sizeof(dexReg));
  prefs.getBytes("dexsh", dexShinyReg, sizeof(dexShinyReg));
  streak = prefs.getUShort("strk", 0);
  bestStreak = prefs.getUShort("bstrk", 0);
  wildRareBonus = prefs.getUChar("wrbon", 0);
  if (wildRareBonus > WILD_RARE_BONUS_MAX) wildRareBonus = WILD_RARE_BONUS_MAX;
  lastCareDay = prefs.getUInt("cday", 0);
  totalMedals = prefs.getUShort("tmedal", 0);
  lastMilestone = prefs.getUShort("mstone", 0);
  gameHi = prefs.getUShort("ghi", 0);
  strHi = prefs.getUShort("shi", 0);
  spdHi = prefs.getUShort("qhi", 0);
  avatar = prefs.getUChar("avtr", 0);
  if (avatar >= AVATAR_COUNT) avatar = 0;
  region = prefs.getUChar("reg", regionAll());
  if (region >= regionCount()) region = regionAll();
  badges = prefs.getUShort("badg", 0);
  badgesHard = prefs.getUShort("badh", 0);
  prefs.getBytes("badgX", badgesX, sizeof(badgesX));
  prefs.getBytes("badhX", badgesHardX, sizeof(badgesHardX));
  prefs.getString("tnam", trainerName, sizeof(trainerName));
}

PlayerSnapshot PlayerProgress::snapshot() const {
  PlayerSnapshot out;
  memcpy(out.dexReg, dexReg, sizeof(dexReg));
  memcpy(out.dexShinyReg, dexShinyReg, sizeof(dexShinyReg));
  out.streak = streak; out.bestStreak = bestStreak;
  out.wildRareBonus = wildRareBonus; out.avatar = avatar; out.region = region;
  out.lastCareDay = lastCareDay; out.totalMedals = totalMedals;
  out.lastMilestone = lastMilestone;
  out.gameHi = gameHi; out.strHi = strHi; out.spdHi = spdHi;
  out.badges = badges; out.badgesHard = badgesHard;
  memcpy(out.badgesX, badgesX, sizeof(badgesX));
  memcpy(out.badgesHardX, badgesHardX, sizeof(badgesHardX));
  memcpy(out.trainerName, trainerName, sizeof(trainerName));
  out.dailyTasks = dailyTasks;
  return out;
}

void PlayerProgress::restore(const PlayerSnapshot &in) {
  memcpy(dexReg, in.dexReg, sizeof(dexReg));
  memcpy(dexShinyReg, in.dexShinyReg, sizeof(dexShinyReg));
  streak = in.streak; bestStreak = in.bestStreak;
  wildRareBonus = in.wildRareBonus > WILD_RARE_BONUS_MAX
                      ? WILD_RARE_BONUS_MAX : in.wildRareBonus;
  avatar = in.avatar < AVATAR_COUNT ? in.avatar : 0;
  region = in.region < regionCount() ? in.region : regionAll();
  lastCareDay = in.lastCareDay; totalMedals = in.totalMedals;
  lastMilestone = in.lastMilestone;
  gameHi = in.gameHi; strHi = in.strHi; spdHi = in.spdHi;
  badges = in.badges; badgesHard = in.badgesHard;
  memcpy(badgesX, in.badgesX, sizeof(badgesX));
  memcpy(badgesHardX, in.badgesHardX, sizeof(badgesHardX));
  memcpy(trainerName, in.trainerName, sizeof(trainerName));
  trainerName[sizeof(trainerName) - 1] = 0;
  dailyTasks = in.dailyTasks;
  for (DailyTask &task : dailyTasks.entries) {
    if (task.species > CONTENT_MAX_SPECIES) task = DailyTask();
    task.completed = task.completed ? 1 : 0;
    task.reserved = 0;
  }
}

bool PlayerProgress::isRegistered(int16_t dex) const {
  return dex >= 1 && dex <= dexCount() &&
         (dexReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
}

bool PlayerProgress::isShinyRegistered(int16_t dex) const {
  return dex >= 1 && dex <= dexCount() &&
         (dexShinyReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
}

void PlayerProgress::registerSpecies(int16_t dex, bool color) {
  if (dex < 1 || dex > dexCount()) return;
  dexReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  if (color) dexShinyReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
}

uint16_t PlayerProgress::registeredCount() const {
  uint16_t count = 0;
  for (int16_t dex = 1; dex <= dexCount(); dex++)
    if (isRegistered(dex)) count++;
  return count;
}

uint16_t PlayerProgress::registeredCountIn(uint16_t lo, uint16_t hi) const {
  uint16_t count = 0;
  for (uint16_t dex = lo; dex <= hi && dex <= dexCount(); dex++)
    if (isRegistered(dex)) count++;
  return count;
}

static bool branchHasUnregistered(const PlayerProgress &progress,
                                  SpeciesId species, uint8_t depth) {
  if (!progress.isRegistered(species)) return true;
  if (depth >= CONTENT_MAX_EVOLUTIONS) return false;
  for (uint8_t i = 0; i < evolutionCount(species); i++) {
    SpeciesId target = evolutionTarget(species, i);
    if (dexValid(target) && branchHasUnregistered(progress, target, depth + 1))
      return true;
  }
  return false;
}

bool PlayerProgress::lineHasUnregistered(int16_t base) const {
  return dexValid(base) && branchHasUnregistered(*this, (SpeciesId)base, 0);
}

uint16_t PlayerProgress::badgeMask(uint8_t rg, bool hard) const {
  if (rg == 0) return hard ? badgesHard : badges;
  if (rg >= CONTENT_MAX_REGIONS) return 0;
  return hard ? badgesHardX[rg - 1] : badgesX[rg - 1];
}

bool PlayerProgress::hasBadge(uint8_t rg, uint8_t gym, bool hard) const {
  return (badgeMask(rg, hard) >> gym) & 1;
}

void PlayerProgress::winBadge(uint8_t rg, uint8_t gym, bool hard) {
  if (rg >= CONTENT_MAX_REGIONS) return;
  uint16_t bit = (uint16_t)1 << gym;
  if (rg == 0) {
    if (hard) badgesHard |= bit;
    else badges |= bit;
  } else if (hard) badgesHardX[rg - 1] |= bit;
  else badgesX[rg - 1] |= bit;
}

uint8_t PlayerProgress::badgeCountIn(uint8_t rg, bool hard) const {
  uint16_t value = badgeMask(rg, hard);
  uint8_t count = 0;
  while (value) { count += value & 1; value >>= 1; }
  return count;
}

uint8_t PlayerProgress::badgeCount(bool hard) const {
  uint8_t count = 0;
  for (uint8_t rg = 0; rg < regionAll(); rg++) count += badgeCountIn(rg, hard);
  return count;
}

void PlayerProgress::renameTrainer(const char *name) {
  strncpy(trainerName, name, sizeof(trainerName) - 1);
  trainerName[sizeof(trainerName) - 1] = 0;
  save();
}

const char *PlayerProgress::regionName() const {
  uint8_t count = regionCount();
  return count ? ::regionName(region < count ? region : regionAll()) : "?";
}

bool PlayerProgress::refreshDailyTasks(uint32_t day) {
  if (!day || dailyTasks.day == day) return false;
  uint16_t legendaryRegionMask = 0;
  for (uint8_t region = 0; region < regionAll() && region < 16; region++) {
    const RegionBattleInfo &battle = regionBattleInfo(region);
    if (battle.trainerCount &&
        hasBadge(region, battle.trainerCount - 1, true))
      legendaryRegionMask |= (uint16_t)1 << region;
  }
  if (!dailyTasksRefresh(dailyTasks, day, legendaryRegionMask,
                         (uint32_t)random(0x7FFFFFFF)))
    return false;
  save();
  return true;
}
