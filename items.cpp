#include "items.h"
#include "battle.h"
#include "pet.h"

static_assert(ITEM_STAT_ATK == (1u << SI_ATK) && ITEM_STAT_DEF == (1u << SI_DEF) &&
              ITEM_STAT_SPA == (1u << SI_SPA) && ITEM_STAT_SPD == (1u << SI_SPD) &&
              ITEM_STAT_SPE == (1u << SI_SPE),
              "item stat flags must match Combatant stage indices");

static bool itemTrainingStat(const ItemEntry &item, TrainingStat &stat) {
  if (item.effect != ITEM_EFFECT_TRAINING_FLOOR || item.param <= 0) return false;
  // GLUE: item packs use the five battle-stat flag positions while persistent
  // training has only three channels. This mapping disappears if those domains
  // ever adopt one shared stat type.
  switch (item.flags) {
    case ITEM_STAT_ATK: stat = TRAINING_ATK; return true;
    case ITEM_STAT_DEF: stat = TRAINING_DEF; return true;
    case ITEM_STAT_SPE: stat = TRAINING_SPE; return true;
    default: return false;
  }
}

bool itemCanApplyToCombatant(const ItemEntry &item, const Combatant &target) {
  switch (item.effect) {
    case ITEM_EFFECT_HEAL_HP:
      return item.param > 0 && !target.fainted() && target.hp < target.maxHp;
    case ITEM_EFFECT_CURE_STATUS:
      return !target.fainted() && (target.ailment != AIL_NONE || target.confuseTurns);
    case ITEM_EFFECT_REVIVE:
      return item.param > 0 && target.fainted();
    case ITEM_EFFECT_BATTLE_STAGE:
      if (item.param <= 0 || target.fainted() ||
          !(item.flags & (ITEM_STAT_ATK | ITEM_STAT_DEF | ITEM_STAT_SPA |
                          ITEM_STAT_SPD | ITEM_STAT_SPE))) return false;
      for (uint8_t i = 0; i < SI_COUNT; i++)
        if ((item.flags & (1u << i)) && target.stage[i] < 6) return true;
      return false;
    default:
      return false;
  }
}

bool itemApplyToCombatant(const ItemEntry &item, Combatant &target) {
  if (!itemCanApplyToCombatant(item, target)) return false;
  switch (item.effect) {
    case ITEM_EFFECT_HEAL_HP: {
      uint32_t healed = (uint32_t)target.hp + (uint16_t)item.param;
      target.hp = healed > target.maxHp ? target.maxHp : (uint16_t)healed;
      return true;
    }
    case ITEM_EFFECT_CURE_STATUS:
      target.ailment = AIL_NONE;
      target.ailTurns = 0;
      target.confuseTurns = 0;
      return true;
    case ITEM_EFFECT_REVIVE: {
      uint32_t hp = (uint32_t)target.maxHp * (uint16_t)item.param / 100U;
      target.hp = hp ? (uint16_t)hp : 1;
      target.ailment = AIL_NONE;
      target.ailTurns = 0;
      target.confuseTurns = 0;
      target.recharge = false;
      target.charging = MOVE_NONE;
      for (uint8_t i = 0; i < SI_COUNT; i++) target.stage[i] = 0;
      return true;
    }
    case ITEM_EFFECT_BATTLE_STAGE:
      for (uint8_t i = 0; i < SI_COUNT; i++) {
        if (!(item.flags & (1u << i))) continue;
        int next = target.stage[i] + item.param;
        target.stage[i] = next > 6 ? 6 : (int8_t)next;
      }
      return true;
    default:
      return false;
  }
}

bool itemCanApplyToPet(const ItemEntry &item, const Pet &target) {
  if (item.effect == ITEM_EFFECT_GIGANTAMAX_FACTOR)
    return !target.isEgg() && !target.gigantamaxFactor &&
           battleGigantamaxEligible(target.speciesId);
  TrainingStat stat = TRAINING_ATK;
  return itemTrainingStat(item, stat) && target.canRaiseTrainingFloor(stat);
}

bool itemApplyToPet(const ItemEntry &item, Pet &target) {
  if (item.effect == ITEM_EFFECT_GIGANTAMAX_FACTOR) {
    if (!itemCanApplyToPet(item, target)) return false;
    return target.giveGigantamaxFactor();
  }
  TrainingStat stat = TRAINING_ATK;
  return itemTrainingStat(item, stat) &&
         target.raiseTrainingFloor(stat, (uint8_t)item.param);
}
