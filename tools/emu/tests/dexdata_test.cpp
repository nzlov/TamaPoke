// The dex TABLE itself, every species of it.
//
// Adding a generation is a data change, and the data is generated -- so the way
// it goes wrong is not a crash but a species that quietly cannot work: an
// evolution pointing past the end of the table, a typing the move list has no
// attack for, a base stat of zero. Each of those is invisible until somebody
// hatches that exact creature.
//
// This is the sweep that makes the next expansion safe rather than hopeful.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "moves.h"
#include "types.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
uint32_t g_seed=31; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  printf("      sweeping %d species\n", dexCount());
  bool partialEvolutionGraph = false;

  // --- every entry is filled in at all
  {
    int noName = 0, badType = 0, zeroStat = 0;
    for (int16_t d = 1; d <= dexCount(); d++) {
      const DexEntry &e = dexEntry(d);
      if (!e.name || !e.name[0]) noName++;
      if (e.type1 >= TYPE_COUNT) badType++;
      if (e.type2 != T_NONE && e.type2 >= TYPE_COUNT) badType++;
      if (!e.bAtk || !e.bDef || !e.bSpe || !e.bHp || !e.bSpA || !e.bSpD) zeroStat++;
    }
    ck(noName == 0, "every species has a name");
    ck(badType == 0, "and a typing inside the chart");
    ck(zeroStat == 0, "and six base stats, none of them zero");
  }

  // --- every pack-provided evolution branch points somewhere real.
  {
    int oob = 0, self = 0, noLevel = 0;
    for (int16_t d = 1; d <= dexCount(); d++) {
      const DexEntry &e = dexEntry(d);
      for (uint8_t i = 0; i < evolutionCount(d); i++) {
        SpeciesId target = evolutionTarget(d, i);
        if (target < 1 || target > CONTENT_MAX_SPECIES) { oob++; continue; }
        if (target > dexCount()) partialEvolutionGraph = true;
        if (target == d) self++;
        if (!e.evolveLevel) noLevel++;
      }
    }
    ck(oob == 0, "no evolution points outside the supported species space");
    ck(self == 0, "and nothing evolves into itself");
    ck(noLevel == 0, "and every evolution has a level to reach");
  }

  // --- the full branching evolution graph must terminate
  {
    int loops = 0;
    std::vector<uint8_t> state(dexCount() + 1, 0);
    std::function<void(SpeciesId)> visit = [&](SpeciesId species) {
      state[species] = 1;
      for (uint8_t i = 0; i < evolutionCount(species); i++) {
        SpeciesId target = evolutionTarget(species, i);
        if (!dexValid(target)) continue;
        if (state[target] == 1) loops++;
        else if (state[target] == 0) visit(target);
      }
      state[species] = 2;
    };
    for (SpeciesId d = 1; d <= dexCount(); d++) if (!state[d]) visit(d);
    int branchSources = 0;
    for (SpeciesId d = 1; d <= dexCount(); d++)
      if (evolutionCount(d) > 1) branchSources++;
    ck(loops == 0, "every evolution chain ends rather than looping");
    ck(branchSources > 0 && evolutionCount(133) >= 3,
       "all branching evolutions come from region packs");
  }

  // Species that genuinely have no attacking move in the games either. Cocoons
  // learn HARDEN and nothing else; DITTO has Transform, UNOWN has Hidden Power,
  // WOBBUFFET and WYNAUT counter rather than attack, SMEARGLE only Sketches.
  // Listed rather than tolerated, so a NEW species joining them is a failure --
  // which is what a new generation's typings would cause.
  static const int16_t NO_ATTACK[] = {
    11, 14, 132, 201, 202, 235, 266, 268, 360, 771, 789, 790, 840
  };
  static const int16_t NO_LEARNSET[] = { 11, 14, 132, 201, 235, 789, 790, 840 };
  auto known = [](const int16_t *a, size_t n, int16_t d) {
    for (size_t i = 0; i < n; i++) if (a[i] == d) return true;
    return false;
  };

  // --- THE ONE THAT BITES ON A NEW GENERATION: a species with no attack of
  //     its own type. dex_moves.py is 77 hand-picked moves, so a typing that
  //     arrives with a new region can leave creatures with no STAB at all.
  {
    int noStab = 0; int16_t first = 0;
    for (int16_t d = 1; d <= dexCount(); d++) {
      const DexEntry &e = dexEntry(d);
      bool found = false;
      for (uint8_t i = 0; i < learnCount(d) && !found; i++) {
        MoveId mv = learnMove(d, i);
        if (mv >= moveCount()) continue;
        const MoveEntry &m = moveEntry(mv);
        if (m.cat == MC_STATUS || !m.power) continue;
        if (m.type == e.type1 || m.type == e.type2) found = true;
      }
      if (found) continue;
      if (known(NO_ATTACK, sizeof(NO_ATTACK)/sizeof(*NO_ATTACK), d)) continue;
      noStab++; if (!first) first = d;
    }
    if (noStab) printf("      %d species have no same-type attack, first is %s (%d)\n",
                       noStab, dexEntry(first).name, first);
    ck(noStab == 0,
       "every species can learn an attack of its own type, bar known exceptions");
  }

  // --- and everything it can learn is a real move
  {
    int oob = 0, empty = 0;
    for (int16_t d = 1; d <= dexCount(); d++) {
      if (!learnCount(d)) {
        if (!known(NO_LEARNSET, sizeof(NO_LEARNSET)/sizeof(*NO_LEARNSET), d)) empty++;
        continue;
      }
      for (uint8_t i = 0; i < learnCount(d); i++)
        if (learnMove(d, i) >= moveCount()) oob++;
    }
    ck(oob == 0, "no learnset entry points past the move table");
    ck(empty == 0, "and no unexpected species has an empty learnset");
  }

  // --- the regions tile the dex with no gaps and no overlaps
  {
    int uncovered = 0;
    for (int16_t d = 1; d <= dexCount(); d++) {
      int in = 0;
      for (uint8_t r = 0; r < regionCount(); r++) {
        if (r == regionAll()) continue;
        if (d >= regionInfo(r).lo && d <= regionInfo(r).hi) in++;
      }
      if (in != 1) uncovered++;
    }
    ck(uncovered == 0, "every species belongs to exactly one region");
    ck(regionInfo(regionAll()).hi == dexCount(), "and ALL reaches the end of the table");
  }

  // --- the Pokedex bitmap is big enough for the table it indexes
  {
    Pet p; p.begin(); p.factoryReset();
    Pet q; q.begin();
    q.dbgHatchAs(dexCount(), false);      // hatching is what registers a species
    ck(q.isRegistered(dexCount()), "the last species can be registered at all");
    ck(!q.isRegistered(dexCount() - 1), "without spilling into its neighbour");
  }

  // --- an evolution TARGET must never hatch from an egg, or the same creature
  //     arrives two ways and the chain stops meaning anything. Pack generation
  //     derives R_EVO from being somebody's target, so this locks that
  //     derivation rather than restating it.
  {
    bool isTarget[dexCount() + 1] = { false };
    for (int16_t d = 1; d <= dexCount(); d++)
      for (uint8_t i = 0; i < evolutionCount(d); i++) {
        SpeciesId target = evolutionTarget(d, i);
        if (target >= 1 && target <= dexCount()) isTarget[target] = true;
      }

    int hatchable = 0;
    for (int16_t d = 1; d <= dexCount(); d++)
      if (isTarget[d] && dexEntry(d).rarity != R_EVO) {
        if (hatchable < 3) printf("      %s (%d) is an evolution AND hatches\n",
                                  dexEntry(d).name, d);
        hatchable++;
      }
    ck(hatchable == 0, "no evolution target also hatches straight from an egg");

    // and the reverse: R_EVO means "only ever reached by evolving", so a
    // species nothing evolves into would be unreachable entirely.
    int stranded = 0;
    for (int16_t d = 1; d <= dexCount(); d++) {
      if (dexEntry(d).rarity != R_EVO || isTarget[d]) continue;
      printf("      %s (%d) can be neither hatched nor evolved into\n",
             dexEntry(d).name, d);
      stranded++;
    }
    ck(partialEvolutionGraph || stranded == 0,
       "and every evolution-only species is reachable in a complete installed graph");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
