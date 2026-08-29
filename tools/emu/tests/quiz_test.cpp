#include <cstdio>
#include <cstring>

#include "Arduino.h"
#include "Preferences.h"
#include "Wire.h"
#include "pet.h"
#include "quiz.h"

uint32_t g_seed = 0x5155495A;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
void sfxPlay(uint8_t) {}
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

static int failures = 0;
static void check(bool condition, const char *message) {
  printf("    %s %s\n", condition ? "PASS" : "FAIL", message);
  if (!condition) failures++;
}

int main() {
  QuizConfig defaults;
  check(defaults.questionTypes == 0,
        "questions are disabled by default");

  QuizConfig config;
  config.choiceWeight = 0;
  config.questionTypes = QUIZ_TYPE_ARITHMETIC;
  check(quizConfigValid(config), "default arithmetic configuration is valid");

  char line[128];
  check(quizConfigFormat(config, line, sizeof(line)) > 0,
        "configuration has a stable serial representation");
  QuizConfig parsed;
  check(quizConfigParse(strchr(line, '\t') + 1, parsed) &&
        parsed.timeSeconds == 30 && parsed.operators == 15 && parsed.choiceWeight == 0 &&
        parsed.questionTypes == QUIZ_TYPE_ARITHMETIC,
        "configuration parses back from the serial fields");

  QuizConfig disabled = config;
  disabled.questionTypes = 0;
  QuizRuntime disabledRuntime;
  disabledRuntime.config = disabled;
  check(quizConfigValid(disabled) && !disabledRuntime.begin("zh-CN"),
        "disabling every question type skips the question");
  check(quizConfigSave(disabled) && quizConfigLoad().questionTypes == 0,
        "the fully disabled state persists instead of reverting to defaults");

  QuizConfig choiceOnly = config;
  choiceOnly.questionTypes = QUIZ_TYPE_CHOICE;
  QuizRuntime choiceOnlyRuntime;
  choiceOnlyRuntime.config = choiceOnly;
  check(!choiceOnlyRuntime.begin("zh-CN"),
        "choice-only mode never falls back to arithmetic without a question bank");

  QuizRuntime runtime;
  runtime.config = config;
  check(runtime.begin("zh-CN") &&
        runtime.kind == QUIZ_QUESTION_ARITHMETIC && runtime.expression[0] && runtime.expected[0],
        "an arithmetic question is generated without a question pack");

  QuizConfig narrow = config;
  narrow.operators = QUIZ_OP_DIVIDE;
  narrow.operandCount = 4;
  narrow.operandDigits = narrow.answerDigits = 1;
  narrow.flags = QUIZ_DIVISION_EXACT;
  QuizRuntime narrowRuntime;
  narrowRuntime.config = narrow;
  check(narrowRuntime.begin("zh-CN") &&
        narrowRuntime.kind == QUIZ_QUESTION_ARITHMETIC,
        "a valid narrow generation rule cannot leave care blocked");
  runtime.markRendered(1000);
  snprintf(runtime.expected, sizeof(runtime.expected), "1/2");
  snprintf(runtime.input, sizeof(runtime.input), "2/4");
  check(runtime.submit(16000), "an equivalent fraction is accepted as input");
  uint8_t percent = 0;
  bool correct = false;
  check(!runtime.takeSettlement(17199, percent, correct) &&
        runtime.takeSettlement(17200, percent, correct) && correct && percent == 50,
        "the 50 percent result settles only after answer feedback");

  Pet pet;
  pet.speciesId = 1;
  pet.fullness = 0;
  pet.joy = 0;
  pet.energy = 100;
  pet.hygiene = 20;
  check(pet.settleCare({ CARE_ACTION_FEED_BERRY, 0 }, 50) == 13 && pet.fullness == 13,
        "feeding scales the existing 25-point berry reward with rounding");
  check(pet.settleCare({ CARE_ACTION_CLEAN, 0 }, 50) == 40 && pet.hygiene == 60,
        "cleaning scales the missing distance to 100");
  pet.joy = 20;
  check(pet.settleCare({ CARE_ACTION_CARESS, 0 }, 50) == 3 && pet.joy == 23,
        "caressing scales its positive effect through the same care boundary");

  pet.trAtk = 0;
  pet.energy = 100;
  pet.fullness = 100;
  check(pet.settleCare({ CARE_ACTION_TRAIN_STRENGTH, 40 }, 0) == 0 &&
        pet.trAtk == 0 && pet.energy == 88 && pet.fullness == 95 && player.strHi == 40,
        "a failed training answer keeps costs and the real session record");
  pet.ivAtk = pet.ivDef = pet.ivSpe = 31;
  check(pet.settleCare({ CARE_ACTION_TRAIN_STRENGTH, 40 }, 50) == 8 && pet.trAtk == 8,
        "training score and answer percentage scale the stat-cap reward together");
  pet.trAtk = 0;
  check(pet.settleCare({ CARE_ACTION_TRAIN_STRENGTH, 72 }, 100) == 30 && pet.trAtk == 30,
        "a full strength session gains 30 percent of the attack training cap");
  check(pet.settleCare({ CARE_ACTION_TRAIN_SPEED, 36 }, 100) == 30 && pet.trSpe == 30,
        "a full speed session gains 30 percent of the speed training cap");
  check(pet.settleCare({ CARE_ACTION_PLAY, 36 }, 100) == 30 && pet.trDef == 30,
        "a full defence session gains 30 percent of the defence training cap");
  pet.ivDef = 16;
  pet.trDef = 0;
  check(pet.settleCare({ CARE_ACTION_PLAY, 36 }, 100) == 25 && pet.trDef == 25,
        "integer training rewards never exceed 30 percent of a non-round cap");
  pet.trAtk = 90;
  check(pet.settleCare({ CARE_ACTION_TRAIN_STRENGTH, 72 }, 100) == 10 && pet.trAtk == 100,
        "training gain stops at the stat training cap");

  QuizRuntime timeout;
  timeout.config = config;
  check(timeout.begin("en-US"), "a timeout question starts");
  timeout.markRendered(2000);
  timeout.update(32000);
  check(timeout.timedOut && !timeout.takeSettlement(33199, percent, correct) &&
        timeout.takeSettlement(33200, percent, correct) &&
        !correct && percent == 0, "timeout settles as a failed zero-effect answer");

  return failures ? 1 : 0;
}
