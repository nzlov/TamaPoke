// Gym badges belong to the player, but the IV reward belongs to the creature.
// A creature may claim each real gym once, across both difficulty ladders.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  // --- one unclaimed gym raises one of the four IVs by exactly one
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=8;
    uint8_t which=9;
    GymIvReward got = p.rewardGymIv(0, 0, which);
    ck(got==GYM_IV_GAINED, "an unclaimed gym grants an IV");
    ck(which<4, "into one of the four innate stats");
    ck(p.ivAtk+p.ivDef+p.ivSpe+p.ivHp==33, "and it raises exactly one point");
    ck(p.gymIvClaimed(0, 0), "the creature remembers that gym");
    ck(p.gymIvRewardAt(0, 0)==which+1, "the byte records which IV increased");
  }

  // --- the byte encoding is the source of truth: 2 means DEF +1
  {
    Pet p; p.begin(); p.newEgg(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivSpe=p.ivHp=31; p.ivDef=20;
    uint8_t which=0;
    ck(p.rewardGymIv(0, 0, which)==GYM_IV_GAINED && which==1 && p.ivDef==21,
       "Kanto gym 0 can grant exactly one defence IV");
    ck(p.gymIvRewards[0]==2, "byte 0 equals 2 for that defence-IV reward");
  }

  // --- the same creature cannot claim the same gym again, even on hard
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=8;
    uint8_t which=0;
    ck(p.rewardGymIv(0, 3, which)==GYM_IV_GAINED, "the first win claims it");
    uint8_t sum=p.ivAtk+p.ivDef+p.ivSpe+p.ivHp;
    ck(p.rewardGymIv(0, 3, which)==GYM_IV_NONE, "a rematch gives no second IV");
    ck(p.ivAtk+p.ivDef+p.ivSpe+p.ivHp==sum, "switching difficulty cannot duplicate it");
  }

  // --- capped IVs are skipped rather than wasting the claim
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=31; p.ivHp=20;
    uint8_t which=0;
    ck(p.rewardGymIv(0, 2, which)==GYM_IV_GAINED, "a creature with one open IV gains it");
    ck(which==3 && p.ivHp==21, "the reward lands on the only IV below 31");
  }

  // --- a perfect creature still records the claim, but cannot exceed 31
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
    uint8_t which=0;
    ck(p.rewardGymIv(0, 1, which)==GYM_IV_MAXED, "a perfect creature reports no headroom");
    ck(p.gymIvClaimed(0, 1), "the completed gym is still remembered");
    ck(p.gymIvRewardAt(0, 1)==GYM_IV_REWARD_MAXED,
       "a perfect claim has its own byte value");
  }

  // --- with room everywhere the random choice reaches all four stats
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    bool hit[4] = {false,false,false,false};
    for (int i = 0; i < 72; i++) {
      p.dbgHatchAs(6, false);
      p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=8;
      uint8_t which=0;
      p.rewardGymIv(0, (uint8_t)(i % GYM_IV_GYMS_PER_REGION), which);
      if (which<4) hit[which]=true;
    }
    ck(hit[0]&&hit[1]&&hit[2]&&hit[3], "different gyms can reach all four IVs");
  }

  // --- only real gyms qualify; elite/champion rows and eggs do not
  {
    Pet p; p.begin(); p.newEgg();
    uint8_t which=0;
    ck(p.rewardGymIv(0, 0, which)==GYM_IV_NONE, "an egg cannot claim a gym reward");
    p.dbgHatchAs(6,false);
    uint8_t elite = regionBattleInfo(0).gymCount;
    ck(p.rewardGymIv(0, elite, which)==GYM_IV_NONE, "elite rows do not grant gym IVs");
  }

  // --- the per-creature claim survives a reload and resets for a new hatch
  {
    Pet p; p.begin(); p.dbgHatchAs(25,false);
    uint8_t which=0;
    p.winBadge(0, 6, false);
    p.rewardGymIv(0, 6, which);
    Pet q; q.begin();
    ck(q.gymIvClaimed(0, 6), "the claim is persisted with the current creature");
    q.newEgg(); q.dbgHatchAs(25,false);
    ck(!q.gymIvClaimed(0, 6), "a different creature gets its own claim map");
    ck(q.hasBadge(0, 6, false), "the player's badge remains global across creatures");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
