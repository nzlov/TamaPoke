#include "items.h"
#include "battle.h"
#include "party.h"
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

bool itemCanApplyToPet(const ItemEntry &item, const Pet &target, MoveId move) {
  if (item.effect == ITEM_EFFECT_GIGANTAMAX_FACTOR)
    return !target.isEgg() && !target.gigantamaxFactor &&
           battleGigantamaxEligible(target.speciesId);
  if (item.effect == ITEM_EFFECT_TEACH_MOVE)
    return target.canLearnStone(move) && !target.knowsMove(move);
  TrainingStat stat = TRAINING_ATK;
  return itemTrainingStat(item, stat) && target.canRaiseTrainingFloor(stat);
}

bool itemApplyToPet(const ItemEntry &item, Pet &target, MoveId move) {
  if (item.effect == ITEM_EFFECT_GIGANTAMAX_FACTOR) {
    if (!itemCanApplyToPet(item, target, move)) return false;
    return target.giveGigantamaxFactor();
  }
  if (item.effect == ITEM_EFFECT_TEACH_MOVE)
    return itemCanApplyToPet(item, target, move) && target.teachMove(move);
  TrainingStat stat = TRAINING_ATK;
  return itemTrainingStat(item, stat) &&
         target.raiseTrainingFloor(stat, (uint8_t)item.param);
}

bool itemUsableOutsideBattle(const ItemEntry &item) {
  return item.effect == ITEM_EFFECT_TRAINING_FLOOR ||
         item.effect == ITEM_EFFECT_GIGANTAMAX_FACTOR ||
         item.effect == ITEM_EFFECT_TEACH_MOVE;
}

bool itemCanApplyToPartyMon(const ItemEntry &item, const PartyMon &target,
                            MoveId move) {
  if (target.empty() || target.isEgg()) return false;
  if (item.effect == ITEM_EFFECT_GIGANTAMAX_FACTOR)
    return !target.gigantamaxFactor() && battleGigantamaxEligible(target.dex);
  if (item.effect == ITEM_EFFECT_TEACH_MOVE)
    return speciesCanLearnMove(target.dex, move) &&
           !Pet::knowsLearnedMove(target.moves, target.reserveMoves, move);
  TrainingStat stat = TRAINING_ATK;
  if (!itemTrainingStat(item, stat)) return false;
  // GLUE: reserve cultivation members live as PartyMon records while the active
  // member uses Pet behavior. Delete this field mapping when both share one
  // mutable creature-state type.
  switch (stat) {
    case TRAINING_ATK: return target.trMinAtk < Pet::trMaxFor(target.ivAtk);
    case TRAINING_DEF: return target.trMinDef < Pet::trMaxFor(target.ivDef);
    case TRAINING_SPE: return target.trMinSpe < Pet::trMaxFor(target.ivSpe);
    default: return false;
  }
}

bool itemApplyToPartyMon(const ItemEntry &item, PartyMon &target, MoveId move) {
  if (!itemCanApplyToPartyMon(item, target, move)) return false;
  if (item.effect == ITEM_EFFECT_GIGANTAMAX_FACTOR) {
    target.setGigantamaxFactor(true);
    return true;
  }
  if (item.effect == ITEM_EFFECT_TEACH_MOVE)
    return Pet::placeInLearnedMoves(target.moves, target.reserveMoves, move,
                                    target.moveUses, target.reserveMoveUses);
  TrainingStat stat = TRAINING_ATK;
  if (!itemTrainingStat(item, stat)) return false;
  uint8_t *floor = nullptr, *training = nullptr, cap = 0;
  switch (stat) {
    case TRAINING_ATK:
      floor = &target.trMinAtk; training = &target.trAtk;
      cap = Pet::trMaxFor(target.ivAtk); break;
    case TRAINING_DEF:
      floor = &target.trMinDef; training = &target.trDef;
      cap = Pet::trMaxFor(target.ivDef); break;
    case TRAINING_SPE:
      floor = &target.trMinSpe; training = &target.trSpe;
      cap = Pet::trMaxFor(target.ivSpe); break;
    default: return false;
  }
  uint16_t next = (uint16_t)*floor + (uint16_t)item.param;
  *floor = next > cap ? cap : (uint8_t)next;
  if (*training < *floor) *training = *floor;
  return true;
}
