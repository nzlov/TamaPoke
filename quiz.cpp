#include "quiz.h"

#include <Preferences.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


namespace {

struct Rational {
  int32_t numerator = 0;
  int32_t denominator = 1;
};

static int32_t greatestCommonDivisor(int32_t left, int32_t right) {
  if (left < 0) left = -left;
  if (right < 0) right = -right;
  while (right) {
    int32_t remainder = left % right;
    left = right;
    right = remainder;
  }
  return left ? left : 1;
}

static bool normalize(int64_t numerator, int64_t denominator, Rational &out) {
  if (!denominator) return false;
  if (denominator < 0) { numerator = -numerator; denominator = -denominator; }
  if (numerator < INT32_MIN || numerator > INT32_MAX || denominator > INT32_MAX) return false;
  int32_t divisor = greatestCommonDivisor((int32_t)numerator, (int32_t)denominator);
  out.numerator = (int32_t)numerator / divisor;
  out.denominator = (int32_t)denominator / divisor;
  return true;
}

static bool applyOperator(const Rational &left, const Rational &right, uint8_t operation,
                          Rational &out) {
  switch (operation) {
    case QUIZ_OP_ADD:
      return normalize((int64_t)left.numerator * right.denominator +
                       (int64_t)right.numerator * left.denominator,
                       (int64_t)left.denominator * right.denominator, out);
    case QUIZ_OP_SUBTRACT:
      return normalize((int64_t)left.numerator * right.denominator -
                       (int64_t)right.numerator * left.denominator,
                       (int64_t)left.denominator * right.denominator, out);
    case QUIZ_OP_MULTIPLY:
      return normalize((int64_t)left.numerator * right.numerator,
                       (int64_t)left.denominator * right.denominator, out);
    case QUIZ_OP_DIVIDE:
      return right.numerator && normalize((int64_t)left.numerator * right.denominator,
                                          (int64_t)left.denominator * right.numerator, out);
  }
  return false;
}

static uint32_t powerOfTen(uint8_t digits) {
  uint32_t result = 1;
  while (digits--) result *= 10;
  return result;
}

static uint8_t digitCount(uint32_t value) {
  uint8_t digits = 1;
  while (value >= 10) { value /= 10; digits++; }
  return digits;
}

static char operatorCharacter(uint8_t operation) {
  if (operation == QUIZ_OP_ADD) return '+';
  if (operation == QUIZ_OP_SUBTRACT) return '-';
  if (operation == QUIZ_OP_MULTIPLY) return '*';
  return '/';
}

static uint8_t randomOperator(uint8_t mask) {
  uint8_t enabled[4], count = 0;
  const uint8_t all[] = { QUIZ_OP_ADD, QUIZ_OP_SUBTRACT, QUIZ_OP_MULTIPLY, QUIZ_OP_DIVIDE };
  for (uint8_t operation : all) if (mask & operation) enabled[count++] = operation;
  return count ? enabled[random(count)] : 0;
}

static bool appendText(char *out, size_t capacity, const char *value) {
  size_t used = strlen(out), added = strlen(value);
  if (used + added >= capacity) return false;
  memcpy(out + used, value, added + 1);
  return true;
}

static bool appendOperation(char *out, size_t capacity, uint8_t operation) {
  char text[] = { ' ', operatorCharacter(operation), ' ', 0 };
  return appendText(out, capacity, text);
}

static bool decimalText(const Rational &value, uint8_t places, char *out, size_t capacity) {
  int32_t denominator = value.denominator;
  while ((denominator % 2) == 0) denominator /= 2;
  while ((denominator % 5) == 0) denominator /= 5;
  if (denominator != 1) return false;
  bool negative = value.numerator < 0;
  uint32_t numerator = negative ? (uint32_t)-(int64_t)value.numerator : (uint32_t)value.numerator;
  uint32_t whole = numerator / (uint32_t)value.denominator;
  uint32_t remainder = numerator % (uint32_t)value.denominator;
  char fraction[8] = {};
  uint8_t used = 0;
  while (remainder && used < places) {
    remainder *= 10;
    fraction[used++] = (char)('0' + remainder / (uint32_t)value.denominator);
    remainder %= (uint32_t)value.denominator;
  }
  if (remainder) return false;
  while (used && fraction[used - 1] == '0') used--;
  fraction[used] = 0;
  int written = used ? snprintf(out, capacity, "%s%lu.%s", negative ? "-" : "",
                                (unsigned long)whole, fraction)
                     : snprintf(out, capacity, "%s%lu", negative ? "-" : "",
                                (unsigned long)whole);
  return written > 0 && (size_t)written < capacity;
}

static bool answerText(const Rational &value, const QuizConfig &config,
                       char *out, size_t capacity) {
  uint32_t absolute = value.numerator < 0 ? (uint32_t)-(int64_t)value.numerator
                                         : (uint32_t)value.numerator;
  if (value.denominator == 1) {
    if (digitCount(absolute) > config.answerDigits) return false;
    int written = snprintf(out, capacity, "%ld", (long)value.numerator);
    return written > 0 && (size_t)written < capacity;
  }
  uint32_t whole = absolute / (uint32_t)value.denominator;
  if ((config.flags & QUIZ_ALLOW_DECIMALS) && digitCount(whole) <= config.answerDigits &&
      decimalText(value, config.decimalPlaces, out, capacity)) return true;
  if (!(config.flags & QUIZ_ALLOW_FRACTIONS) ||
      digitCount(absolute) > config.fractionDigits ||
      digitCount((uint32_t)value.denominator) > config.fractionDigits) return false;
  int written = snprintf(out, capacity, "%ld/%ld", (long)value.numerator,
                         (long)value.denominator);
  return written > 0 && (size_t)written < capacity;
}

static bool operand(uint8_t mode, const QuizConfig &config, Rational &value,
                    char *display, size_t capacity) {
  uint32_t maximum = powerOfTen(config.operandDigits) - 1;
  if (mode == 0) {
    value.numerator = (int32_t)random(1, maximum + 1);
    value.denominator = 1;
    snprintf(display, capacity, "%ld", (long)value.numerator);
    return true;
  }
  if (mode == 1) {
    uint32_t scale = powerOfTen(config.decimalPlaces);
    uint32_t scaledMaximum = maximum * scale;
    Rational raw{ (int32_t)random(1, scaledMaximum + 1), (int32_t)scale };
    if (!normalize(raw.numerator, raw.denominator, value)) return false;
    return decimalText(value, config.decimalPlaces, display, capacity);
  }
  uint32_t fractionMaximum = powerOfTen(config.fractionDigits) - 1;
  int32_t numerator = (int32_t)random(1, fractionMaximum + 1);
  int32_t denominator = (int32_t)random(2, fractionMaximum + 1);
  if (!normalize(numerator, denominator, value)) return false;
  snprintf(display, capacity, "(%ld/%ld)", (long)numerator, (long)denominator);
  return true;
}

static bool generateArithmetic(const QuizConfig &config, char *expression, size_t expressionCapacity,
                               char *expected, size_t expectedCapacity) {
  for (uint8_t attempt = 0; attempt < 120; attempt++) {
    uint8_t modes[3] = { 0, 0, 0 }, modeCount = 1;
    if (config.flags & QUIZ_ALLOW_DECIMALS) modes[modeCount++] = 1;
    if (config.flags & QUIZ_ALLOW_FRACTIONS) modes[modeCount++] = 2;
    uint8_t mode = modes[random(modeCount)];
    Rational values[4];
    char terms[4][32] = {};
    bool valid = true;
    for (uint8_t i = 0; i < config.operandCount; i++)
      if (!operand(mode, config, values[i], terms[i], sizeof(terms[i]))) valid = false;
    if (!valid) continue;

    expression[0] = 0;
    Rational result = values[0];
    uint8_t operations[3] = {};
    bool useParentheses = (config.flags & QUIZ_ALLOW_PARENTHESES) &&
                          config.parenthesisDepth && config.operandCount > 2 && random(2);
    if (!useParentheses) {
      uint8_t operation = randomOperator(config.operators);
      for (uint8_t i = 0; i + 1 < config.operandCount; i++) operations[i] = operation;
      appendText(expression, expressionCapacity, terms[0]);
      for (uint8_t i = 1; i < config.operandCount && valid; i++) {
        valid = appendOperation(expression, expressionCapacity, operation) &&
                appendText(expression, expressionCapacity, terms[i]) &&
                applyOperator(result, values[i], operation, result);
        if (valid && operation == QUIZ_OP_DIVIDE &&
            (config.flags & QUIZ_DIVISION_EXACT) && result.denominator != 1) valid = false;
      }
    } else {
      uint8_t depth = config.parenthesisDepth;
      if (depth >= config.operandCount) depth = config.operandCount - 1;
      snprintf(expression, expressionCapacity, "%s", terms[0]);
      for (uint8_t i = 1; i <= depth && valid; i++) {
        uint8_t operation = randomOperator(config.operators);
        operations[i - 1] = operation;
        char previous[160];
        snprintf(previous, sizeof(previous), "%s", expression);
        expression[0] = 0;
        valid = appendText(expression, expressionCapacity, "(") &&
                appendText(expression, expressionCapacity, previous) &&
                appendOperation(expression, expressionCapacity, operation) &&
                appendText(expression, expressionCapacity, terms[i]) &&
                appendText(expression, expressionCapacity, ")") &&
                applyOperator(result, values[i], operation, result);
        if (valid && operation == QUIZ_OP_DIVIDE &&
            (config.flags & QUIZ_DIVISION_EXACT) && result.denominator != 1) valid = false;
      }
      uint8_t tailOperation = (config.operators & (QUIZ_OP_ADD | QUIZ_OP_SUBTRACT))
                                ? randomOperator(config.operators & (QUIZ_OP_ADD | QUIZ_OP_SUBTRACT))
                                : operations[depth - 1];
      for (uint8_t i = depth + 1; i < config.operandCount && valid; i++) {
        valid = appendOperation(expression, expressionCapacity, tailOperation) &&
                appendText(expression, expressionCapacity, terms[i]) &&
                applyOperator(result, values[i], tailOperation, result);
        if (valid && tailOperation == QUIZ_OP_DIVIDE &&
            (config.flags & QUIZ_DIVISION_EXACT) && result.denominator != 1) valid = false;
      }
    }
    if (!valid || (!(config.flags & QUIZ_ALLOW_NEGATIVE) && result.numerator < 0) ||
        !answerText(result, config, expected, expectedCapacity)) continue;
    return true;
  }

  // Every valid global rule must still yield a question. Random generation can
  // exhaust its retry budget for very narrow combinations such as exact-only
  // division; this deterministic expression stays inside the same enabled
  // operator, operand-count and digit limits instead of leaving care blocked.
  uint8_t operation = randomOperator(config.operators);
  uint8_t first = operation == QUIZ_OP_SUBTRACT ? config.operandCount : 1;
  snprintf(expression, expressionCapacity, "%u", first);
  Rational result{ first, 1 }, one{ 1, 1 };
  for (uint8_t index = 1; index < config.operandCount; index++) {
    if (!appendOperation(expression, expressionCapacity, operation) ||
        !appendText(expression, expressionCapacity, "1") ||
        !applyOperator(result, one, operation, result)) return false;
  }
  return answerText(result, config, expected, expectedCapacity);
}

static bool parseUnsigned(const char *start, const char *end, int64_t &value) {
  if (start >= end) return false;
  value = 0;
  for (const char *at = start; at < end; at++) {
    if (!isdigit((unsigned char)*at)) return false;
    value = value * 10 + (*at - '0');
    if (value > INT32_MAX) return false;
  }
  return true;
}

static bool parseAnswer(const char *text, Rational &out) {
  if (!text || !*text) return false;
  bool negative = *text == '-';
  if (negative) text++;
  if (!*text) return false;
  const char *slash = strchr(text, '/');
  const char *dot = strchr(text, '.');
  if (slash && (dot || strchr(slash + 1, '/'))) return false;
  if (dot && strchr(dot + 1, '.')) return false;
  int64_t left = 0, right = 1;
  if (slash) {
    if (!parseUnsigned(text, slash, left) || !parseUnsigned(slash + 1, text + strlen(text), right) ||
        !right) return false;
  } else if (dot) {
    int64_t whole = 0, fraction = 0;
    if (dot != text && !parseUnsigned(text, dot, whole)) return false;
    const char *end = text + strlen(text);
    if (!parseUnsigned(dot + 1, end, fraction)) return false;
    uint32_t scale = powerOfTen((uint8_t)(end - dot - 1));
    left = whole * scale + fraction;
    right = scale;
  } else if (!parseUnsigned(text, text + strlen(text), left)) {
    return false;
  }
  if (negative) left = -left;
  return normalize(left, right, out);
}

}  // namespace


bool quizConfigValid(const QuizConfig &config) {
  return config.timeSeconds >= 5 && config.timeSeconds <= 120 &&
         !(config.questionTypes & ~(QUIZ_TYPE_CHOICE | QUIZ_TYPE_ARITHMETIC)) &&
         (!(config.questionTypes & QUIZ_TYPE_ARITHMETIC) || config.operators) &&
         !(config.operators & ~0x0Fu) &&
         config.operandCount >= 2 && config.operandCount <= 4 &&
         config.operandDigits >= 1 && config.operandDigits <= 3 &&
         config.answerDigits >= 1 && config.answerDigits <= 6 &&
         config.decimalPlaces >= 1 && config.decimalPlaces <= 3 &&
         config.fractionDigits >= 1 && config.fractionDigits <= 3 &&
         !(config.flags & ~0x1Fu) && config.parenthesisDepth <= 3 &&
         (!(config.flags & QUIZ_ALLOW_PARENTHESES) || config.parenthesisDepth >= 1) &&
         config.choiceWeight <= 100;
}

QuizConfig quizConfigLoad() {
  QuizConfig config;
  Preferences preferences;
  preferences.begin("quiz", true);
  config.timeSeconds = preferences.getUShort("time", config.timeSeconds);
  config.operators = preferences.getUChar("ops", config.operators);
  config.operandCount = preferences.getUChar("terms", config.operandCount);
  config.operandDigits = preferences.getUChar("opdigits", config.operandDigits);
  config.answerDigits = preferences.getUChar("ansdigits", config.answerDigits);
  config.decimalPlaces = preferences.getUChar("decimals", config.decimalPlaces);
  config.fractionDigits = preferences.getUChar("fracdigits", config.fractionDigits);
  config.flags = preferences.getUChar("flags", config.flags);
  config.parenthesisDepth = preferences.getUChar("pardepth", config.parenthesisDepth);
  config.choiceWeight = preferences.getUChar("choice", config.choiceWeight);
  config.questionTypes = preferences.getUChar("types", config.questionTypes);
  preferences.end();
  return quizConfigValid(config) ? config : QuizConfig{};
}

bool quizConfigSave(const QuizConfig &config) {
  if (!quizConfigValid(config)) return false;
  Preferences preferences;
  preferences.begin("quiz", false);
  preferences.putUShort("time", config.timeSeconds);
  preferences.putUChar("ops", config.operators);
  preferences.putUChar("terms", config.operandCount);
  preferences.putUChar("opdigits", config.operandDigits);
  preferences.putUChar("ansdigits", config.answerDigits);
  preferences.putUChar("decimals", config.decimalPlaces);
  preferences.putUChar("fracdigits", config.fractionDigits);
  preferences.putUChar("flags", config.flags);
  preferences.putUChar("pardepth", config.parenthesisDepth);
  preferences.putUChar("choice", config.choiceWeight);
  preferences.putUChar("types", config.questionTypes);
  preferences.end();
  return true;
}

size_t quizConfigFormat(const QuizConfig &config, char *out, size_t capacity) {
  if (!out || !capacity) return 0;
  int written = snprintf(out, capacity, "QUIZCFG\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u",
                         config.timeSeconds, config.operators, config.operandCount,
                         config.operandDigits, config.answerDigits, config.decimalPlaces,
                         config.fractionDigits, config.flags, config.parenthesisDepth,
                         config.choiceWeight, config.questionTypes);
  return written > 0 && (size_t)written < capacity ? (size_t)written : 0;
}

bool quizConfigParse(const char *text, QuizConfig &config) {
  if (!text) return false;
  unsigned values[11];
  if (sscanf(text, "%u %u %u %u %u %u %u %u %u %u %u",
             &values[0], &values[1], &values[2], &values[3], &values[4],
             &values[5], &values[6], &values[7], &values[8], &values[9],
             &values[10]) != 11) return false;
  if (values[0] > UINT16_MAX)
    return false;
  for (uint8_t index = 1; index < 11; index++)
    if (values[index] > UINT8_MAX) return false;
  QuizConfig parsed;
  parsed.timeSeconds = (uint16_t)values[0];
  parsed.operators = (uint8_t)values[1];
  parsed.operandCount = (uint8_t)values[2];
  parsed.operandDigits = (uint8_t)values[3];
  parsed.answerDigits = (uint8_t)values[4];
  parsed.decimalPlaces = (uint8_t)values[5];
  parsed.fractionDigits = (uint8_t)values[6];
  parsed.flags = (uint8_t)values[7];
  parsed.parenthesisDepth = (uint8_t)values[8];
  parsed.choiceWeight = (uint8_t)values[9];
  parsed.questionTypes = (uint8_t)values[10];
  if (!quizConfigValid(parsed)) return false;
  config = parsed;
  return true;
}

bool QuizRuntime::beginChoice(const char *locale) {
  uint32_t count = contentChoiceQuestionCount(locale);
  if (!count) return false;
  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    uint32_t index = (uint32_t)random((long)count);
    if (!contentChoiceQuestionAt(locale, index, choice)) continue;
    if (count == 1 || choice.idHash != lastChoiceHash || attempt == 4) {
      lastChoiceHash = choice.idHash;
      kind = QUIZ_QUESTION_CHOICE;
      return true;
    }
  }
  return false;
}

bool QuizRuntime::beginArithmetic() {
  if (!generateArithmetic(config, expression, sizeof(expression), expected, sizeof(expected))) return false;
  kind = QUIZ_QUESTION_ARITHMETIC;
  return true;
}

bool QuizRuntime::begin(const char *locale) {
  if (active) return false;
  kind = QUIZ_QUESTION_NONE;
  bool choiceEnabled = config.questionTypes & QUIZ_TYPE_CHOICE;
  bool arithmeticEnabled = config.questionTypes & QUIZ_TYPE_ARITHMETIC;
  uint32_t choices = choiceEnabled ? contentChoiceQuestionCount(locale) : 0;
  bool wantChoice = choices && (!arithmeticEnabled || config.choiceWeight == 100 ||
                    (config.choiceWeight && random(100) < config.choiceWeight));
  bool ready = wantChoice ? beginChoice(locale)
                          : (arithmeticEnabled && beginArithmetic());
  if (!ready && wantChoice && arithmeticEnabled) ready = beginArithmetic();
  if (!ready && !wantChoice && choices) ready = beginChoice(locale);
  if (!ready) return false;
  input[0] = 0;
  startedAt = feedbackUntil = 0;
  effectPercent = 0;
  selectedOption = 0xFF;
  scrollLine = maxScrollLine = 0;
  timerStarted = answered = correct = timedOut = settlementPending = false;
  active = true;
  return true;
}

void QuizRuntime::markRendered(uint32_t now) {
  if (!active || answered || timerStarted) return;
  timerStarted = true;
  startedAt = now;
}

uint32_t QuizRuntime::remainingMs(uint32_t now) const {
  if (!active || answered || !timerStarted) return 0;
  uint32_t duration = (uint32_t)config.timeSeconds * 1000u;
  uint32_t elapsed = now - startedAt;
  return elapsed < duration ? duration - elapsed : 0;
}

uint8_t QuizRuntime::percentAt(uint32_t now) const {
  if (!timerStarted) return 0;
  uint32_t duration = (uint32_t)config.timeSeconds * 1000u;
  uint32_t elapsed = now - startedAt;
  if (elapsed >= duration) return 0;
  uint8_t stage = (uint8_t)(((uint64_t)elapsed * 6u) / duration);
  if (stage > 5) stage = 5;
  return (uint8_t)(((6u - stage) * 100u + 3u) / 6u);
}

void QuizRuntime::finish(bool wasCorrect, uint32_t now) {
  if (!active || answered) return;
  answered = true;
  correct = wasCorrect;
  effectPercent = wasCorrect ? percentAt(now) : 0;
  feedbackUntil = now + 1200;
  settlementPending = true;
}

void QuizRuntime::update(uint32_t now) {
  if (!active) return;
  if (!answered && timerStarted && !remainingMs(now)) {
    timedOut = true;
    finish(false, now);
  }
  if (answered && !settlementPending && now >= feedbackUntil) active = false;
}

bool QuizRuntime::choose(uint8_t option, uint32_t now) {
  if (!active || answered || kind != QUIZ_QUESTION_CHOICE || option >= choice.optionCount) return false;
  selectedOption = option;
  finish(option == choice.correctIndex, now);
  return true;
}

bool QuizRuntime::append(char value) {
  if (!active || answered || kind != QUIZ_QUESTION_ARITHMETIC) return false;
  size_t length = strlen(input);
  if (length + 1 >= sizeof(input)) return false;
  if (isdigit((unsigned char)value)) {
    input[length] = value; input[length + 1] = 0; return true;
  }
  if (value == '-' && !length && (config.flags & QUIZ_ALLOW_NEGATIVE)) {
    input[0] = '-'; input[1] = 0; return true;
  }
  if (value == '.' && (config.flags & QUIZ_ALLOW_DECIMALS) && !strchr(input, '.') &&
      !strchr(input, '/')) {
    input[length] = value; input[length + 1] = 0; return true;
  }
  if (value == '/' && (config.flags & QUIZ_ALLOW_FRACTIONS) && !strchr(input, '/') &&
      !strchr(input, '.') && length && !(length == 1 && input[0] == '-')) {
    input[length] = value; input[length + 1] = 0; return true;
  }
  return false;
}

void QuizRuntime::erase() {
  size_t length = strlen(input);
  if (active && !answered && length) input[length - 1] = 0;
}

void QuizRuntime::clearInput() {
  if (active && !answered) input[0] = 0;
}

bool QuizRuntime::submit(uint32_t now) {
  if (!active || answered || kind != QUIZ_QUESTION_ARITHMETIC) return false;
  Rational entered, answer;
  if (!parseAnswer(input, entered) || !parseAnswer(expected, answer)) return false;
  finish(entered.numerator == answer.numerator && entered.denominator == answer.denominator, now);
  return true;
}

bool QuizRuntime::takeSettlement(uint32_t now, uint8_t &percent, bool &wasCorrect) {
  if (!settlementPending || (int32_t)(now - feedbackUntil) < 0) return false;
  settlementPending = false;
  active = false;
  percent = effectPercent;
  wasCorrect = correct;
  return true;
}
