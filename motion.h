#pragma once

#include <stdint.h>

struct MotionSample {
  uint32_t at = 0;
  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  float gx = 0.0f;
  float gy = 0.0f;
  float gz = 0.0f;
};

class ThrowGestureDetector {
public:
  void arm(uint32_t now);
  bool update(const MotionSample &sample);

private:
  void beginStroke(const MotionSample &sample, float gyroMagnitude);
  void clearStroke();

  uint32_t armedAt = 0;
  uint32_t strokeAt = 0;
  uint32_t lastAt = 0;
  float directionX = 0.0f;
  float directionY = 0.0f;
  float directionZ = 0.0f;
  float angularTravel = 0.0f;
  float peakProjectedGyro = 0.0f;
  float peakAccelDelta = 0.0f;
  bool tracking = false;
  bool fired = false;
};

bool motionBegin();
bool motionStart();
void motionStop();
bool motionRead(MotionSample &sample, uint32_t now);
