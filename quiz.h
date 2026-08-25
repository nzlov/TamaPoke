#pragma once

#include <Arduino.h>

#include "content.h"


enum QuizOperator : uint8_t {
  QUIZ_OP_ADD = 1,
  QUIZ_OP_SUBTRACT = 2,
  QUIZ_OP_MULTIPLY = 4,
  QUIZ_OP_DIVIDE = 8,
};

enum QuizConfigFlag : uint8_t {
  QUIZ_ALLOW_NEGATIVE = 1,
  QUIZ_ALLOW_DECIMALS = 2,
  QUIZ_ALLOW_FRACTIONS = 4,
  QUIZ_ALLOW_PARENTHESES = 8,
  QUIZ_DIVISION_EXACT = 16,
};

enum QuizQuestionType : uint8_t {
  QUIZ_TYPE_CHOICE = 1,
  QUIZ_TYPE_ARITHMETIC = 2,
};

struct QuizConfig {
  uint16_t timeSeconds = 30;
  uint8_t operators = QUIZ_OP_ADD | QUIZ_OP_SUBTRACT | QUIZ_OP_MULTIPLY | QUIZ_OP_DIVIDE;
  uint8_t operandCount = 3;
  uint8_t operandDigits = 2;
  uint8_t answerDigits = 3;
  uint8_t decimalPlaces = 2;
  uint8_t fractionDigits = 2;
  uint8_t flags = QUIZ_ALLOW_DECIMALS | QUIZ_ALLOW_FRACTIONS | QUIZ_ALLOW_PARENTHESES;
  uint8_t parenthesisDepth = 1;
  uint8_t choiceWeight = 50;
  uint8_t questionTypes = QUIZ_TYPE_CHOICE | QUIZ_TYPE_ARITHMETIC;
};

bool quizConfigValid(const QuizConfig &config);
QuizConfig quizConfigLoad();
bool quizConfigSave(const QuizConfig &config);
size_t quizConfigFormat(const QuizConfig &config, char *out, size_t capacity);
bool quizConfigParse(const char *text, QuizConfig &config);

enum QuizQuestionKind : uint8_t {
  QUIZ_QUESTION_NONE = 0,
  QUIZ_QUESTION_CHOICE,
  QUIZ_QUESTION_ARITHMETIC,
};

class QuizRuntime {
public:
  QuizConfig config;
  QuizQuestionKind kind = QUIZ_QUESTION_NONE;
  ContentChoiceQuestion choice;
  char expression[160] = {};
  char expected[32] = {};
  char input[32] = {};
  uint32_t startedAt = 0;
  uint32_t feedbackUntil = 0;
  uint32_t lastChoiceHash = 0;
  uint8_t effectPercent = 0;
  uint8_t selectedOption = 0xFF;
  uint8_t scrollLine = 0;
  uint8_t maxScrollLine = 0;
  bool active = false;
  bool timerStarted = false;
  bool answered = false;
  bool correct = false;
  bool timedOut = false;
  bool settlementPending = false;

  void loadConfig() { config = quizConfigLoad(); }
  bool begin(const char *locale);
  void markRendered(uint32_t now);
  void update(uint32_t now);
  uint32_t remainingMs(uint32_t now) const;
  bool choose(uint8_t option, uint32_t now);
  bool append(char value);
  void erase();
  void clearInput();
  bool submit(uint32_t now);
  bool takeSettlement(uint32_t now, uint8_t &percent, bool &wasCorrect);

private:
  bool beginChoice(const char *locale);
  bool beginArithmetic();
  void finish(bool wasCorrect, uint32_t now);
  uint8_t percentAt(uint32_t now) const;
};
