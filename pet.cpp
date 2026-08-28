#include "pet.h"
#include "avatars.h"
#include "dex.h"
#include "moves.h"
#include "audio.h"
#include "save.h"
#include "wild.h"

void Pet::begin() {
  prefs.begin("tamapoke", false);
  uint16_t storedVersion = prefs.getUShort("savev", 0);
  if (storedVersion != SAVE_STATE_VERSION) {
    if (prefs.isKey("init") || prefs.isKey("savev"))
      Serial.printf("save schema %u unsupported; resetting\n", storedVersion);
    prefs.clear();
    prefs.putUShort("savev", SAVE_STATE_VERSION);
  }
  // Zeroed BEFORE the branch below, not inside load(): getBytes() leaves its
  // destination untouched when the key is missing, and the fresh-install path
  // returns without ever calling load(). Without this a begin() after a factory
  // reset keeps the old Pokedex alive in RAM -- the firmware reboots on WIPE so
  // it never showed there, but anything calling begin() twice would see it, and
  // Party::begin() already guards the same way for the same reason.
  memset(badgesX, 0, sizeof(badgesX));
  memset(badgesHardX, 0, sizeof(badgesHardX));
  memset(dexReg, 0, sizeof(dexReg));
  memset(dexShinyReg, 0, sizeof(dexShinyReg));
  memset(gymIvRewards, 0, sizeof(gymIvRewards));
  for (int i = 0; i < regionCount(); i++) eggByRegion[i] = 0;
  if (!prefs.getBool("init", false)) {
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
  eggByRegion[region % regionCount()] = eggTarget;
  starterPick = (registeredCount() == 0);  // primera partida: el jugador elige inicial
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
  uint32_t mins = (seenEpoch && nowEpoch > seenEpoch) ? (nowEpoch - seenEpoch) / 60 : 0;
  if (mins < 2 || ceremony != CER_NONE || starterPick || dead) {
    if (persist) save();  // primera vez, sin tiempo que aplicar o aun eligiendo inicial
    backgroundMode = wasBackground;
    return;
  }
  if (mins > 14UL * 24 * 60) mins = 14UL * 24 * 60;  // tope: 2 semanas

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
      uint16_t next = (uint16_t)wildRareBonus + gain;
      wildRareBonus = next > WILD_RARE_BONUS_MAX ? WILD_RARE_BONUS_MAX : next;
    } else if (ending == CER_RUNAWAY) {
      wildRareBonus = wildRareBonus > 2 ? (uint8_t)(wildRareBonus - 2) : 0;
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
  ageMinutes = m.stateVersion ? m.ageMinutes
                              : (uint32_t)(m.level ? m.level - 1 : 0) * MINUTES_PER_LEVEL;
  raisedMinutes = m.stateVersion >= 3 ? m.raisedMinutes : ageMinutes;
  lastLearnLevel = m.stateVersion ? m.lastLearnLevel : (uint8_t)m.level;
  learnQCount = m.learnQCount > sizeof(learnQueue) / sizeof(learnQueue[0])
                  ? sizeof(learnQueue) / sizeof(learnQueue[0]) : m.learnQCount;
  memcpy(learnQueue, m.learnQueue, sizeof(learnQueue));
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
  memcpy(out.gymIvRewards, gymIvRewards, sizeof(gymIvRewards));
  strncpy(out.nick, nick, sizeof(out.nick) - 1);
  out.nick[sizeof(out.nick) - 1] = 0;
  out.stateVersion = 5;
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
  memcpy(out.learnQueue, learnQueue, sizeof(out.learnQueue));
  out.learnQCount = learnQCount;
  memcpy(out.eggByRegion, eggByRegion, sizeof(out.eggByRegion));
}

void Pet::reviveFrom(const PartyMon &m) {
  importState(m);
  if (!m.empty()) {
    registerSpecies(speciesId, shiny);
    save();
  }
}

PartyMon Pet::toPartyMon() const {
  PartyMon out;
  exportState(out);
  return out;
}

void Pet::copySharedFrom(const Pet &other) {
  memcpy(dexReg, other.dexReg, sizeof(dexReg));
  memcpy(dexShinyReg, other.dexShinyReg, sizeof(dexShinyReg));
  streak = other.streak; bestStreak = other.bestStreak;
  wildRareBonus = other.wildRareBonus;
  lastCareDay = other.lastCareDay;
  totalMedals = other.totalMedals;
  lastMilestone = other.lastMilestone;
  gameHi = other.gameHi; strHi = other.strHi; spdHi = other.spdHi;
  avatar = other.avatar; region = other.region;
  badges = other.badges; badgesHard = other.badgesHard;
  memcpy(badgesX, other.badgesX, sizeof(badgesX));
  memcpy(badgesHardX, other.badgesHardX, sizeof(badgesHardX));
  strncpy(trainerName, other.trainerName, sizeof(trainerName) - 1);
  trainerName[sizeof(trainerName) - 1] = 0;
  lastSeenEpoch = other.lastSeenEpoch;
  screenIsOff = other.screenIsOff;
}

void Pet::mergeSharedFrom(const Pet &other) {
  copySharedFrom(other);
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

static bool branchHasUnregistered(const Pet &pet, SpeciesId species, uint8_t depth) {
  if (!pet.isRegistered(species)) return true;
  if (depth >= CONTENT_MAX_EVOLUTIONS) return false;
  for (uint8_t i = 0; i < evolutionCount(species); i++) {
    SpeciesId target = evolutionTarget(species, i);
    if (dexValid(target) && branchHasUnregistered(pet, target, depth + 1)) return true;
  }
  return false;
}

// quedan miembros sin registrar en la linea evolutiva de esta base?
bool Pet::lineHasUnregistered(int16_t base) const {
  return dexValid(base) && branchHasUnregistered(*this, (SpeciesId)base, 0);
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
    if (incompleteOnly && !pet.lineHasUnregistered(d)) continue;
    if (!regionAvailable(regionOfDex(d))) continue;
    seen++;
    if (random(seen) == 0) selected = d;
  }
  return selected;
}

int16_t Pet::pickEggSpecies() {
  const uint8_t use = eggRegionFallback(region % regionCount());
  const RegionInfo &rg = regionInfo(use);
  // primera partida: inicial clasico -- del region elegida, so a Johto run
  // starts with a Johto starter rather than a Kanto one
  if (registeredCount() == 0) {
    return rg.starters[random(rg.starterCount)];
  }

  uint8_t tier = R_COMUN;
  int rare = 27 + careBonus();
  int leg = (registeredCount() >= 25) ? 3 + careBonus() / 3 : 0;
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
  if (r == region) return;
  uint8_t old = region;
  region = r;
  if (isEgg() && eggTarget >= 1) {
    if (old < regionCount()) eggByRegion[old] = eggTarget;
    int16_t known = eggByRegion[r];
    eggTarget = known >= 1 ? known : rollInRegion(r, eggRarity());
    eggByRegion[r] = eggTarget;
  }
  save();
}

void Pet::registerSpecies(int16_t dex, bool color) {
  if (dex < 1 || dex > dexCount()) return;
  dexReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  if (color) dexShinyReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
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
  int s = streak > 30 ? 30 : streak;
  return s / 3 + bond / 25;
}

// primer cuidado del dia: avanza la racha y afianza el vinculo
void Pet::registerCare() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint32_t d = today();
  if (d == 0 || d == lastCareDay) return;  // sin reloj, o ya conto hoy
  if (lastCareDay == 0 || d == lastCareDay + 1) {
    streak++;
  } else {
    streak = 1;        // hubo un hueco de dias
    lastMilestone = 0;
  }
  lastCareDay = d;
  bondToday = 0;
  if (streak > bestStreak) bestStreak = streak;
  bond = clamp100(bond + 4);
  uint16_t ms = (streak >= 100) ? 100 : (streak >= 30) ? 30
              : (streak >= 7)   ? 7   : (streak >= 3)  ? 3 : 0;
  if (ms > lastMilestone) {
    lastMilestone = ms;
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
  if (streak >= 7) medals |= MED_STREAK7;
  if (bond >= 100) medals |= MED_BOND;
  if (!evolutionAvailable(speciesId)) medals |= MED_FINAL;
  if (weight == 0 && level() >= 5 && careMistakes == 0) medals |= MED_FIT;
  uint16_t gained = medals & ~before;
  if (gained) {
    for (uint16_t m = gained; m; m &= (m - 1)) totalMedals++;
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

bool Pet::knowsMove(MoveId mv) const {
  if (!mv) return false;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (moves[i] == mv) return true;
  return false;
}

// Picks a sensible default set, best first.
//
// NOT "the newest four": 1907 of the 2281 learnset entries sit at level 0 and
// another 237 at level 1, so for most species level orders nothing and taking
// the last four in table order is arbitrary -- it handed a level 100 Charizard
// GROWL and LEER. So score instead: attacks over status, stronger over weaker,
// STAB ahead of equal power, and a bonus for the handful of moves that really
// are gated behind a level, since those are meant to be upgrades.
// TMs unlock at one level, for everything.
//
// This replaced a power/2 curve, which was impossible for a player to predict
// (SURF at 45, ROCK SLIDE at 37) and dribbled unlocks out one at a time so none
// of them felt like anything. A single number is explainable in one sentence and
// lands on a seam the game already has: the first five leaders sit at 14-43, so
// you fight the early ladder on what your species actually learns, and TMs
// arrive as you enter the back half. A creature may qualify for farewell after
// three cultivated days and caps at 100.
//
// It only works because dex_moves.py now carries the cheap early attacks --
// SCRATCH, PECK, POISON STING, BUBBLE and the rest. Without those, gating TMs
// this hard would leave young creatures with nothing at all, which is exactly
// what the power/2 version was papering over.
#define TM_LEVEL 40

static uint8_t tmLevelFor(const MoveEntry &m) {
  (void)m;
  return TM_LEVEL;
}

// THE single answer, used by relearnFromLevel(), by the STAB fallback and by
// the move picker in the sketch. Three call sites once had three opinions.
uint8_t moveUnlockLevel(SpeciesId dex, uint16_t idx) {
  uint8_t at = learnLevel(dex, idx);
  if (learnMethod(dex, idx) == LM_LEVEL_UP) return at;
  MoveId mv = learnMove(dex, idx);
  if (!mv || mv >= ::moveCount()) return 255;
  return tmLevelFor(moveEntry(mv));       // a TM: no natural level, so the gate
}

void Pet::relearnFromLevel() {
  for (int i = 0; i < MOVE_SLOTS; i++) moves[i] = 0;
  if (isEgg()) return;
  const DexEntry &d = dexEntry(speciesId);
  uint8_t lvl = level();
  uint16_t n = learnCount(speciesId);
  int16_t score[MOVE_SLOTS] = { 0, 0, 0, 0 };
  // Two passes. Level-up moves (level >= 1) are what a creature grows into, so
  // they fill the set first; TMs (level 0, no gate) only top up the slots left
  // over. Without this a just-hatched pet opens with FIRE BLAST and SOLAR BEAM,
  // because every TM is legal at level 1.
  for (int pass = 0; pass < 2; pass++) {
  bool tmPass = (pass == 1);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(speciesId, i);
    if (at > lvl) continue;
    if (tmPass != (learnMethod(speciesId, i) != LM_LEVEL_UP)) continue;
    MoveId mv = learnMove(speciesId, i);
    if (!mv || mv >= ::moveCount() || knowsMove(mv)) continue;
    const MoveEntry &m = moveEntry(mv);
    // A TM carries no level requirement in the data, which is true of the games
    // but wrong here: with only one or two level-up moves early on, the spare
    // slots were filled with the strongest TMs in the table and a NEWBORN opened
    // with SURF, BLIZZARD and OUTRAGE. That, not the damage formula, is why a
    // level 1 Squirtle could beat Brock.
    //
    // So a TM is gated by its own power: roughly power/2, which puts the 40s
    // around level 20 and the 110s out past 50 where a creature is genuinely
    // built. Level-up moves are untouched -- they already have real gates.
    if (tmPass && lvl < tmLevelFor(m)) continue;
    int16_t sc = (m.cat == MC_STATUS) ? 10 : (int16_t)m.power + 20;
    // STAB outweighs raw power, or every species defaults to the same two
    // generic sledgehammers and the roster loses its identity.
    if (m.cat != MC_STATUS && (m.type == d.type1 || m.type == d.type2)) sc += 40;
    if (m.effect == EF_RECHARGE) sc -= 35;   // a free turn for the opponent
    if (m.effect == EF_RECOIL) sc -= 20;
    sc += at;
    if (sc < 1) sc = 1;
    int slot = -1;
    for (int s = 0; s < MOVE_SLOTS; s++)
      if (sc > score[s]) { slot = s; break; }
    if (slot < 0) continue;
    for (int s = MOVE_SLOTS - 1; s > slot; s--) {
      score[s] = score[s - 1];
      moves[s] = moves[s - 1];
    }
    score[slot] = sc;
    moves[slot] = mv;
  }
  if (moveCount() >= MOVE_SLOTS) break;   // level-up moves already filled it
  }
  // Guarantee one same-type move. Machamp's only Fighting options are weak or
  // recoil-laden, so pure scoring left it with four generic attacks and nothing
  // that reads as a Machamp. If the set came out with no STAB, the weakest slot
  // gives way to the best same-type attack the species can actually learn.
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (!moves[i]) continue;
    const MoveEntry &m = moveEntry(moves[i]);
    if (m.cat != MC_STATUS && (m.type == d.type1 || m.type == d.type2)) return;
  }
  MoveId best = 0;
  int16_t bestSc = 0;
  for (uint16_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(speciesId, i);
    if (at > lvl) continue;
    MoveId mv = learnMove(speciesId, i);
    if (!mv || mv >= ::moveCount()) continue;
    const MoveEntry &m = moveEntry(mv);
    if (m.cat == MC_STATUS || (m.type != d.type1 && m.type != d.type2)) continue;
    // The same TM gate as above. This fallback used to ignore it, which is how
    // a level 1 Squirtle ended up holding SURF: it had no Water move, so the
    // guarantee reached past every check and handed it the best one in the
    // table. A creature with no STAB it can legally use simply has none yet.
    if (learnMethod(speciesId, i) != LM_LEVEL_UP && lvl < tmLevelFor(m)) continue;
    int16_t sc = (int16_t)m.power;
    if (m.effect == EF_RECHARGE) sc -= 35;
    if (m.effect == EF_RECOIL) sc -= 20;
    if (sc > bestSc) { bestSc = sc; best = mv; }
  }
  if (best) moves[MOVE_SLOTS - 1] = best;
}

// Queues every level-up move unlocked since the last check. A free slot is
// filled silently -- the games do not ask when there is room either -- and only
// a full moveset produces an offer the player has to answer.
void Pet::checkLearnGates() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint8_t lvl = level();
  if (lvl <= lastLearnLevel) return;
  uint16_t n = learnCount(speciesId);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(speciesId, i);
    if (learnMethod(speciesId, i) != LM_LEVEL_UP ||
        at <= lastLearnLevel || at > lvl) continue;
    MoveId mv = learnMove(speciesId, i);
    if (!mv || mv >= ::moveCount() || knowsMove(mv)) continue;
    int freeSlot = -1;
    for (int s = 0; s < MOVE_SLOTS; s++)
      if (!moves[s]) { freeSlot = s; break; }
    if (freeSlot >= 0) { moves[freeSlot] = mv; continue; }
    if (learnQCount >= sizeof(learnQueue) / sizeof(learnQueue[0])) continue;
    bool dup = false;
    for (uint8_t q = 0; q < learnQCount; q++)
      if (learnQueue[q] == mv) dup = true;
    if (!dup) learnQueue[learnQCount++] = mv;
  }
  lastLearnLevel = lvl;
  pendingSave = true;
}

static void popLearn(MoveId *q, uint8_t &n) {
  if (!n) return;
  for (uint8_t i = 0; i + 1 < n; i++) q[i] = q[i + 1];
  q[--n] = 0;
}

void Pet::acceptLearn(uint8_t slot) {
  if (!learnQCount || slot >= MOVE_SLOTS) return;
  moves[slot] = learnQueue[0];
  popLearn(learnQueue, learnQCount);
  save();
}

void Pet::declineLearn() {
  popLearn(learnQueue, learnQCount);
  save();
}

uint8_t Pet::pendingLearnables(MoveId *out, uint8_t max) const {
  if (isEgg() || !out || !max) return 0;
  uint8_t lvl = level(), w = 0;
  uint16_t n = learnCount(speciesId);
  for (uint16_t i = 0; i < n && w < max; i++) {
    if (learnMethod(speciesId, i) == LM_LEVEL_UP && learnLevel(speciesId, i) > lvl) continue;
    if (moveUnlockLevel(speciesId, i) > lvl) continue;
    MoveId mv = learnMove(speciesId, i);
    if (knowsMove(mv)) continue;
    bool dup = false;                     // do not offer the same move twice
    for (uint8_t j = 0; j < w; j++)
      if (out[j] == mv) { dup = true; break; }
    if (!dup) out[w++] = mv;
  }
  return w;
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

uint16_t Pet::registeredCount() const {
  uint16_t n = 0;
  for (int i = 1; i <= dexCount(); i++)
    if (isRegistered(i)) n++;
  return n;
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
  registerSpecies(speciesId, shiny);  // criado = registrado en la pokedex
  // Start empty: checkLearnGates() fills the level-1 moves. Seeding from TMs
  // instead would hand a newborn FIRE BLAST, which no level 1 creature knows.
  for (int i = 0; i < MOVE_SLOTS; i++) moves[i] = 0;
  learnQCount = 0;
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
    if (dexValid(target) && !isRegistered(target)) options[optionCount++] = target;
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
  registerSpecies(speciesId, shiny);
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
    if (score > gameHi) gameHi = score;  // nuevo record
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
    if (hits > spdHi) spdHi = hits;
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
    if (hits > strHi) strHi = hits;
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
  save();
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
  ticksSinceSave = 0;
  pendingSave = false;
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
  prefs.putUChar("mvlv", lastLearnLevel);
  prefs.putUChar("avtr", avatar);
  prefs.putUChar("reg", region);
  prefs.putBytes("badgX", badgesX, sizeof(badgesX));
  prefs.putBytes("badhX", badgesHardX, sizeof(badgesHardX));
  prefs.putBytes("eggR", eggByRegion, sizeof(eggByRegion));
  prefs.putString("tnam", trainerName);
  prefs.putBool("froz", frozen);
  prefs.putBool("dead", dead);
  prefs.putUShort("badg", badges);
  prefs.putUShort("badh", badgesHard);
  prefs.putBool("bk", berryKnown);
  prefs.putBool("shy", shiny);
  prefs.putBool("spkl", shiny);  // legacy mirror for older firmware
  prefs.putBool("gmax", gigantamaxFactor);
  prefs.putBool("eshy", eggShiny);
  prefs.putBool("stpk", starterPick);
  prefs.putUChar("slpa", sleepAuto);
  prefs.putBytes("dexsh", dexShinyReg, sizeof(dexShinyReg));
  prefs.putUInt("age", ageMinutes);
  prefs.putUInt("raise", raisedMinutes);
  prefs.putShort("dexn", speciesId);
  prefs.putShort("eggT2", eggTarget);
  prefs.putUChar("crack", eggTaps);
  prefs.putUChar("mist", careMistakes);
  prefs.putBool("sleep", sleeping);
  prefs.putUChar("lend", lastEnd);
  if (lastSeenEpoch) prefs.putUInt("seen", lastSeenEpoch);
  prefs.putBytes("dexreg", dexReg, sizeof(dexReg));
  prefs.putUShort("strk", streak);
  prefs.putUShort("bstrk", bestStreak);
  prefs.putUChar("wrbon", wildRareBonus);
  prefs.putUInt("cday", lastCareDay);
  prefs.putUChar("bond", bond);
  prefs.putUShort("medal", medals);
  prefs.putUShort("tmedal", totalMedals);
  prefs.putUShort("mstone", lastMilestone);
  prefs.putUShort("ghi", gameHi);
  prefs.putUShort("shi", strHi);
  prefs.putUShort("qhi", spdHi);
  prefs.putString("nick", nick);
  if (roster) roster->captureActive(*this);
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
  prefs.getBytes("dexsh", dexShinyReg, sizeof(dexShinyReg));
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
  prefs.getBytes("dexreg", dexReg, sizeof(dexReg));
  streak = prefs.getUShort("strk", 0);
  bestStreak = prefs.getUShort("bstrk", 0);
  wildRareBonus = prefs.getUChar("wrbon", 0);
  if (wildRareBonus > WILD_RARE_BONUS_MAX) wildRareBonus = WILD_RARE_BONUS_MAX;
  lastCareDay = prefs.getUInt("cday", 0);
  bond = prefs.getUChar("bond", 0);
  medals = prefs.getUShort("medal", 0);
  totalMedals = prefs.getUShort("tmedal", 0);
  lastMilestone = prefs.getUShort("mstone", 0);
  gameHi = prefs.getUShort("ghi", 0);
  strHi = prefs.getUShort("shi", 0);
  spdHi = prefs.getUShort("qhi", 0);
  prefs.getString("nick", nick, sizeof(nick));
  if (prefs.getBytesLength("mvs") == sizeof(moves)) {
    prefs.getBytes("mvs", moves, sizeof(moves));
  }
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (moves[i] >= ::moveCount()) moves[i] = 0;
  lastLearnLevel = prefs.getUChar("mvlv", 0);
  frozen = prefs.getBool("froz", false);
  dead = prefs.getBool("dead", false) && speciesId >= 1;
  avatar = prefs.getUChar("avtr", 0);
  prefs.getBytes("badgX", badgesX, sizeof(badgesX));
  prefs.getBytes("badhX", badgesHardX, sizeof(badgesHardX));
  region = prefs.getUChar("reg", regionAll());
  if (region >= regionCount()) region = regionAll();
  prefs.getBytes("eggR", eggByRegion, sizeof(eggByRegion));
  prefs.getString("tnam", trainerName, sizeof(trainerName));
  if (avatar >= AVATAR_COUNT) avatar = 0;
  badges = prefs.getUShort("badg", 0);
  badgesHard = prefs.getUShort("badh", 0);
  learnQCount = 0;      // rebuilt from lastLearnLevel by the next tick
  checkLearnGates();
  if (speciesId >= 1) registerSpecies(speciesId, shiny);
}
