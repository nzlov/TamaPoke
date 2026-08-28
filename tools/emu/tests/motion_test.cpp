#include "motion.h"

#include <cstdio>

static int bad = 0;

static void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static bool feed(ThrowGestureDetector &detector, uint32_t at,
                 float accel, float gyro) {
  MotionSample sample;
  sample.at = at;
  sample.az = accel;
  sample.gy = gyro;
  return detector.update(sample);
}

int main() {
  ThrowGestureDetector detector;
  detector.arm(1000);
  bool idleFired = false;
  for (uint32_t at = 1000; at <= 1900; at += 50)
    idleFired = detector.update({at, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f}) || idleFired;
  check(!idleFired, "resting gravity never looks like a throw");

  detector.arm(2000);
  check(!feed(detector, 2100, 2.2f, 500.0f) &&
        !feed(detector, 2110, 1.0f, 0.0f),
        "the selection tap and a single impact spike are ignored");

  detector.arm(3000);
  bool slowFired = false;
  for (uint32_t at = 3250; at <= 3800; at += 50)
    slowFired = feed(detector, at, 1.1f, 100.0f) || slowFired;
  check(!slowFired, "a slow wrist turn stays below the throw peak");

  detector.arm(4000);
  bool shakeFired = false;
  shakeFired = feed(detector, 4250, 1.1f, 200.0f) || shakeFired;
  shakeFired = feed(detector, 4300, 1.7f, 200.0f) || shakeFired;
  shakeFired = feed(detector, 4350, 1.7f, -200.0f) || shakeFired;
  shakeFired = feed(detector, 4400, 1.1f, -200.0f) || shakeFired;
  check(!shakeFired, "a short direction-reversing shake is rejected");

  detector.arm(5000);
  bool throwFired = false;
  throwFired = feed(detector, 5250, 1.1f, 100.0f) || throwFired;
  throwFired = feed(detector, 5300, 1.3f, 220.0f) || throwFired;
  throwFired = feed(detector, 5350, 1.7f, 300.0f) || throwFired;
  throwFired = feed(detector, 5400, 1.3f, 250.0f) || throwFired;
  check(throwFired, "a smooth accelerated wrist throw is detected");
  check(!feed(detector, 5450, 1.8f, 300.0f),
        "one armed gesture can fire only once");

  std::puts(bad ? "FAILURES" : "throw gesture detector is selective and one-shot");
  return bad ? 1 : 0;
}
