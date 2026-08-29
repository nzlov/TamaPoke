#include "pet.h"
#include "dex.h"
#include "moves.h"
#include "audio.h"
#include "party.h"
#include "save.h"
#include "wild.h"
#include "perf.h"

void Pet::begin() {
  progress.begin();
  prefs.begin("tamapoke", false);
  // Zeroed BEFORE the branch below, not inside load(): getBytes() leaves its
  // destination untouched when the key is missing, and the fresh-install path
  // returns without ever calling load(). Without this a begin() after a factory
  // reset keeps the old Pokedex alive in RAM -- the firmware reboots on WIPE so
  // it never showed there, but anything calling begin() twice would see it, and
  // Party::begin() already guards the same way for the same reason.
  memset(gymIvRewards, 0, sizeof(gymIvRewards));
  for (int i = 0; i < regionCount(); i++) eggByRegion[i] = 0;
  if (prefs.getUShort("savev", 0) == SAVE_STATE_VERSION) {
    // Current saves are loaded exclusively by Party::attach from team2.
    // The temporary Pet state is never rendered before that attachment.
    speciesId = -1;
    pendingSave = false;
    ticksSinceSave = 0;
  } else if (!prefs.getBool("init", false)) {
    prefs.putBool("init", true);
    newEgg();
  } else {
    load();
  }
  lastTick = millis();
}

void Pet::newEgg() {
  ceremony = CER_NONE;
  dead = false;
  neglectTicks = 0;
  weight = 0;
  speciesId = -1;
  prevSpeciesId = -1;
  nature = NATURE_UNKNOWN;
  gender = GENDER_UNKNOWN;
  for (int i = 0; i < regionCount(); i++) eggByRegion[i] = 0;
  eggTarget = pickEggSpecies();  // especie oculta segun rareza y pokedex
  eggByRegion[progress.region % regionCount()] = eggTarget;
  starterPick = (progress.registeredCount() == 0);  // primera partida: el jugador elige inicial
  // The combined rare state belongs to wild encounters. A safety egg is
  // deliberately neutral rather than another route into the wild economy.
  shiny = false;
  gigantamaxFactor = false;
  eggShiny = false;
  eggTaps = 0;
  fullness = 80;
  joy = 80;
  energy = 80;
  hygiene = 100;
  poops = 0;
  ageMinutes = 0;
  raisedMinutes = 0;
  careMistakes = 0;
  mistakeCooldown = 0;
  sleeping = false;
  frozen = false;
  memset(gymIvRewards, 0, sizeof(gymIvRewards));
  save();
}

// progresion offline: el tiempo paso aunque estuviera apagado, pero con
// piedad — las barras bajan con suelo en 15 (vuelve hambriento, no muerto),
// sin descuidos ni escapadas en ausencia
static uint8_t dropTo(uint8_t v, uint8_t d, uint8_t fl) {
  if (v <= fl) return v;
  return (v - fl > d) ? v - d : fl;
}

void Pet::advanceAgeMinute() {
  if (frozen) return;
  ageMinutes++;
  if (!isEgg()) raisedMinutes++;
}

void Pet::setClock(uint32_t nowEpoch) {
  lastSeenEpoch = nowEpoch;
  if (nowEpoch) save();  // persiste ya: un corte de luz no pierde la referencia
}

void Pet::syncClock(uint32_t nowEpoch) {
  uint32_t seen = prefs.getUInt("seen", 0);
  syncClockFrom(nowEpoch, seen, true);
}

void Pet::syncClockFrom(uint32_t nowEpoch, uint32_t seenEpoch, bool persist) {
  bool wasBackground = backgroundMode;
  if (!persist) backgroundMode = true;
  lastSeenEpoch = nowEpoch;
  if (nowEpoch == 0) { backgroundMode = wasBackground; return; }
  uint32_t elapsed = (seenEpoch && nowEpoch > seenEpoch) ? nowEpoch - seenEpoch : 0;
  constexpr uint32_t MAX_OFFLINE_SECONDS = 14UL * 24 * 60 * 60;
  if (elapsed > MAX_OFFLINE_SECONDS) {
    Serial.printf("RTC jump ignored: %u seconds\n", elapsed);
    if (persist) save();
    backgroundMode = wasBackground;
    return;
  }
  uint32_t mins = elapsed / 60;
  if (mins < 2 || ceremony != CER_NONE || starterPick || dead) {
    if (persist) save();  // primera vez, sin tiempo que aplicar o aun eligiendo inicial
    backgroundMode = wasBackground;
    return;
  }
  for (uint32_t i = 0; i < mins; i++) {
    advanceAgeMinute();
    if (isEgg()) {
      if (ageMinutes >= 3) hatch();  // eclosiona en tu ausencia
      continue;
    }
    if (sleeping) {  // descanso: baja lento y con suelo, igual que en vivo
      energy = clamp100(energy + 6);
      if (ageMinutes % 2 == 0) {
        fullness = dropTo(fullness, 1, 30);
        joy = dropTo(joy, 1, 35);
      }
      if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
      trainingTick(true);
      continue;
    }
    fullness = dropTo(fullness, 2, 15);
    energy = dropTo(energy, 1, 15);
    hygiene = dropTo(hygiene, 1, 15);
    joy = dropTo(joy, 1, 15);
    trainingTick(false);
  }
  if (!isEgg()) {
    if (!sleeping) {  // durmiendo no ensucia
      uint8_t p = poops + mins / 240;
      poops = p > 3 ? 3 : p;
    }
    // la evolucion NO se aplica offline: queda lista y la dispara el usuario
    // tocando al bicho cuando vuelve (para que vea la transformacion)
  }
  Serial.printf("offline: %u min aplicados (nv.%u)\n", mins, level());
  if (persist) save();
  backgroundMode = wasBackground;
}

void Pet::update(uint32_t nowMs) {
  // Finish the lifecycle at the roster boundary. Normal endings never create
  // an egg; Party only does that when both cultivation and Box are empty.
  if (ceremony != CER_NONE && millis() > ceremonyUntil) {
    uint8_t ending = ceremony;
    if (ending == CER_FAREWELL) {
      uint8_t gain = level() >= 100 ? 2 : 1;
      uint16_t next = (uint16_t)progress.wildRareBonus + gain;
      progress.wildRareBonus = next > WILD_RARE_BONUS_MAX ? WILD_RARE_BONUS_MAX : next;
    } else if (ending == CER_RUNAWAY) {
      progress.wildRareBonus = progress.wildRareBonus > 2
                                   ? (uint8_t)(progress.wildRareBonus - 2) : 0;
    }
    lastEnd = ending;
    ceremony = CER_NONE;
    if (roster) roster->removeActiveAndEnsurePlayable(*this);
    else newEgg();
    return;
  }
  while (nowMs - lastTick >= PET_TICK_MS) {
    lastTick += PET_TICK_MS;
    tick();
  }
}

void Pet::tick() {
  if (ceremony != CER_NONE || dead) return;  // time stops after leaving or death
  if (starterPick) return;  // la partida no empieza hasta elegir inicial: si el
                            // tiempo corriera aqui, el huevo eclosionaria solo a
                            // los 3 min con la especie sorteada y se perderia la
                            // eleccion del jugador
  advanceAgeMinute();   // a revived companion does not age or lose training

  if (isEgg()) {
    if (ageMinutes >= 3) hatch();  // si no lo tocas, eclosiona solo a los 3 min
    return;
  }

  applyAutoSleep();   // put down at 21:00 still goes to bed at 22:00


  // el sueño es descanso: la energia se recupera y las necesidades bajan MUCHO
  // mas lento que despierto y con suelo (amanece pidiendo algo de mimo, no a
  // cero, sin descuidos ni escapadas). despierto: comida -2/min, hig/joy -1/min.
  // El peso aun se quema y el descanso mantiene las tres vias de entrenamiento.
  if (sleeping) {
    energy = clamp100(energy + 6);
    if (weight > 0 && ageMinutes % 3 == 0) weight--;
    if (ageMinutes % 2 == 0) {                 // ~4x mas lento que despierto
      fullness = dropTo(fullness, 1, 30);
      joy = dropTo(joy, 1, 35);
    }
    if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
    trainingTick(true);
    checkMedals();  // aun puede cruzar un nivel por edad mientras duerme
    if (++ticksSinceSave >= 5) pendingSave = true;
    return;
  }

  if (!backgroundMode && ageMinutes % MINUTES_PER_LEVEL == 0)
    sfxPlay(SFX_LEVEL);  // subio de nivel (despierto)

  fullness = clamp100(fullness - 2);
  energy = clamp100(energy - 1);
  if (fullness > 40 && poops < 3 && random(100) < 15) poops++;

  hygiene = clamp100(hygiene - 1 - 4 * poops);
  // el sobrepeso da pereza: la energia cae el doble
  if (weight > 50) energy = clamp100(energy - 1);
  if (weight > 0 && ageMinutes % 3 == 0) weight--;

  int dJoy = -1;
  if (fullness < 30) dJoy -= 2;
  if (hygiene < 30) dJoy -= 2;
  joy = clamp100(joy + dJoy);
  trainingTick(false);

  // Descuido: dejar una estadistica por los suelos cuenta como error de
  // cuidado (con enfriamiento para no contar el mismo descuido cada minuto)
  if (mistakeCooldown > 0) mistakeCooldown--;
  if (lowestStat() <= 10 && mistakeCooldown == 0) {
    careMistakes++;
    mistakeCooldown = 60;
    if (bond > 1) bond--;  // el descuido enfria el vinculo, pero sin arrasarlo:
                           // a -3 cada 30 min se perdia mucho mas de lo que se
                           // podia ganar en todo un dia y el vinculo se atascaba
  }

  checkMedals();  // la evolucion la dispara el usuario (canEvolveNow + tap), no el tick
  checkLearnGates();

  // abandono total: con TODO a cero durante una hora queda lista para escaparse;
  // NO se va sola, la dispara el usuario con el boton (final triste, lo presencia)
  if (inTotalNeglect()) {
    if (neglectTicks < RUNAWAY_TICKS) neglectTicks++;
  } else {
    neglectTicks = 0;  // un solo cuidado la salva
  }

  // A final form becomes farewell-ready after three player-raised days. It is
  // still a menu action, never an automatic ending.

  // autoguardado periodico: NO escribir a flash aqui (corre dentro del loop,
  // mientras se anima); solo marcar y dejar que el loop lo vuelque al atenuar
  if (++ticksSinceSave >= 5) pendingSave = true;
}

void Pet::advanceBackgroundMinute() {
  bool wasBackground = backgroundMode;
  backgroundMode = true;
  tick();
  backgroundMode = wasBackground;
  pendingSave = false;
  ticksSinceSave = 0;
}

// GLUE: PartyMon is the versioned persistence record while Pet remains the
// behavioural runtime used throughout the sketch. Keep all field mapping in
// these two functions; remove them only when Pet directly owns CreatureState.
void Pet::importState(const PartyMon &m) {
  if (m.empty()) return;
  ceremony = CER_NONE;
  speciesId = m.dex;
  prevSpeciesId = -1;
  shiny = m.shiny != 0 || m.sparkle != 0;
  gigantamaxFactor = m.gigantamaxFactor();
  nature = natureValid(m.nature) ? m.nature
                                 : natureForLegacy(m.dex, m.ivAtk, m.ivDef,
                                                   m.ivSpe, m.ivHp);
  gender = genderValid(m.gender)
               ? m.gender
               : genderForLegacy(m.dex, dexEntry(m.dex).femaleRate,
                                 m.ivAtk, m.ivDef, m.ivSpe, m.ivHp);
  abilitySlot = abilitySlotValid(m.abilitySlot()) &&
                        speciesAbility(m.dex, m.abilitySlot())
                    ? m.abilitySlot()
                    : abilitySlotForLegacy(m.dex, m.ivAtk, m.ivDef, m.ivSpe, m.ivHp);
  ivAtk = m.ivAtk; ivDef = m.ivDef; ivSpe = m.ivSpe; ivHp = m.ivHp;
  trAtk = m.trAtk; trDef = m.trDef; trSpe = m.trSpe;
  trMinAtk = m.trMinAtk; trMinDef = m.trMinDef; trMinSpe = m.trMinSpe;
  memcpy(gymIvRewards, m.gymIvRewards, sizeof(gymIvRewards));
  for (int i = 0; i < MOVE_SLOTS; i++) moves[i] = m.moves[i];
  for (int i = 0; i < RESERVE_MOVE_SLOTS; i++) reserveMoves[i] = m.reserveMoves[i];
  ageMinutes = m.stateVersion ? m.ageMinutes
                              : (uint32_t)(m.level ? m.level - 1 : 0) * MINUTES_PER_LEVEL;
  raisedMinutes = m.stateVersion >= 3 ? m.raisedMinutes : ageMinutes;
  lastLearnLevel = m.stateVersion ? m.lastLearnLevel : (uint8_t)m.level;
  medals = m.medals;
  fullness = m.fullness; joy = m.joy; energy = m.energy; hygiene = m.hygiene;
  poops = m.poops; weight = m.weight;
  careMistakes = m.careMistakes;
  mistakeCooldown = m.mistakeCooldown;
  sleeping = m.sleeping != 0;
  sleepAuto = m.sleepAuto;
  lastEnd = m.lastEnd;
  bond = m.bond;
  bondToday = m.bondToday;
  berryKnown = m.berryKnown != 0;
  eggTarget = m.eggTarget;
  eggShiny = m.eggShiny != 0;
  eggTaps = m.eggTaps;
  starterPick = m.starterPick != 0;
  evoDeclinedLv = m.evoDeclinedLv;
  neglectTicks = m.neglectTicks;
  uint8_t maintainedTicks = (uint8_t)m.trainingTicks;
  uint8_t lowStateTicks = (uint8_t)(m.trainingTicks >> 8);
  if ((uint16_t)maintainedTicks + lowStateTicks >= TRAINING_STATE_TICKS)
    maintainedTicks = lowStateTicks = 0;
  trainingTicks = maintainedTicks | (uint16_t)lowStateTicks << 8;
  memcpy(eggByRegion, m.eggByRegion, sizeof(eggByRegion));
  frozen = false;
  dead = m.dead();
  strncpy(nick, m.nick, sizeof(nick) - 1);
  nick[sizeof(nick) - 1] = 0;
  pendingSave = false;
  ticksSinceSave = 0;
  lastTick = millis();
}

void Pet::exportState(PartyMon &out) const {
  out = PartyMon();
  out.dex = isEgg() ? -1 : speciesId;
  out.level = level();
  out.medals = medals;
  out.ivAtk = ivAtk; out.ivDef = ivDef; out.ivSpe = ivSpe; out.ivHp = ivHp;
  out.trAtk = trAtk; out.trDef = trDef; out.trSpe = trSpe;
  out.trMinAtk = trMinAtk; out.trMinDef = trMinDef; out.trMinSpe = trMinSpe;
  out.shiny = shiny ? 1 : 0;
  out.sparkle = out.shiny;
  out.setGigantamaxFactor(gigantamaxFactor);
  out.setAbilitySlot(abilitySlot);
  out.nature = nature;
  out.setDead(dead);
  out.gender = gender;
  for (int i = 0; i < MOVE_SLOTS; i++) out.moves[i] = moves[i];
  for (int i = 0; i < RESERVE_MOVE_SLOTS; i++) out.reserveMoves[i] = reserveMoves[i];
  memcpy(out.gymIvRewards, gymIvRewards, sizeof(gymIvRewards));
  strncpy(out.nick, nick, sizeof(out.nick) - 1);
  out.nick[sizeof(out.nick) - 1] = 0;
  out.stateVersion = 6;
  out.fullness = fullness; out.joy = joy; out.energy = energy; out.hygiene = hygiene;
  out.poops = poops; out.weight = weight;
  out.berryKnown = berryKnown ? 1 : 0;
  out.careMistakes = careMistakes;
  out.sleeping = sleeping ? 1 : 0; out.sleepAuto = sleepAuto;
  out.lastEnd = lastEnd; out.bond = bond;
  out.ageMinutes = ageMinutes;
  out.raisedMinutes = raisedMinutes;
  out.eggTarget = eggTarget; out.eggShiny = eggShiny ? 1 : 0; out.eggTaps = eggTaps;
  out.starterPick = starterPick ? 1 : 0;
  out.evoDeclinedLv = evoDeclinedLv;
  out.mistakeCooldown = mistakeCooldown; out.neglectTicks = neglectTicks;
  out.bondToday = bondToday; out.trainingTicks = trainingTicks;
  out.lastLearnLevel = lastLearnLevel;
  memcpy(out.eggByRegion, eggByRegion, sizeof(out.eggByRegion));
}

void Pet::reviveFrom(const PartyMon &m) {
  importState(m);
  if (!m.empty()) {
    progress.registerSpecies(speciesId, shiny);
    save();
  }
}

PartyMon Pet::toPartyMon() const {
  PartyMon out;
  exportState(out);
  return out;
}

// vuelca el guardado periodico pendiente (lo llama el loop en un momento sin
// animacion para que el paron de la escritura a flash no se vea)
void Pet::saveNow() { save(); }

void Pet::flushSave() {
  if (pendingSave) save();
}

// Training settles exactly once per 60 total minutes. Sleeping, or staying
// awake with every need at 40 or above, counts as maintained; the majority of
// the hour selects half or double nature-specific decay, with base decay on a
// 30:30 tie. Care state never grants training.
void Pet::trainingTick(bool resting) {
  if (frozen || isEgg()) return;
  uint8_t maintainedTicks = (uint8_t)trainingTicks;
  uint8_t lowStateTicks = (uint8_t)(trainingTicks >> 8);
  uint8_t decayNumerator = 0;
  uint8_t decayDenominator = 1;
  if (resting || lowestStat() >= 40) maintainedTicks++;
  else lowStateTicks++;
  if ((uint16_t)maintainedTicks + lowStateTicks >= TRAINING_STATE_TICKS) {
    decayNumerator = 1;
    if (maintainedTicks > lowStateTicks) decayDenominator = 2;
    else if (lowStateTicks > maintainedTicks) decayNumerator = 2;
    maintainedTicks = 0;
    lowStateTicks = 0;
  }
  if (decayNumerator) {
    auto decay = [](uint8_t value, uint8_t cap, uint8_t percent, uint8_t floor,
                    uint8_t numerator, uint8_t denominator) {
      if (value <= floor) return value;
      uint16_t divisor = (uint16_t)100 * denominator;
      uint32_t scaled = (uint32_t)cap * percent * numerator;
      uint8_t loss = (uint8_t)((scaled + divisor - 1) / divisor);
      return value - floor > loss ? (uint8_t)(value - loss) : floor;
    };
    trAtk = decay(trAtk, trMaxAtk(),
                  natureTrainingDecayPercent(nature, NATURE_TRAIN_ATK), trMinAtk,
                  decayNumerator, decayDenominator);
    trDef = decay(trDef, trMaxDef(),
                  natureTrainingDecayPercent(nature, NATURE_TRAIN_DEF), trMinDef,
                  decayNumerator, decayDenominator);
    trSpe = decay(trSpe, trMaxSpe(),
                  natureTrainingDecayPercent(nature, NATURE_TRAIN_SPE), trMinSpe,
                  decayNumerator, decayDenominator);
  }
  trainingTicks = maintainedTicks | (uint16_t)lowStateTicks << 8;
}

uint8_t Pet::eggRarity() const {
  return (eggTarget >= 1 && eggTarget <= dexCount()) ? dexEntry(eggTarget).rarity : R_COMUN;
}

// Pick the safety egg species by rarity, biased toward incomplete lines.

uint16_t gRegionArt = 0xFFFF;   // everything, until the SD narrows it

bool regionAvailable(uint8_t r) {
  if (r >= regionCount()) return false;
  if (r == regionAll()) {                       // the mixed pool: any pack will do
    for (uint8_t i = 0; i < regionCount(); i++)
      if (i != regionAll() && (gRegionArt & (uint16_t)(1u << i))) return true;
    return false;
  }
  return (gRegionArt & (uint16_t)(1u << r)) != 0;
}

uint8_t regionOfDex(int16_t d) {
  for (uint8_t i = 0; i < regionCount(); i++) {
    if (i == regionAll()) continue;
    if (d >= regionInfo(i).lo && d <= regionInfo(i).hi) return i;
  }
  return regionAll();
}

uint8_t nextAvailableRegion(uint8_t from) {
  for (uint8_t i = 1; i <= regionCount(); i++) {
    uint8_t r = (uint8_t)((from + i) % regionCount());
    if (regionAvailable(r)) return r;
  }
  return from;                      // nothing available anywhere: stay put
}

// The region to actually hatch from. Normally the player's own, but a card can
// be swapped under a save: rather than rewrite their choice (which would lose
// it silently the moment they put the right card back), the CHOICE is kept and
// only the roll falls through to somewhere playable.
static uint8_t eggRegionFallback(uint8_t want) {
  if (regionAvailable(want)) return want;
  for (uint8_t i = 0; i < regionCount(); i++)
    if (i != regionAll() && regionAvailable(i)) return i;
  return want;                      // no art anywhere: behave as we always did
}

// Reservoir sampling keeps selection uniform without a firmware-sized species
// array or a hard cap on how many candidates a future region pack may contain.
static int16_t pickRegionSpecies(const RegionInfo &rg, uint8_t tier,
                                 bool incompleteOnly, const Pet &pet) {
  int16_t selected = 0;
  uint16_t seen = 0;
  for (int16_t d = rg.lo; d <= rg.hi; d++) {
    if (!spriteAvailable(d)) continue;
    if (dexEntry(d).rarity != tier) continue;
    if (incompleteOnly && !pet.playerProgress().lineHasUnregistered(d)) continue;
    if (!regionAvailable(regionOfDex(d))) continue;
    seen++;
    if (random(seen) == 0) selected = d;
  }
  return selected;
}

int16_t Pet::pickEggSpecies() {
  const uint8_t use = eggRegionFallback(progress.region % regionCount());
  const RegionInfo &rg = regionInfo(use);
  // primera partida: inicial clasico -- del region elegida, so a Johto run
  // starts with a Johto starter rather than a Kanto one
  if (progress.registeredCount() == 0) {
    return rg.starters[random(rg.starterCount)];
  }

  uint8_t tier = R_COMUN;
  int rare = 27 + careBonus();
  int leg = (progress.registeredCount() >= 25) ? 3 + careBonus() / 3 : 0;
  int r = random(100);
  if (r < leg) tier = R_LEGENDARIO;
  else if (r < leg + rare) tier = R_RARO;

  // candidatos del tier con linea incompleta; si no hay, baja de tier;
  // si la pokedex del tier esta completa, vale cualquiera del tier
  for (int pass = 0; pass < 2; pass++) {
    for (int t = tier; t >= R_COMUN; t--) {
      int16_t selected = pickRegionSpecies(rg, (uint8_t)t, pass == 0, *this);
      if (selected) return selected;
    }
  }
  return rg.starters[random(rg.starterCount)];  // inalcanzable, por si acaso
}

// Rolls a species of a GIVEN tier inside a region. Used when the player changes
// region while an egg is waiting: the rarity they were granted is kept and only
// the region changes, so switching cannot be farmed for a legendary.
int16_t Pet::rollInRegion(uint8_t r, uint8_t tier) {
  const RegionInfo &rg = regionInfo(eggRegionFallback(r % regionCount()));
  for (int t = tier; t >= R_COMUN; t--) {
    int16_t selected = pickRegionSpecies(rg, (uint8_t)t, false, *this);
    if (selected) return selected;
  }
  return rg.starters[0];      // a region with nothing in it cannot happen
}

// Changing region swaps the WAITING egg to that region's creature.
//
// Without this the setting would look broken: you would pick Johto and still
// hatch a Rattata, because the species is rolled when the egg appears and not
// when it cracks. Two rules stop it becoming a re-roll button:
//
//   1. The rarity tier is kept. Only which species of that tier changes, so
//      toggling can never be farmed for a legendary.
//   2. Each region's answer is REMEMBERED for this egg. Switching back shows
//      the same creature again, so there is nothing to gain by flipping.
//
// A hatched creature is untouched -- this only ever moves an egg.
void Pet::setRegion(uint8_t r) {
  r %= regionCount();
  // The sprite pack is a real gate, not a hint: without it the region is not
  // selectable at all. The chooser still SHOWS it, greyed and with a reason --
  // hiding it outright is how Johto and Hoenn once came to look absent when
  // they were built and reachable all along.
  if (!regionAvailable(r)) return;
  if (r == progress.region) return;
  uint8_t old = progress.region;
  progress.region = r;
  if (isEgg() && eggTarget >= 1) {
    if (old < regionCount()) eggByRegion[old] = eggTarget;
    int16_t known = eggByRegion[r];
    eggTarget = known >= 1 ? known : rollInRegion(r, eggRarity());
    eggByRegion[r] = eggTarget;
  }
  save();
}

void Pet::setDead(bool value) {
  if (isEgg()) return;
  dead = value;
  if (dead) sleeping = false;
  save();
}

bool Pet::giveGigantamaxFactor() {
  if (isEgg() || gigantamaxFactor) return false;
  gigantamaxFactor = true;
  save();
  return true;
}

// la racha y el vinculo mejoran el sorteo del huevo (0..~14)
int Pet::careBonus() const {
  int s = progress.streak > 30 ? 30 : progress.streak;
  return s / 3 + bond / 25;
}

// primer cuidado del dia: avanza la racha y afianza el vinculo
void Pet::registerCare() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint32_t d = today();
  if (d == 0 || d == progress.lastCareDay) return;  // sin reloj, o ya conto hoy
  if (progress.lastCareDay == 0 || d == progress.lastCareDay + 1) {
    progress.streak++;
  } else {
    progress.streak = 1;        // hubo un hueco de dias
    progress.lastMilestone = 0;
  }
  progress.lastCareDay = d;
  bondToday = 0;
  if (progress.streak > progress.bestStreak)
    progress.bestStreak = progress.streak;
  bond = clamp100(bond + 4);
  uint16_t ms = (progress.streak >= 100) ? 100
              : (progress.streak >= 30) ? 30
              : (progress.streak >= 7)  ? 7
              : (progress.streak >= 3)  ? 3 : 0;
  if (ms > progress.lastMilestone) {
    progress.lastMilestone = ms;
    milestoneUntil = millis() + 4500;
  }
  checkMedals();
  save();
}

void Pet::addBond(uint8_t amt) {
  if (bondToday >= 20) return;  // tope diario: el vinculo no se farmea
  bond = clamp100(bond + amt);
  bondToday += amt;
}

void Pet::checkMedals() {
  if (isEgg()) return;
  uint16_t before = medals;
  if (level() >= 10) medals |= MED_LV10;
  if (level() >= 25) medals |= MED_LV25;
  if (level() >= 50) medals |= MED_LV50;
  if (berryKnown) medals |= MED_BERRY;
  if (progress.streak >= 7) medals |= MED_STREAK7;
  if (bond >= 100) medals |= MED_BOND;
  if (!evolutionAvailable(speciesId)) medals |= MED_FINAL;
  if (weight == 0 && level() >= 5 && careMistakes == 0) medals |= MED_FIT;
  uint16_t gained = medals & ~before;
  if (gained) {
    for (uint16_t m = gained; m; m &= (m - 1)) progress.totalMedals++;
    newMedal = gained;
    medalUntil = millis() + 4000;
    if (!backgroundMode && !sleeping) sfxPlay(SFX_MEDAL);
    save();
  }
}

void Pet::rename(const char *name) {
  strncpy(nick, name, sizeof(nick) - 1);
  nick[sizeof(nick) - 1] = 0;
  save();
}

// La aportacion del IV (IV x nivel / 100) es exactamente la de los juegos de
// 3a generacion en adelante: un IV perfecto vale +31 a nivel 100. El resto de
// la formula es la de TamaPoke (base plana + nivel) y no la de los juegos: con
// el x nivel/100 canonico sobre la base, un bicho recien nacido mostraria
// stats de un solo digito, que en una pantalla de mascota parece un error.
static uint16_t calcStat(uint8_t base, uint8_t iv, uint8_t lvl, uint8_t tr,
                         NatureId nature, PetGender gender, NatureStat stat) {
  uint16_t untrained = (uint16_t)base + lvl + (uint16_t)iv * lvl / 100;
  return genderStatValue(gender, stat,
                         natureStatValue(nature, stat, untrained, tr));
}

uint16_t Pet::atkStat() const {
  return isEgg() ? 0 : calcStat(dexEntry(speciesId).bAtk, ivAtk, level(), trAtk,
                                nature, gender, NATURE_STAT_ATK);
}
uint16_t Pet::defStat() const {
  return isEgg() ? 0 : calcStat(dexEntry(speciesId).bDef, ivDef, level(), trDef,
                                nature, gender, NATURE_STAT_DEF);
}
uint16_t Pet::speStat() const {
  return isEgg() ? 0 : calcStat(dexEntry(speciesId).bSpe, ivSpe, level(), trSpe,
                                nature, gender, NATURE_STAT_SPE);
}
// la vitalidad no se entrena (no hay nada que la suba), asi que lleva un +10
// fijo en lugar del entrenamiento, igual que el +Nivel+10 del HP en los juegos
uint16_t Pet::vitStat() const {
  return isEgg() ? 0 : calcStat(dexEntry(speciesId).bHp, ivHp, level(), 10,
                                nature, gender, NATURE_STAT_NONE);
}
// Special reuses the physical IV and training against the species' special base
// stat, which is what keeps Alakazam (50 Atk / 135 SpA) a real attacker without
// adding more persisted IV fields.
uint16_t Pet::spaStat() const {
  return isEgg() ? 0 : calcStat(dexEntry(speciesId).bSpA, ivAtk, level(), trAtk,
                                nature, gender, NATURE_STAT_SPA);
}
uint16_t Pet::spdStat() const {
  return isEgg() ? 0 : calcStat(dexEntry(speciesId).bSpD, ivDef, level(), trDef,
                                nature, gender, NATURE_STAT_SPD);
}

bool Pet::canRaiseTrainingFloor(TrainingStat stat) const {
  if (isEgg() || frozen || ceremony != CER_NONE) return false;
  switch (stat) {
    case TRAINING_ATK: return trMinAtk < trMaxAtk();
    case TRAINING_DEF: return trMinDef < trMaxDef();
    case TRAINING_SPE: return trMinSpe < trMaxSpe();
    default: return false;
  }
}

bool Pet::raiseTrainingFloor(TrainingStat stat, uint8_t amount) {
  if (!amount || !canRaiseTrainingFloor(stat)) return false;
  uint8_t *floor = nullptr, *training = nullptr, cap = 0;
  switch (stat) {
    case TRAINING_ATK: floor = &trMinAtk; training = &trAtk; cap = trMaxAtk(); break;
    case TRAINING_DEF: floor = &trMinDef; training = &trDef; cap = trMaxDef(); break;
    case TRAINING_SPE: floor = &trMinSpe; training = &trSpe; cap = trMaxSpe(); break;
    default: return false;
  }
  uint16_t next = (uint16_t)*floor + amount;
  *floor = next > cap ? cap : (uint8_t)next;
  if (*training < *floor) *training = *floor;
  save();
  return true;
}

// ---------- moves ----------

uint8_t Pet::moveCount() const {
  uint8_t n = 0;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (moves[i]) n++;
  return n;
}

uint8_t Pet::learnedMoveCount() const {
  return learnedMoveCount(moves, reserveMoves);
}

uint8_t Pet::learnedMoveCount(const MoveId (&active)[MOVE_SLOTS],
                              const MoveId (&reserve)[RESERVE_MOVE_SLOTS]) {
  uint8_t n = 0;
  for (MoveId move : active) if (move) n++;
  for (MoveId move : reserve) if (move) n++;
  return n;
}

bool Pet::knowsMove(MoveId mv) const {
  return knowsLearnedMove(moves, reserveMoves, mv);
}

bool Pet::knowsLearnedMove(const MoveId (&active)[MOVE_SLOTS],
                           const MoveId (&reserve)[RESERVE_MOVE_SLOTS],
                           MoveId mv) {
  if (!mv) return false;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (active[i] == mv) return true;
  for (int i = 0; i < RESERVE_MOVE_SLOTS; i++)
    if (reserve[i] == mv) return true;
  return false;
}

bool Pet::canLearnStone(MoveId mv) const {
  return !isEgg() && ceremony == CER_NONE && speciesCanLearnMove(speciesId, mv);
}

bool Pet::placeLearnedMove(MoveId mv) {
  return placeInLearnedMoves(moves, reserveMoves, mv);
}

bool Pet::placeInLearnedMoves(MoveId (&active)[MOVE_SLOTS],
                              MoveId (&reserve)[RESERVE_MOVE_SLOTS],
                              MoveId mv) {
  if (!moveValid(mv) || knowsLearnedMove(active, reserve, mv)) return false;
  for (MoveId &slot : active)
    if (!slot) { slot = mv; return true; }
  for (MoveId &slot : reserve)
    if (!slot) { slot = mv; return true; }
  uint8_t replace = (uint8_t)random(LEARNED_MOVE_SLOTS);
  if (replace < MOVE_SLOTS) active[replace] = mv;
  else reserve[replace - MOVE_SLOTS] = mv;
  return true;
}

bool Pet::teachMove(MoveId mv) {
  if (!canLearnStone(mv) || knowsMove(mv) || !placeLearnedMove(mv)) return false;
  save();
  return true;
}

void Pet::relearnFromLevel() {
  for (MoveId &move : moves) move = MOVE_NONE;
  for (MoveId &move : reserveMoves) move = MOVE_NONE;
  lastLearnLevel = 0;
  if (isEgg()) return;
  uint8_t lvl = level();
  uint16_t n = learnCount(speciesId);
  for (uint16_t i = 0; i < n; i++) {
    if (learnMethod(speciesId, i) != LM_LEVEL_UP || learnLevel(speciesId, i) > lvl)
      continue;
    placeLearnedMove(learnMove(speciesId, i));
  }
  lastLearnLevel = lvl;
}

void Pet::checkLearnGates() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint8_t lvl = level();
  if (lvl <= lastLearnLevel) return;
  uint16_t n = learnCount(speciesId);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(speciesId, i);
    if (learnMethod(speciesId, i) != LM_LEVEL_UP ||
        at <= lastLearnLevel || at > lvl) continue;
    placeLearnedMove(learnMove(speciesId, i));
  }
  lastLearnLevel = lvl;
  pendingSave = true;
}

// Tirada de un IV: 8-31. El suelo en 8 es deliberado — en los juegos un 0 es
// posible porque puedes criar cientos de huevos, aqui cada crianza dura 3 dias
// y un individuo de desecho seria un castigo desproporcionado. La racha y el
// vinculo del bicho ANTERIOR empujan la tirada: cuidar bien mejora la camada.
uint8_t Pet::rollIV(int bonus) const {
  int v = 8 + (int)random(24) + bonus / 2;  // bonus 0..14 -> +0..7
  return (uint8_t)(v > 31 ? 31 : v);
}

void Pet::rollIVs() {
  int bonus = careBonus();
  ivAtk = rollIV(bonus);
  ivDef = rollIV(bonus);
  ivSpe = rollIV(bonus);
  ivHp = rollIV(bonus);
  // los legendarios nacen con 3 de 4 IV perfectos, como en los juegos
  if (speciesId >= 1 && speciesId <= dexCount() && dexEntry(speciesId).rarity == R_LEGENDARIO) {
    uint8_t *p[4] = { &ivAtk, &ivDef, &ivSpe, &ivHp };
    for (int k = 3; k > 0; k--) {  // baraja para elegir cuales 3
      int j = random(k + 1);
      uint8_t *t = p[k]; p[k] = p[j]; p[j] = t;
    }
    for (int k = 0; k < 3; k++) *p[k] = 31;
  }
  // en la 2a generacion el shiny ERA un patron de DV concreto: un shiny nunca
  // era mediocre. Aqui se traduce como un suelo de 20 en todos los IV.
  if (shiny) {
    if (ivAtk < 20) ivAtk = 20;
    if (ivDef < 20) ivDef = 20;
    if (ivSpe < 20) ivSpe = 20;
    if (ivHp < 20) ivHp = 20;
  }
}

// Final form with three player-raised days: the menu may offer farewell.
bool Pet::canFarewellNow() const {
  if (frozen || dead) return false;
  return !isEgg() && !sleeping && ceremony == CER_NONE &&
         !evolutionAvailable(speciesId) && raisedMinutes >= FAREWELL_AGE_MIN;
}

// abandono total durante 1h: lista para escaparse. La dispara el usuario con el
// boton (final triste); cuidarla un solo tick la salva (neglectTicks se resetea)
bool Pet::canRunawayNow() const {
  if (frozen || dead) return false;
  // inTotalNeglect() as well as the counter, and NOT just the counter. The
  // sleeping branch of tick() returns before the neglect block, so neglectTicks
  // is frozen rather than cleared for the whole night: a creature that went to
  // bed at zero woke with energy back at 100 and was still one tap from
  // leaving, until the next tick 60 s later cleared it. That tap is a caress --
  // the button is drawn over the creature -- so the window really was reachable
  // and it cost somebody a DRAGONAIR.
  return !isEgg() && !sleeping && ceremony == CER_NONE &&
         neglectTicks >= RUNAWAY_TICKS && inTotalNeglect();
}

bool Pet::canExitNow() const {
  if (frozen || dead) return false;
  return !isEgg() && !sleeping && ceremony == CER_NONE && !starterPick;
}

void Pet::startFarewell() {
  if (!canFarewellNow()) return;
  lastEnd = CER_FAREWELL;
  ceremony = CER_FAREWELL;
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;  // corazones durante toda la despedida
  sfxPlay(SFX_BYE);
  save();
}

void Pet::startRunaway() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_RUNAWAY;
  ceremony = CER_RUNAWAY;
  ceremonyUntil = millis() + CEREMONY_MS;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::release() {
  if (!canExitNow() || canFarewellNow()) return;
  lastEnd = CER_RELEASE;
  ceremony = CER_RELEASE;
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::hatch() {
  speciesId = eggTarget;
  dead = false;
  shiny = eggShiny;
  gigantamaxFactor = false;
  // IV del individuo (cada crianza es unica). Se tiran ANTES de resetear el
  // vinculo a proposito: el careBonus que los empuja es el del bicho anterior.
  rollIVs();
  nature = (NatureId)random(NATURE_COUNT);
  gender = genderFromRate(dexEntry(speciesId).femaleRate, (uint8_t)random(8));
  abilitySlot = abilitySlotForLegacy(speciesId, ivAtk, ivDef, ivSpe, ivHp);
  trAtk = trDef = trSpe = 0;
  trMinAtk = trMinDef = trMinSpe = 0;
  raisedMinutes = 0;
  trainingTicks = 0;
  berryKnown = false;
  bond = 0;          // vinculo, medallas y nombre son del individuo
  bondToday = 0;
  medals = 0;
  newMedal = 0;
  nick[0] = 0;
  progress.registerSpecies(speciesId, shiny);  // criado = registrado en la pokedex
  // Start empty: checkLearnGates() fills natural level-1 moves in active slots
  // first, then reserves.
  for (MoveId &move : moves) move = MOVE_NONE;
  for (MoveId &move : reserveMoves) move = MOVE_NONE;
  lastLearnLevel = 0;
  checkLearnGates();
  checkMedals();     // por si nace ya en forma final (legendario)
  if (!backgroundMode) sfxPlay(SFX_HATCH);
  save();
}

// ¿se dan ya las condiciones para evolucionar? Cada descuido retrasa la
// evolucion 1 nivel, y ademas tiene que estar bien cuidado en ese momento
// (ninguna estadistica por debajo de 40). NO evoluciona sola: la dispara el
// usuario tocando al bicho (evolve()), para que vea la transformacion.
bool Pet::canEvolveNow() const {
  if (frozen || dead) return false;
  if (isEgg() || sleeping || ceremony != CER_NONE) return false;
  const DexEntry &d = dexEntry(speciesId);
  if (!evolutionAvailable(speciesId)) return false;
  return level() >= (uint16_t)(d.evolveLevel + careMistakes) &&
         lowestStat() >= 40;
}

void Pet::evolve() {
  if (!canEvolveNow()) return;
  prevSpeciesId = speciesId;
  SpeciesId options[CONTENT_MAX_EVOLUTIONS];
  uint8_t optionCount = 0;
  for (uint8_t i = 0; i < evolutionCount(speciesId); i++) {
    SpeciesId target = evolutionTarget(speciesId, i);
    if (dexValid(target) && !progress.isRegistered(target)) options[optionCount++] = target;
  }
  if (!optionCount)
    for (uint8_t i = 0; i < evolutionCount(speciesId); i++) {
      SpeciesId target = evolutionTarget(speciesId, i);
      if (dexValid(target)) options[optionCount++] = target;
    }
  SpeciesId next = options[random(optionCount)];
  speciesId = next;
  if (!speciesAbility(speciesId, abilitySlot))
    abilitySlot = abilitySlotForLegacy(speciesId, ivAtk, ivDef, ivSpe, ivHp);
  progress.registerSpecies(speciesId, shiny);
  checkLearnGates();   // the new form may gate a move at this very level
  sfxPlay(SFX_EVOLVE);
  evolveUntil = millis() + EVOLVE_ANIM_MS;
  save();
}

void Pet::feed() {
  feedBerry(0);
}

void Pet::feedBerry(uint8_t color) {
  settleCare({ CARE_ACTION_FEED_BERRY, color }, 100);
}

void Pet::feedCandy() {
  settleCare({ CARE_ACTION_FEED_CANDY, 0 }, 100);
}

uint8_t Pet::playResult(uint8_t score) {
  return settleCare({ CARE_ACTION_PLAY, score }, 100);
}

uint8_t Pet::trainSpeed(uint16_t hits) {
  return settleCare({ CARE_ACTION_TRAIN_SPEED, hits }, 100);
}

uint8_t Pet::trainStrength(uint16_t hits) {
  return settleCare({ CARE_ACTION_TRAIN_STRENGTH, hits }, 100);
}

static uint8_t scaledCareGain(uint16_t value, uint8_t percent) {
  if (percent > 100) percent = 100;
  return (uint8_t)(((uint32_t)value * percent + 50u) / 100u);
}

static constexpr uint8_t TRAINING_FULL_SCORE = 18;
static constexpr uint8_t TRAINING_MAX_CAP_PERCENT = 30;

static uint8_t scaledTrainingGain(uint8_t cap, uint8_t current,
                                  uint8_t scoreGain, uint8_t answerPercent) {
  if (answerPercent > 100) answerPercent = 100;
  if (scoreGain > TRAINING_FULL_SCORE) scoreGain = TRAINING_FULL_SCORE;
  if (current >= cap || !scoreGain || !answerPercent) return 0;
  uint8_t maxGain = (uint16_t)cap * TRAINING_MAX_CAP_PERCENT / 100u;
  constexpr uint32_t denominator = TRAINING_FULL_SCORE * 100u;
  uint32_t gain = ((uint32_t)maxGain * scoreGain * answerPercent +
                   denominator / 2u) / denominator;
  uint8_t remaining = cap - current;
  return gain < remaining ? (uint8_t)gain : remaining;
}

uint8_t Pet::settleCare(const CareAction &action, uint8_t percent) {
  if (percent > 100) percent = 100;
  if (ceremony != CER_NONE || isEgg()) return 0;
  if ((action.kind == CARE_ACTION_FEED_BERRY || action.kind == CARE_ACTION_FEED_CANDY ||
       action.kind == CARE_ACTION_CARESS) && sleeping) return 0;

  if (action.kind == CARE_ACTION_FEED_BERRY) {
    bool loved = lovesBerry((uint8_t)action.value);
    uint8_t food = scaledCareGain(loved ? 35 : 25, percent);
    uint8_t happiness = scaledCareGain(loved ? 10 : 0, percent);
    fullness = clamp100(fullness + food);
    joy = clamp100(joy + happiness);
    if (percent) {
      if (loved) {
        heartUntil = millis() + HEART_MS;
        berryKnown = true;
        addBond(scaledCareGain(2, percent));
      }
      registerCare();
    }
    if (percent) eatUntil = millis() + EAT_ANIM_MS;
    if (percent) save();
    return food;
  }
  if (action.kind == CARE_ACTION_FEED_CANDY) {
    uint8_t food = scaledCareGain(10, percent);
    fullness = clamp100(fullness + food);
    joy = clamp100(joy + scaledCareGain(12, percent));
    weight = clamp100(weight + scaledCareGain(12, percent));
    if (percent) registerCare();
    if (percent) eatUntil = millis() + EAT_ANIM_MS;
    if (percent) save();
    return food;
  }
  if (action.kind == CARE_ACTION_CLEAN) {
    if (!percent) return 0;
    uint8_t gain = scaledCareGain(100 - hygiene, percent);
    hygiene = clamp100(hygiene + gain);
    poops = 0;
    addBond(scaledCareGain(1, percent));
    registerCare();
    save();
    return gain;
  }
  if (action.kind == CARE_ACTION_CARESS) {
    uint8_t gain = scaledCareGain(5, percent);
    joy = clamp100(joy + gain);
    if (percent) {
      heartUntil = millis() + HEART_MS;
      addBond(scaledCareGain(1, percent));
      registerCare();
    }
    return gain;
  }
  if (action.kind == CARE_ACTION_PLAY) {
    uint16_t score = action.value;
    // The ball game is DEFENCE's trainer now. It used to train SPEED, which was
    // moved to its own reaction test to stop playing being a stat grind -- and
    // that left DEF with no active trainer at all, only the slow passive tick.
    // Keeping the ball on the defensive stat fits it: you are stopping something
    // from getting past you.
    uint8_t before = trDef;
    uint8_t baseGain = score / 2;
    if (baseGain > TRAINING_FULL_SCORE) baseGain = TRAINING_FULL_SCORE;
    uint8_t potentialGain = baseGain < trMaxDef() - before ? baseGain : trMaxDef() - before;
    uint8_t gain = scaledTrainingGain(trMaxDef(), before, baseGain, percent);
    uint16_t v = (uint16_t)trDef + gain;
    trDef = v > trMaxDef() ? trMaxDef() : (uint8_t)v;
    gain = trDef - before;
    uint8_t joyGain = (uint8_t)(5 + (score > 15 ? 30 : score * 2));
    joy = clamp100(joy + scaledCareGain(joyGain, percent));
    energy = dropTo(energy, 10 + score / 2, 5);
    fullness = dropTo(fullness, 5, 5);
    int burn = (int)weight - score * 2;  // el ejercicio quema peso
    weight = burn > 0 ? burn : 0;
    if (percent && score >= 5) heartUntil = millis() + HEART_MS;
    if (score > progress.gameHi) progress.gameHi = score;  // nuevo record
    // Training bonds, and it scales with the session: a token effort is worth the
    // base, a full one is worth more. The daily cap in addBond() still stops it
    // being farmed -- this changes how fast a good session gets there, not the
    // ceiling.
    if (percent) {
      addBond(scaledCareGain((uint8_t)(2 + potentialGain / 6), percent));
      registerCare();
    }
    save();
    return gain;
  }
  if (action.kind == CARE_ACTION_TRAIN_SPEED) {
    uint16_t hits = action.value;
    uint8_t baseGain = hits / 2;
    if (baseGain > TRAINING_FULL_SCORE) baseGain = TRAINING_FULL_SCORE;
    uint8_t before = trSpe;
    uint8_t potentialGain = baseGain < trMaxSpe() - before ? baseGain : trMaxSpe() - before;
    uint8_t gain = scaledTrainingGain(trMaxSpe(), before, baseGain, percent);
    uint8_t v = trSpe + gain;
    trSpe = v > trMaxSpe() ? trMaxSpe() : v;
    gain = trSpe - before;
    energy = dropTo(energy, 10, 5);
    fullness = dropTo(fullness, 4, 5);
    int burn = (int)weight - hits / 2;
    weight = burn > 0 ? burn : 0;
    joy = clamp100(joy + scaledCareGain(4, percent));
    if (hits > progress.spdHi) progress.spdHi = hits;
    if (percent) {
      addBond(scaledCareGain((uint8_t)(2 + potentialGain / 6), percent));
      registerCare();
    }
    save();
    return gain;
  }
  if (action.kind == CARE_ACTION_TRAIN_STRENGTH) {
    uint16_t hits = action.value;
    uint8_t baseGain = hits / 4;
    if (baseGain > TRAINING_FULL_SCORE) baseGain = TRAINING_FULL_SCORE;
    uint8_t before = trAtk;
    uint8_t potentialGain = baseGain < trMaxAtk() - before ? baseGain : trMaxAtk() - before;
    uint8_t gain = scaledTrainingGain(trMaxAtk(), before, baseGain, percent);
    uint8_t v = trAtk + gain;
    trAtk = v > trMaxAtk() ? trMaxAtk() : v;
    gain = trAtk - before;
    energy = dropTo(energy, 12, 5);
    fullness = dropTo(fullness, 5, 5);
    int burn = (int)weight - hits / 3;
    weight = burn > 0 ? burn : 0;
    joy = clamp100(joy + scaledCareGain(6, percent));
    if (percent && hits >= 20) heartUntil = millis() + HEART_MS;
    if (hits > progress.strHi) progress.strHi = hits;
    if (percent) {
      addBond(scaledCareGain((uint8_t)(2 + potentialGain / 6), percent));
      registerCare();
    }
    save();
    return gain;
  }
  return 0;
}

uint8_t Pet::gymIvRewardAt(uint8_t region, uint8_t gym) const {
  if (region >= CONTENT_MAX_REGIONS || gym >= GYM_IV_GYMS_PER_REGION) return 0;
  return gymIvRewards[(size_t)region * GYM_IV_GYMS_PER_REGION + gym];
}

GymIvReward Pet::rewardGymIv(uint8_t region, uint8_t gym, uint8_t &which) {
  which = 0;
  if (ceremony != CER_NONE || isEgg() || frozen || dead || region >= regionAll() ||
      gym >= GYM_IV_GYMS_PER_REGION || gym >= regionBattleInfo(region).gymCount)
    return GYM_IV_NONE;
  size_t at = (size_t)region * GYM_IV_GYMS_PER_REGION + gym;
  if (gymIvRewards[at] != GYM_IV_REWARD_UNCLAIMED) return GYM_IV_NONE;

  which = (uint8_t)random(4);
  switch (which) {
    case 0: ivAtk++; break;
    case 1: ivDef++; break;
    case 2: ivSpe++; break;
    default: ivHp++; break;
  }
  gymIvRewards[at] = (uint8_t)(which + 1);
  return GYM_IV_GAINED;
}

void Pet::play() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 25);
  energy = clamp100(energy - 10);
  fullness = clamp100(fullness - 5);
  heartUntil = millis() + HEART_MS;
  addBond(2);
  registerCare();
  save();
}

// The hour off the RTC, the same source the scene uses. With no clock at all
// there is no night, so a board that has never been set never auto-sleeps.
bool Pet::isNightHour() const {
  if (!lastSeenEpoch) return false;
  int h = (int)((lastSeenEpoch / 3600) % 24);
  // The window may or may not cross midnight, and it must keep working either
  // way: with NIGHT_START 0 the old `h >= START || h < END` was true for every
  // hour of the day, which put the creature to sleep the moment the screen went
  // off at noon. sleep_test catches it, and did.
  if (NIGHT_START < NIGHT_END) return h >= NIGHT_START && h < NIGHT_END;
  return h >= NIGHT_START || h < NIGHT_END;
}

// Auto-sleep needs the screen off AND the night hours, and is re-checked every
// tick rather than only on the button, so a device put down at 21:00 nods off
// at 22:00 and gets up at 06:00 without anyone touching it.
//
// Both halves earn their place. Screen-off alone paused the game whenever you
// put the device down, and the creature is meant to get hungry during the day.
// The hour alone sent it to bed while you were still playing with it.
//
// Only what this put to sleep is woken by it: a creature the player sent to bed
// with the light button stays there until the player says otherwise.
void Pet::applyAutoSleep() {
  if (isEgg() || ceremony != CER_NONE) return;
  bool night = isNightHour();
  if (screenIsOff && night && !sleeping && sleepAuto != SLEEP_PLAYER) {
    sleeping = true;
    sleepAuto = SLEEP_AUTO;
    pendingSave = true;
  }
  // NOTHING wakes it here, and that is the whole point. Waking at 06:00 would
  // reopen the hole this exists to close: from the sleep floors, food is empty
  // by 06:15 and every stat by 07:40, so anyone who sleeps past eight would
  // find the creature ready to run away again. It sleeps until YOU are up,
  // which is the screen coming back on.
  if (!night && sleepAuto == SLEEP_PLAYER) sleepAuto = SLEEP_NONE;  // a new day
}

void Pet::setScreenOff(bool off) {
  screenIsOff = off;
  // Coming back to the device is what wakes it -- only if the device is what
  // put it to sleep. A creature sent to bed with the light stays there.
  if (!off && sleeping && sleepAuto == SLEEP_AUTO) {
    sleeping = false;
    sleepAuto = SLEEP_NONE;
  }
  applyAutoSleep();
  save();
}

void Pet::toggleLight() {
  if (ceremony != CER_NONE) return;
  if (isEgg()) return;
  sleeping = !sleeping;
  // The player's hand beats the clock until morning: waking it at 23:00 must
  // not be undone a minute later by the auto-sleep, and neither must putting
  // it to bed early.
  sleepAuto = SLEEP_PLAYER;
  save();
}

void Pet::clean() {
  settleCare({ CARE_ACTION_CLEAN, 0 }, 100);
}

void Pet::caress() {
  settleCare({ CARE_ACTION_CARESS, 0 }, 100);
}

void Pet::eggTap() {
  if (!isEgg()) return;
  if (++eggTaps >= 3) hatch();
  else save();
}

PetMood Pet::mood() const {
  if (sleeping) return MOOD_SLEEPING;
  if (eating()) return MOOD_EATING;
  if (lowestStat() < 25) return MOOD_SAD;
  return MOOD_HAPPY;
}

void Pet::save() {
  if (backgroundMode) return;
  uint32_t started = perfNowUs();
  ticksSinceSave = 0;
  pendingSave = false;
  if (roster) {
    roster->captureActive(*this, false);
    roster->save();
    perfRecord(PERF_PET_SAVE, perfNowUs() - started, 1);
    return;
  }
  // Before Party is attached these scalar keys remain the migration and
  // standalone-Pet format. Normal runtime commits use the atomic team2 blob.
  prefs.putUChar("full", fullness);
  prefs.putUChar("joy", joy);
  prefs.putUChar("ene", energy);
  prefs.putUChar("hyg", hygiene);
  prefs.putUChar("poop", poops);
  prefs.putUChar("wgt", weight);
  prefs.putUChar("ivat", ivAtk);
  prefs.putUChar("ivdf", ivDef);
  prefs.putUChar("ivsp", ivSpe);
  prefs.putUChar("ivhp", ivHp);
  prefs.putUChar("nat", (uint8_t)nature);
  prefs.putUChar("gndr", (uint8_t)gender);
  prefs.putUChar("abil", (uint8_t)abilitySlot);
  prefs.putUChar("tatk", trAtk);
  prefs.putUChar("tdef", trDef);
  prefs.putUChar("tspe", trSpe);
  prefs.putUChar("tminat", trMinAtk);
  prefs.putUChar("tmindf", trMinDef);
  prefs.putUChar("tminsp", trMinSpe);
  prefs.putBytes("giv", gymIvRewards, sizeof(gymIvRewards));
  prefs.putBytes("mvs", moves, sizeof(moves));
  prefs.putBytes("rsvm", reserveMoves, sizeof(reserveMoves));
  prefs.putUChar("mvlv", lastLearnLevel);
  prefs.putBytes("eggR", eggByRegion, sizeof(eggByRegion));
  prefs.putBool("froz", frozen);
  prefs.putBool("dead", dead);
  prefs.putBool("bk", berryKnown);
  prefs.putBool("shy", shiny);
  prefs.putBool("spkl", shiny);  // legacy mirror for older firmware
  prefs.putBool("gmax", gigantamaxFactor);
  prefs.putBool("eshy", eggShiny);
  prefs.putBool("stpk", starterPick);
  prefs.putUChar("slpa", sleepAuto);
  prefs.putUInt("age", ageMinutes);
  prefs.putUInt("raise", raisedMinutes);
  prefs.putShort("dexn", speciesId);
  prefs.putShort("eggT2", eggTarget);
  prefs.putUChar("crack", eggTaps);
  prefs.putUChar("mist", careMistakes);
  prefs.putBool("sleep", sleeping);
  prefs.putUChar("lend", lastEnd);
  if (lastSeenEpoch) prefs.putUInt("seen", lastSeenEpoch);
  prefs.putUChar("bond", bond);
  prefs.putUShort("medal", medals);
  prefs.putString("nick", nick);
  progress.save();
  perfRecord(PERF_PET_SAVE, perfNowUs() - started, lastSeenEpoch ? 63 : 62);
}

void Pet::load() {
  fullness = prefs.getUChar("full", 80);
  joy = prefs.getUChar("joy", 80);
  energy = prefs.getUChar("ene", 80);
  hygiene = prefs.getUChar("hyg", 100);
  poops = prefs.getUChar("poop", 0);
  weight = prefs.getUChar("wgt", 0);
  ivAtk = prefs.getUChar("ivat", 16);
  ivDef = prefs.getUChar("ivdf", 16);
  ivSpe = prefs.getUChar("ivsp", 16);
  ivHp = prefs.getUChar("ivhp", 16);
  trAtk = prefs.getUChar("tatk", 0);
  trDef = prefs.getUChar("tdef", 0);
  trSpe = prefs.getUChar("tspe", 0);
  trMinAtk = prefs.getUChar("tminat", 0);
  trMinDef = prefs.getUChar("tmindf", 0);
  trMinSpe = prefs.getUChar("tminsp", 0);
  if (prefs.getBytesLength("giv") == sizeof(gymIvRewards))
    prefs.getBytes("giv", gymIvRewards, sizeof(gymIvRewards));
  for (uint8_t &reward : gymIvRewards)
    if (reward > GYM_IV_REWARD_HP &&
        reward != GYM_IV_REWARD_LEGACY_CLAIMED) reward = 0;
  // un guardado antiguo puede traer entrenamiento por encima del nuevo tope
  if (trAtk > trMaxAtk()) trAtk = trMaxAtk();
  if (trDef > trMaxDef()) trDef = trMaxDef();
  if (trSpe > trMaxSpe()) trSpe = trMaxSpe();
  if (trMinAtk > trMaxAtk()) trMinAtk = trMaxAtk();
  if (trMinDef > trMaxDef()) trMinDef = trMaxDef();
  if (trMinSpe > trMaxSpe()) trMinSpe = trMaxSpe();
  if (trAtk < trMinAtk) trAtk = trMinAtk;
  if (trDef < trMinDef) trDef = trMinDef;
  if (trSpe < trMinSpe) trSpe = trMinSpe;
  berryKnown = prefs.getBool("bk", false);
  shiny = prefs.getBool("shy", false) || prefs.getBool("spkl", false);
  gigantamaxFactor = prefs.getBool("gmax", false);
  eggShiny = prefs.getBool("eshy", false);
  starterPick = prefs.getBool("stpk", false);
  sleepAuto = prefs.getUChar("slpa", SLEEP_NONE);
  ageMinutes = prefs.getUInt("age", 0);
  raisedMinutes = prefs.isKey("raise") ? prefs.getUInt("raise", 0) : ageMinutes;
  speciesId = prefs.getShort("dexn", -1);
  nature = (NatureId)prefs.getUChar("nat", NATURE_UNKNOWN);
  if (!natureValid(nature) && speciesId >= 1)
    nature = natureForLegacy(speciesId, ivAtk, ivDef, ivSpe, ivHp);
  gender = (PetGender)prefs.getUChar("gndr", GENDER_UNKNOWN);
  if (speciesId < 1) gender = GENDER_UNKNOWN;
  else if (!genderValid(gender))
    gender = genderForLegacy(speciesId, dexEntry(speciesId).femaleRate,
                             ivAtk, ivDef, ivSpe, ivHp);
  abilitySlot = (AbilitySlot)prefs.getUChar("abil", ABILITY_SLOT_UNKNOWN);
  if (speciesId < 1) {
    abilitySlot = ABILITY_SLOT_UNKNOWN;
  } else if (!abilitySlotValid(abilitySlot) ||
             (dexValid(speciesId) && !speciesAbility(speciesId, abilitySlot))) {
    abilitySlot = dexValid(speciesId)
        ? abilitySlotForLegacy(speciesId, ivAtk, ivDef, ivSpe, ivHp)
        : ABILITY_SLOT_ONE;
  }
  eggTarget = prefs.getShort("eggT2", 4);
  eggTaps = prefs.getUChar("crack", 0);
  careMistakes = prefs.getUChar("mist", 0);
  sleeping = prefs.getBool("sleep", false);
  lastEnd = prefs.getUChar("lend", CER_NONE);
  bond = prefs.getUChar("bond", 0);
  medals = prefs.getUShort("medal", 0);
  prefs.getString("nick", nick, sizeof(nick));
  if (prefs.getBytesLength("mvs") == sizeof(moves)) {
    prefs.getBytes("mvs", moves, sizeof(moves));
  }
  if (prefs.getBytesLength("rsvm") == sizeof(reserveMoves)) {
    prefs.getBytes("rsvm", reserveMoves, sizeof(reserveMoves));
  }
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (moves[i] >= ::moveCount()) moves[i] = 0;
  for (int i = 0; i < RESERVE_MOVE_SLOTS; i++)
    if (reserveMoves[i] >= ::moveCount()) reserveMoves[i] = 0;
  lastLearnLevel = prefs.getUChar("mvlv", 0);
  frozen = prefs.getBool("froz", false);
  dead = prefs.getBool("dead", false) && speciesId >= 1;
  prefs.getBytes("eggR", eggByRegion, sizeof(eggByRegion));
  checkLearnGates();
  if (speciesId >= 1) progress.registerSpecies(speciesId, shiny);
}
