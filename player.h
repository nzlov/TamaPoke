#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "dex.h"
#include "daily_tasks.h"
#include "trainers.h"

class Pet;
class Party;

struct PlayerSnapshot {
  uint8_t dexReg[(CONTENT_MAX_SPECIES + 7) / 8] = { 0 };
  uint8_t dexShinyReg[(CONTENT_MAX_SPECIES + 7) / 8] = { 0 };
  uint16_t streak = 0;
  uint16_t bestStreak = 0;
  uint8_t wildRareBonus = 0;
  uint8_t avatar = 0;
  uint8_t region = 0xFF;
  uint8_t reserved = 0;
  uint32_t lastCareDay = 0;
  uint16_t totalMedals = 0;
  uint16_t lastMilestone = 0;
  uint16_t gameHi = 0;
  uint16_t strHi = 0;
  uint16_t spdHi = 0;
  uint16_t badges = 0;
  uint16_t badgesHard = 0;
  uint16_t badgesX[CONTENT_MAX_REGIONS - 1] = { 0 };
  uint16_t badgesHardX[CONTENT_MAX_REGIONS - 1] = { 0 };
  char trainerName[12] = "";
  DailyTaskState dailyTasks;
};
static_assert(sizeof(PlayerSnapshot) == 612 + sizeof(DailyTaskState),
              "player snapshot must stay byte-exact for roster v6");

class PlayerProgress {
public:
  uint8_t dexReg[(CONTENT_MAX_SPECIES + 7) / 8] = { 0 };
  uint8_t dexShinyReg[(CONTENT_MAX_SPECIES + 7) / 8] = { 0 };
  uint16_t streak = 0, bestStreak = 0;
  uint8_t wildRareBonus = 0;
  uint32_t lastCareDay = 0;
  uint16_t totalMedals = 0;
  uint16_t lastMilestone = 0;
  uint16_t gameHi = 0, strHi = 0, spdHi = 0;
  char trainerName[12] = "";
  uint8_t region = 0xFF;
  uint8_t avatar = 0;
  uint16_t badges = 0;
  uint16_t badgesHard = 0;
  uint16_t badgesX[CONTENT_MAX_REGIONS - 1] = { 0 };
  uint16_t badgesHardX[CONTENT_MAX_REGIONS - 1] = { 0 };
  DailyTaskState dailyTasks;

  void begin();
  void attach(Pet &pet, Party &party);
  void save();
  PlayerSnapshot snapshot() const;
  void restore(const PlayerSnapshot &snapshot);

  bool isRegistered(int16_t dex) const;
  bool isShinyRegistered(int16_t dex) const;
  void registerSpecies(int16_t dex, bool color);
  uint16_t registeredCount() const;
  uint16_t registeredCountIn(uint16_t lo, uint16_t hi) const;
  bool lineHasUnregistered(int16_t base) const;

  uint16_t badgeMask(uint8_t region, bool hard) const;
  bool hasBadge(uint8_t region, uint8_t gym, bool hard) const;
  void winBadge(uint8_t region, uint8_t gym, bool hard);
  uint8_t badgeCountIn(uint8_t region, bool hard) const;
  uint8_t badgeCount(bool hard) const;

  void renameTrainer(const char *name);
  const char *regionName() const;
  bool refreshDailyTasks(uint32_t day);

private:
  Preferences prefs;
  Pet *boundPet = nullptr;
  Party *roster = nullptr;

  void load();
  void saveKeys();
};

extern PlayerProgress player;
