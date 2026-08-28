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

struct ThrowGestureDiagnostics {
  uint16_t events = 0;
  uint16_t completedHolds = 0;
  uint16_t eventTimeouts = 0;
  uint32_t longestHoldMs = 0;
  float maxAngularPath = 0.0f;
  float maxGyro = 0.0f;
  float maxAccelDelta = 0.0f;
  float maxOrientationDelta = 0.0f;
};

class ThrowGestureDetector {
public:
  void arm(uint32_t now);
  bool update(const MotionSample &sample);
  const ThrowGestureDiagnostics &diagnostics() const;

private:
  enum State : uint8_t {
    PREPARING,
    SEEKING,
    ACTIVE,
  };

  static constexpr uint8_t BASELINE_SAMPLE_LIMIT = 12;

  void resetPreparation(uint32_t at, bool waitForTapSettle);
  void clearBaseline();
  void observeBaseline(const MotionSample &sample);
  void beginEvent(uint32_t at);
  void resetHold();
  void addHoldSample(const MotionSample &sample);
  float orientationDelta() const;

  State state = PREPARING;
  uint32_t armedAt = 0;
  uint32_t quietAt = 0;
  uint32_t eventAt = 0;
  uint32_t lastAt = 0;
  uint32_t holdAt = 0;
  float baselineSamples[BASELINE_SAMPLE_LIMIT][3] = {};
  uint8_t baselineNext = 0;
  uint8_t baselineCount = 0;
  float baselineX = 0.0f;
  float baselineY = 0.0f;
  float baselineZ = 0.0f;
  float holdX = 0.0f;
  float holdY = 0.0f;
  float holdZ = 0.0f;
  uint16_t holdCount = 0;
  float angularPath = 0.0f;
  float peakGyro = 0.0f;
  float peakAccelDelta = 0.0f;
  bool waitingForTapSettle = true;
  bool fired = false;
  ThrowGestureDiagnostics diagnostic;
};

bool motionBegin();
bool motionStart();
void motionStop();
bool motionRead(MotionSample &sample, uint32_t now);
