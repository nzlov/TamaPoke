#include <cstdio>
#include <cstring>

#include "Arduino.h"
#include "Preferences.h"
#include "content.h"
#include "quiz.h"

uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int failures = 0;
static void check(bool condition, const char *message) {
  printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
  if (!condition) failures++;
}

int main() {
  check(contentValidatePackFile(QUIZ_READER_FIXTURE) == CONTENT_PACK_VALID,
        "indexed question pack passes full firmware validation");
  contentBegin();
  check(contentChoiceQuestionCount("zh-CN") == 2 && contentChoiceQuestionCount("en") == 1 &&
        contentChoiceQuestionCount("fr") == 0,
        "locale spans count questions without loading records");
  ContentChoiceQuestion first, second;
  check(contentChoiceQuestionAt("zh-CN", 0, first) &&
        contentChoiceQuestionAt("zh-CN", 1, second) &&
        strcmp(first.id, "zh-1") == 0 && strcmp(second.id, "zh-2") == 0,
        "direct indexes address sorted variable-size records");
  check(first.optionCount == 3 && first.correctIndex == 2 && strcmp(first.stem, "第一题") == 0 &&
        strcmp(first.options[2], "C") == 0,
        "one indexed read decodes the complete selected question");
  check(!contentChoiceQuestionAt("zh-CN", 2, first),
        "out-of-range question indexes are rejected");

  QuizConfig choiceOnly;
  choiceOnly.questionTypes = QUIZ_TYPE_CHOICE;
  QuizRuntime choiceRuntime;
  choiceRuntime.config = choiceOnly;
  check(choiceRuntime.begin("zh-CN") && choiceRuntime.kind == QUIZ_QUESTION_CHOICE,
        "choice-only mode selects an installed question and never arithmetic");

  QuizConfig arithmeticOnly;
  arithmeticOnly.questionTypes = QUIZ_TYPE_ARITHMETIC;
  QuizRuntime arithmeticRuntime;
  arithmeticRuntime.config = arithmeticOnly;
  check(arithmeticRuntime.begin("zh-CN") &&
        arithmeticRuntime.kind == QUIZ_QUESTION_ARITHMETIC,
        "arithmetic-only mode ignores an installed question bank");

  QuizConfig weighted;
  weighted.questionTypes = QUIZ_TYPE_CHOICE | QUIZ_TYPE_ARITHMETIC;
  weighted.choiceWeight = 100;
  QuizRuntime weightedRuntime;
  weightedRuntime.config = weighted;
  check(weightedRuntime.begin("zh-CN") && weightedRuntime.kind == QUIZ_QUESTION_CHOICE,
        "enabling both types keeps the configured choice ratio");
  return failures ? 1 : 0;
}
