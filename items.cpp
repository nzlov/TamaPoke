#include "items.h"
#include "battle.h"

bool itemCanApplyToCombatant(const ItemEntry &item, const Combatant &target) {
  switch (item.effect) {
    case ITEM_EFFECT_HEAL_HP:
      return item.param > 0 && !target.fainted() && target.hp < target.maxHp;
    case ITEM_EFFECT_CURE_STATUS:
      return !target.fainted() && (target.ailment != AIL_NONE || target.confuseTurns);
    case ITEM_EFFECT_REVIVE:
      return item.param > 0 && target.fainted();
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
    default:
      return false;
  }
}
