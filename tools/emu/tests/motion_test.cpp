#include "motion.h"

#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const char *message) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message);
  if (!ok) failures++;
}

bool feed(ThrowGestureDetector &detector, uint32_t at,
          float ax, float ay, float az, float gyroY) {
  return detector.update({at, ax, ay, az, 0.0f, gyroY, 0.0f});
}

void prepare(ThrowGestureDetector &detector, uint32_t armedAt) {
  for (uint32_t at = armedAt; at <= armedAt + 220; at += 20) {
    feed(detector, at, 0.0f, 0.0f, 1.0f, 0.0f);
  }
}

bool feedMotion(ThrowGestureDetector &detector, uint32_t start,
                float terminalY, float terminalZ,
                uint32_t holdDuration = 300) {
  bool fired = false;
  fired = feed(detector, start, 0.0f, 0.0f, 1.1f, 100.0f) || fired;
  fired = feed(detector, start + 50, 0.0f, 0.0f, 1.4f, 220.0f) || fired;
  fired = feed(detector, start + 100, 0.0f, 0.0f, 1.7f, 300.0f) || fired;
  fired = feed(detector, start + 150, 0.0f, 0.0f, 1.3f, 250.0f) || fired;
  for (uint32_t elapsed = 0; elapsed <= holdDuration; elapsed += 50) {
    fired = feed(detector, start + 200 + elapsed,
                 0.0f, terminalY, terminalZ, 0.0f) || fired;
  }
  return fired;
}
}

int main() {
  ThrowGestureDetector detector;

  detector.arm(1000);
  bool idleFired = false;
  for (uint32_t at = 1000; at <= 2000; at += 50) {
    idleFired = feed(detector, at, 0.0f, 0.0f, 1.0f, 0.0f) || idleFired;
  }
  check(!idleFired, "held idle is rejected");

  detector.arm(3000);
  check(!feed(detector, 3100, 0.0f, 0.0f, 2.2f, 500.0f) &&
        !feed(detector, 3250, 0.0f, 0.0f, 1.0f, 0.0f),
        "the selection tap is ignored during settle time");

  detector.arm(4000);
  prepare(detector, 4000);
  check(feedMotion(detector, 4350, 1.0f, 0.0f),
        "a flick held at a changed orientation is accepted");
  check(!feed(detector, 4900, 0.0f, 1.0f, 0.0f, 0.0f),
        "one armed gesture fires only once");

  detector.arm(5000);
  prepare(detector, 5000);
  check(!feedMotion(detector, 5350, 0.0f, 1.0f),
        "large motion returning to the initial orientation is rejected");

  detector.arm(7000);
  prepare(detector, 7000);
  bool shakeFired = false;
  shakeFired = feed(detector, 7350, 0.0f, 0.0f, 1.1f, 100.0f) || shakeFired;
  shakeFired = feed(detector, 7400, 0.0f, 0.0f, 1.4f, 220.0f) || shakeFired;
  shakeFired = feed(detector, 7450, 0.0f, 0.0f, 1.7f, 300.0f) || shakeFired;
  shakeFired = feed(detector, 7500, 0.0f, 0.0f, 1.3f, 250.0f) || shakeFired;
  shakeFired = feed(detector, 7550, 0.0f, 0.0f, 1.3f, -250.0f) || shakeFired;
  shakeFired = feed(detector, 7600, 0.0f, 0.0f, 1.7f, -300.0f) || shakeFired;
  for (uint32_t at = 7650; at <= 8000; at += 50) {
    shakeFired = feed(detector, at, 0.0f, 0.0f, 1.0f, 0.0f) || shakeFired;
  }
  check(!shakeFired,
        "a reversing shake ending at its initial orientation is rejected");

  detector.arm(9000);
  prepare(detector, 9000);
  check(!feedMotion(detector, 9350, 1.0f, 0.0f, 250),
        "a terminal hold shorter than 300 ms is rejected");

  detector.arm(11000);
  prepare(detector, 11000);
  check(!feedMotion(detector, 11350, 0.5f, 0.8660254f),
        "a terminal orientation change below 50 degrees is rejected");

  std::puts(failures ? "FAILURES" : "throw gesture detector tests passed");
  return failures ? 1 : 0;
}
