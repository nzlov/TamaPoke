#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include <cstdio>

uint32_t g_seed = 0xF04D;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static Combatant mon(AbilityKey ability, int16_t dex = 1) {
  Combatant c;
  c.dex = dex;
  c.level = 50;
  c.maxHp = c.hp = 200;
  for (uint8_t i = 0; i < SI_COUNT; i++) c.base[i] = 100;
  c.type1 = T_NORMAL;
  c.type2 = T_NONE;
  c.ability = ability;
  battleInitializeForm(c);
  return c;
}

static BattleMove move(uint8_t category, uint8_t type = T_NORMAL,
                       uint8_t power = 80, uint8_t flags = MF_NONE) {
  BattleMove result;
  result.source = 1;
  result.entry.cat = category;
  result.entry.type = type;
  result.entry.power = power;
  result.entry.acc = 0;
  result.entry.target = category == MC_STATUS ? TG_SELF : TG_FOE;
  result.entry.fieldFlags = flags;
  return result;
}

static MoveId findMove(const char *name) {
  for (MoveId id = 1; id < moveCount(); id++)
    if (!strcmp(moveEntry(id).name, name)) return id;
  return MOVE_NONE;
}

int main() {
  BattleField field;
  Combatant foe = mon(ABILITY_NONE);
  TurnLog log, foeLog;
  FieldLog fieldLog;

  Combatant castform = mon(ABILITY_FORECAST, 351);
  battleSetWeather(field, BWEATHER_RAIN);
  battleRefreshForms(field, castform, foe);
  ck(castform.form == BFORM_CASTFORM_RAIN && castform.type1 == T_WATER,
     "Forecast follows active weather and changes Castform's type");
  battleSetWeather(field, BWEATHER_NONE);
  battleRefreshForms(field, castform, foe);
  ck(castform.form == BFORM_BASE && castform.type1 == T_NORMAL,
     "Forecast restores the native form when weather ends");

  Combatant cherrim = mon(ABILITY_FLOWER_GIFT, 421);
  battleSetWeather(field, BWEATHER_SUN);
  battleRefreshForms(field, cherrim, foe);
  ck(cherrim.form == BFORM_CHERRIM_SUN &&
     battleEffectiveStat(cherrim, SI_ATK) == 150 &&
     battleEffectiveStat(cherrim, SI_SPD) == 150,
     "Flower Gift's sunny form boosts Attack and Special Defense");

  Combatant darmanitan = mon(ABILITY_ZEN_MODE, 555);
  darmanitan.hp = 100;
  battleRefreshForms(field, darmanitan, foe);
  ck(darmanitan.form == BFORM_DARMANITAN_ZEN &&
     darmanitan.base[SI_ATK] == 1 && darmanitan.base[SI_SPA] == 210 &&
     darmanitan.type2 == T_PSYCHIC,
     "Zen Mode applies its HP threshold, stat delta and Psychic typing");

  Combatant aegislash = mon(ABILITY_STANCE_CHANGE, 681);
  BattleMove strike = move(MC_PHYS);
  battleAct(aegislash, foe, field, strike, log);
  ck(aegislash.form == BFORM_AEGISLASH_BLADE &&
     aegislash.base[SI_ATK] == 190 && aegislash.base[SI_DEF] == 10,
     "Stance Change enters Blade Form before a damaging move");
  BattleMove shield = move(MC_STATUS, T_STEEL, 0, MF_STANCE_SHIELD);
  shield.entry.effect = EF_PROTECT;
  battleAct(aegislash, foe, field, shield, log);
  ck(aegislash.form == BFORM_BASE && aegislash.protectedTurn,
     "King's Shield restores Shield Form and protects the user");

  Combatant wishiwashi = mon(ABILITY_SCHOOLING, 746);
  battleRefreshForms(field, wishiwashi, foe);
  ck(wishiwashi.form == BFORM_WISHIWASHI_SCHOOL &&
     wishiwashi.base[SI_ATK] == 220,
     "Schooling forms above level 20 while HP is over one quarter");
  wishiwashi.hp = 50;
  battleRefreshForms(field, wishiwashi, foe);
  ck(wishiwashi.form == BFORM_BASE,
     "Schooling returns to Solo Form at one quarter HP");

  Combatant minior = mon(ABILITY_SHIELDS_DOWN, 774);
  BattleMove poison = move(MC_PHYS, T_NORMAL, 1);
  poison.entry.ailment = AIL_POISON;
  poison.entry.ailChance = 100;
  battleAct(foe, minior, field, poison, log);
  ck(minior.ailment == AIL_NONE,
     "Shields Down's Meteor Form blocks status ailments");
  minior.hp = 100;
  battleRefreshForms(field, minior, foe);
  battleAct(foe, minior, field, poison, log);
  ck(minior.form == BFORM_MINIOR_CORE && minior.ailment == AIL_POISON &&
     minior.base[SI_SPE] == 160,
     "Minior's Core Form changes stats and can receive ailments");

  Combatant mimikyu = mon(ABILITY_DISGUISE, 778);
  battleAct(foe, mimikyu, field, strike, log);
  ck(mimikyu.form == BFORM_MIMIKYU_BUSTED && mimikyu.hp == 175 &&
     log.damage == 0,
     "Disguise blocks the first hit and pays one eighth max HP");

  Combatant eiscue = mon(ABILITY_ICE_FACE, 875);
  battleAct(foe, eiscue, field, strike, log);
  ck(eiscue.form == BFORM_EISCUE_NOICE && log.damage == 0 &&
     eiscue.base[SI_SPE] == 180,
     "Ice Face blocks one physical hit and enters Noice Face");
  battleSetWeather(field, BWEATHER_SNOW);
  battleEndRound(field, eiscue, foe, log, foeLog, fieldLog);
  ck(eiscue.form == BFORM_EISCUE_ICE,
     "snow restores a broken Ice Face");

  Combatant morpeko = mon(ABILITY_HUNGER_SWITCH, 877);
  battleEndRound(field, morpeko, foe, log, foeLog, fieldLog);
  MoveId wheel = findMove("AURA WHEEL");
  BattleMove resolvedWheel = battleMoveFor(morpeko, wheel);
  ck(wheel && morpeko.form == BFORM_MORPEKO_HANGRY &&
     resolvedWheel.entry.type == T_DARK,
     "Hunger Switch alternates forms and makes Aura Wheel Dark-type");

  Combatant cramorant = mon(ABILITY_GULP_MISSILE, 845);
  BattleMove surf = move(MC_SPEC, T_WATER, 90, MF_GULP_MISSILE);
  battleAct(cramorant, foe, field, surf, log);
  ck(cramorant.form == BFORM_CRAMORANT_GULPING,
     "Surf loads Gulp Missile's prey above half HP");
  uint16_t attackerHp = foe.hp;
  battleAct(foe, cramorant, field, strike, log);
  ck(cramorant.form == BFORM_BASE && foe.hp + foe.maxHp / 4u == attackerHp &&
     foe.stage[SI_DEF] == -1,
     "Gulp Missile spits prey for quarter HP and lowers Defense");

  Combatant palafin = mon(ABILITY_ZERO_TO_HERO, 964);
  battleOnSwitchOut(palafin);
  EntryLog entryLog;
  battleOnEnter(palafin, foe, field, 0, entryLog);
  ck(palafin.form == BFORM_PALAFIN_HERO && palafin.base[SI_ATK] == 190,
     "Zero to Hero transforms after switching out and re-entering");

  std::printf("\n%s\n", bad ? "FORM TESTS FAILED" : "ALL FORM TESTS PASS");
  return bad ? 1 : 0;
}
