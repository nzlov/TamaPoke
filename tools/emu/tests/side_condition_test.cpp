#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include <cstdio>

uint32_t g_seed = 0x51DE;
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

static Combatant mon(uint8_t type = T_NORMAL) {
  Combatant c;
  c.level = 50;
  c.maxHp = c.hp = 200;
  for (uint8_t i = 0; i < SI_COUNT; i++) c.base[i] = 100;
  c.type1 = type;
  return c;
}

static BattleMove move(uint8_t category, uint8_t effect, int8_t param,
                       uint8_t target = TG_SELF) {
  BattleMove result;
  result.source = 1;
  result.entry.type = T_NORMAL;
  result.entry.cat = category;
  result.entry.power = category == MC_STATUS ? 0 : 80;
  result.entry.acc = 0;
  result.entry.effect = effect;
  result.entry.param = param;
  result.entry.target = target;
  return result;
}

int main() {
  Combatant a = mon(), b = mon();
  BattleField field;
  TurnLog log;
  BattleMove reflect = move(MC_STATUS, EF_SET_SCREEN, BSCREEN_REFLECT);
  battleAct(a, b, field, reflect, log, 100, 0);
  ck(field.sides[0].reflectTurns == BATTLE_FIELD_TURNS &&
     !field.sides[1].reflectTurns && log.screenSet == BSCREEN_REFLECT,
     "Reflect belongs to the user's side for five turns");

  BattleMove physical = move(MC_PHYS, EF_NONE, 0, TG_FOE);
  uint16_t plain = battleDamage(a, b, BattleField(), physical, false, 255);
  uint16_t screened = battleDamage(a, b, field, physical, false, 255,
                                   &field.sides[0]);
  uint16_t critical = battleDamage(a, b, field, physical, true, 255,
                                   &field.sides[0]);
  ck(screened < plain * 6 / 10 && critical == battleDamage(
         a, b, BattleField(), physical, true, 255),
     "Reflect halves physical damage while critical hits bypass it");

  BattleMove light = move(MC_STATUS, EF_SET_SCREEN, BSCREEN_LIGHT_SCREEN);
  battleAct(b, a, field, light, log, 100, 1);
  BattleMove special = move(MC_SPEC, EF_NONE, 0, TG_FOE);
  ck(battleDamage(a, b, field, special, false, 255, &field.sides[1]) <
     battleDamage(a, b, BattleField(), special, false, 255) * 6 / 10,
     "Light Screen independently halves special damage on the other side");

  BattleMove veil = move(MC_STATUS, EF_SET_SCREEN, BSCREEN_AURORA_VEIL);
  BattleField clear;
  battleAct(a, b, clear, veil, log, 100, 0);
  ck(!clear.sides[0].auroraVeilTurns,
     "Aurora Veil cannot be created without snow");
  battleSetEnvironment(clear, BWEATHER_SNOW);
  battleAct(a, b, clear, veil, log, 100, 0);
  ck(clear.sides[0].auroraVeilTurns == BATTLE_FIELD_TURNS,
     "Aurora Veil can be created during snow");

  BattleMove spikes = move(MC_STATUS, EF_SET_HAZARD, BHAZARD_SPIKES, TG_FOE);
  for (uint8_t i = 0; i < 5; i++) battleAct(a, b, field, spikes, log, 100, 0);
  ck(field.sides[1].spikesLayers == 3,
     "Spikes target the opposing side and cap at three layers");
  BattleMove toxic = move(MC_STATUS, EF_SET_HAZARD, BHAZARD_TOXIC_SPIKES, TG_FOE);
  for (uint8_t i = 0; i < 3; i++) battleAct(a, b, field, toxic, log, 100, 0);
  ck(field.sides[1].toxicSpikesLayers == 2,
     "Toxic Spikes cap at two layers");
  BattleMove rocks = move(MC_STATUS, EF_SET_HAZARD, BHAZARD_STEALTH_ROCK, TG_FOE);
  BattleMove web = move(MC_STATUS, EF_SET_HAZARD, BHAZARD_STICKY_WEB, TG_FOE);
  battleAct(a, b, field, rocks, log, 100, 0);
  battleAct(a, b, field, web, log, 100, 0);
  ck(field.sides[1].stealthRock && field.sides[1].stickyWeb,
     "Stealth Rock and Sticky Web persist independently");

  BattleField bounced;
  b.ability = ABILITY_MAGIC_BOUNCE;
  spikes.entry.tags = MT_REFLECTABLE;
  battleAct(a, b, bounced, spikes, log, 100, 0);
  ck(bounced.sides[0].spikesLayers == 1 && !bounced.sides[1].spikesLayers,
     "Magic Bounce reflects a reflectable hazard exactly once");

  BattleMove spin = move(MC_PHYS, EF_CLEAR_FIELD, BCLEAR_OWN_HAZARDS, TG_FOE);
  battleAct(a, b, bounced, spin, log, 100, 0);
  ck(!bounced.sides[0].spikesLayers && log.fieldCleared,
     "Rapid-Spin-style clearing removes hazards from the user's side");

  field.sides[0].spikesLayers = field.sides[1].spikesLayers = 2;
  BattleMove defog = move(MC_STATUS, EF_CLEAR_FIELD, BCLEAR_ALL, TG_FOE);
  battleAct(a, b, field, defog, log, 100, 0);
  ck(!field.sides[0].spikesLayers && !field.sides[1].spikesLayers &&
     !field.sides[0].reflectTurns && !field.sides[1].lightScreenTurns,
     "Defog-style clearing removes hazards and screens from both sides");

  FieldLog fieldLog;
  TurnLog aLog, bLog;
  field.sides[0].reflectTurns = field.sides[1].lightScreenTurns = 1;
  battleEndRound(field, a, b, aLog, bLog, fieldLog);
  ck(!field.sides[0].reflectTurns && !field.sides[1].lightScreenTurns &&
     fieldLog.screensExpired[0] == BSCREEN_REFLECT &&
     fieldLog.screensExpired[1] == BSCREEN_LIGHT_SCREEN,
     "side screens expire after their final completed round");

  BattleField entryField;
  entryField.sides[0].spikesLayers = 3;
  entryField.sides[0].stealthRock = true;
  entryField.sides[0].stickyWeb = true;
  Combatant entrant = mon(T_FIRE), opponent = mon();
  EntryLog entryLog;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(entrant.hp == 100 && entrant.stage[SI_SPE] == -1 &&
     entryLog.hazardDamage == 100 && entryLog.stageMask == ST_SPE,
     "entry applies three-layer Spikes, type-aware Stealth Rock and Sticky Web");

  entryField = BattleField();
  entryField.sides[0].toxicSpikesLayers = 2;
  entrant = mon(T_NORMAL);
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(entrant.ailment == AIL_POISON && entryLog.inflicted == AIL_POISON,
     "Toxic Spikes poison a grounded entrant");
  entrant = mon(T_POISON);
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(!entryField.sides[0].toxicSpikesLayers && entryLog.toxicSpikesAbsorbed,
     "a grounded Poison entrant absorbs Toxic Spikes");

  entryField = BattleField();
  entryField.sides[0].spikesLayers = 3;
  entryField.sides[0].stealthRock = true;
  entrant = mon(T_FLYING);
  uint16_t airborneHp = entrant.hp;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(entrant.hp < airborneHp && entryLog.hazardDamage == entrant.maxHp / 4,
     "airborne entrants avoid Spikes but still take Stealth Rock damage");
  entrant = mon();
  entrant.ability = ABILITY_EELEVATE;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(entrant.hp == entrant.maxHp,
     "Eelevate grants complete entry-hazard immunity");

  entryField = BattleField();
  entrant = mon();
  entrant.ability = ABILITY_DRIZZLE;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(entryField.weather == BWEATHER_RAIN && entryLog.weatherSet == BWEATHER_RAIN,
     "Drizzle starts rain through the shared entry lifecycle");
  entrant.ability = ABILITY_ELECTRIC_SURGE;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(entryField.terrain == BTERRAIN_ELECTRIC &&
     entryLog.terrainSet == BTERRAIN_ELECTRIC,
     "terrain surges use the shared entry lifecycle");

  entrant = mon();
  opponent = mon();
  entrant.ability = ABILITY_INTIMIDATE;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(opponent.stage[SI_ATK] == -1 && entryLog.stageMask == ST_ATK,
     "Intimidate lowers the opposing Attack on entry");
  entrant.ability = ABILITY_DOWNLOAD;
  opponent.base[SI_DEF] = 80;
  opponent.base[SI_SPD] = 120;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(entrant.stage[SI_ATK] == 1,
     "Download raises Attack against the lower physical defense");
  entrant = mon();
  entrant.ability = ABILITY_TRACE;
  opponent.ability = ABILITY_STAMINA;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(entrant.ability == ABILITY_STAMINA && entryLog.traced,
     "Trace copies the opposing ability on entry");

  entryField.sides[0].reflectTurns = entryField.sides[1].lightScreenTurns = 4;
  entrant.ability = ABILITY_SCREEN_CLEANER;
  battleOnEnter(entrant, opponent, entryField, 0, entryLog);
  ck(!entryField.sides[0].reflectTurns && !entryField.sides[1].lightScreenTurns &&
     entryLog.screensCleared,
     "Screen Cleaner removes screens from both sides on entry");

  Combatant trapped = mon(), trapper = mon();
  trapper.ability = ABILITY_SHADOW_TAG;
  ck(!battleCanSwitch(trapped, trapper),
     "Shadow Tag prevents an ordinary opponent from switching");
  trapped.type1 = T_GHOST;
  ck(battleCanSwitch(trapped, trapper),
     "Ghost types can escape Shadow Tag");
  trapped = mon();
  trapper.ability = ABILITY_ARENA_TRAP;
  ck(!battleCanSwitch(trapped, trapper),
     "Arena Trap prevents a grounded opponent from switching");
  trapped.type1 = T_FLYING;
  ck(battleCanSwitch(trapped, trapper),
     "airborne creatures escape Arena Trap");
  trapped = mon(T_STEEL);
  trapper.ability = ABILITY_MAGNET_PULL;
  ck(!battleCanSwitch(trapped, trapper),
     "Magnet Pull prevents a Steel opponent from switching");

  BattleMove roar = move(MC_STATUS, EF_FORCE_SWITCH, 0, TG_FOE);
  trapped = mon();
  trapper = mon();
  battleAct(trapper, trapped, field, roar, log);
  ck(log.switchRequest == BSWITCH_TARGET,
     "a phazing status move requests a target replacement");
  trapped.ability = ABILITY_SUCTION_CUPS;
  battleAct(trapper, trapped, field, roar, log);
  ck(log.switchRequest == BSWITCH_NONE && log.immune,
     "Suction Cups prevents forced switching");

  BattleMove pivot = move(MC_PHYS, EF_PIVOT, 0, TG_FOE);
  trapped = mon();
  battleAct(trapper, trapped, field, pivot, log);
  ck(log.damage && log.switchRequest == BSWITCH_USER,
     "a pivot attack requests a user replacement after dealing damage");

  trapped = mon();
  trapped.ability = ABILITY_EMERGENCY_EXIT;
  trapped.hp = 110;
  BattleMove threshold = move(MC_PHYS, EF_NONE, 0, TG_FOE);
  threshold.entry.power = 30;
  battleAct(trapper, trapped, field, threshold, log);
  ck(trapped.hp <= trapped.maxHp / 2 && log.switchRequest == BSWITCH_TARGET,
     "Emergency Exit requests a switch when damage crosses half HP");

  return bad ? 1 : 0;
}
