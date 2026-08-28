#include "motion.h"

#include <math.h>

#if defined(ARDUINO)
#include <Arduino.h>
#include <Wire.h>
#include <SensorQMI8658.hpp>
#include "pin_config.h"

static SensorQMI8658 qmi;
static bool qmiReady = false;
#endif

namespace {
constexpr uint32_t TAP_SETTLE_MS = 200;
constexpr uint32_t PREPARE_QUIET_MS = 30;
constexpr uint32_t EVENT_LIMIT_MS = 2000;
constexpr uint32_t SAMPLE_DT_LIMIT_MS = 60;
constexpr uint32_t HOLD_MS = 300;
constexpr float EVENT_START_DPS = 80.0f;
constexpr float PREPARE_MAX_DPS = 40.0f;
constexpr float HOLD_MAX_DPS = 60.0f;
constexpr float MIN_ANGULAR_PATH_DEG = 30.0f;
constexpr float MIN_PEAK_GYRO_DPS = 150.0f;
constexpr float MIN_ACCEL_DELTA_G = 0.30f;
constexpr float MIN_ORIENTATION_DELTA_DEG = 50.0f;
constexpr float MIN_GRAVITY_G = 0.70f;
constexpr float MAX_GRAVITY_G = 1.30f;
constexpr float RADIANS_TO_DEGREES = 57.2957795f;

float magnitude(float x, float y, float z) {
  return sqrtf(x * x + y * y + z * z);
}

bool gravityLike(float accelMagnitude) {
  return accelMagnitude >= MIN_GRAVITY_G && accelMagnitude <= MAX_GRAVITY_G;
}
}

void ThrowGestureDetector::arm(uint32_t now) {
  fired = false;
  diagnostic = ThrowGestureDiagnostics();
  resetPreparation(now, true);
}

void ThrowGestureDetector::resetPreparation(uint32_t at,
                                            bool waitForTapSettle) {
  state = PREPARING;
  armedAt = at;
  quietAt = eventAt = lastAt = 0;
  angularPath = peakGyro = peakAccelDelta = 0.0f;
  waitingForTapSettle = waitForTapSettle;
  clearBaseline();
  resetHold();
}

void ThrowGestureDetector::clearBaseline() {
  baselineNext = baselineCount = 0;
  baselineX = baselineY = baselineZ = 0.0f;
}

void ThrowGestureDetector::observeBaseline(const MotionSample &sample) {
  if (baselineCount == BASELINE_SAMPLE_LIMIT) {
    baselineX -= baselineSamples[baselineNext][0];
    baselineY -= baselineSamples[baselineNext][1];
    baselineZ -= baselineSamples[baselineNext][2];
  } else {
    baselineCount++;
  }
  baselineSamples[baselineNext][0] = sample.ax;
  baselineSamples[baselineNext][1] = sample.ay;
  baselineSamples[baselineNext][2] = sample.az;
  baselineX += sample.ax;
  baselineY += sample.ay;
  baselineZ += sample.az;
  baselineNext = static_cast<uint8_t>((baselineNext + 1) %
                                      BASELINE_SAMPLE_LIMIT);
}

void ThrowGestureDetector::beginEvent(uint32_t at) {
  state = ACTIVE;
  eventAt = lastAt = at;
  angularPath = peakGyro = peakAccelDelta = 0.0f;
  resetHold();
  diagnostic.events++;
}

void ThrowGestureDetector::resetHold() {
  holdAt = 0;
  holdX = holdY = holdZ = 0.0f;
  holdCount = 0;
}

void ThrowGestureDetector::addHoldSample(const MotionSample &sample) {
  holdX += sample.ax;
  holdY += sample.ay;
  holdZ += sample.az;
  holdCount++;
}

float ThrowGestureDetector::orientationDelta() const {
  float baselineMagnitude = magnitude(baselineX, baselineY, baselineZ);
  float holdMagnitude = magnitude(holdX, holdY, holdZ);
  if (baselineMagnitude == 0.0f || holdMagnitude == 0.0f) return 0.0f;

  float cosine = (baselineX * holdX + baselineY * holdY + baselineZ * holdZ) /
                 (baselineMagnitude * holdMagnitude);
  if (cosine < -1.0f) cosine = -1.0f;
  if (cosine > 1.0f) cosine = 1.0f;
  return acosf(cosine) * RADIANS_TO_DEGREES;
}

bool ThrowGestureDetector::update(const MotionSample &sample) {
  if (fired) return false;

  float gyroMagnitude = magnitude(sample.gx, sample.gy, sample.gz);
  float accelMagnitude = magnitude(sample.ax, sample.ay, sample.az);

  if (state == PREPARING) {
    if (waitingForTapSettle && sample.at - armedAt < TAP_SETTLE_MS) {
      if (gyroMagnitude <= PREPARE_MAX_DPS && gravityLike(accelMagnitude)) {
        observeBaseline(sample);
      }
      return false;
    }

    waitingForTapSettle = false;
    if (baselineCount > 0) {
      state = SEEKING;
      quietAt = 0;
    } else {
      if (gyroMagnitude > PREPARE_MAX_DPS) {
        quietAt = 0;
        clearBaseline();
      } else {
        if (quietAt == 0) quietAt = sample.at;
        if (gravityLike(accelMagnitude)) observeBaseline(sample);
        if (sample.at - quietAt >= PREPARE_QUIET_MS && baselineCount > 0) {
          state = SEEKING;
          quietAt = 0;
        }
      }
      return false;
    }
  }

  if (state == SEEKING) {
    if (gyroMagnitude >= EVENT_START_DPS) beginEvent(sample.at);
    return false;
  }

  if (sample.at - eventAt > EVENT_LIMIT_MS) {
    diagnostic.eventTimeouts++;
    resetPreparation(sample.at, false);
    return false;
  }

  uint32_t dt = sample.at - lastAt;
  if (dt > SAMPLE_DT_LIMIT_MS) dt = SAMPLE_DT_LIMIT_MS;
  lastAt = sample.at;
  angularPath += gyroMagnitude * (static_cast<float>(dt) / 1000.0f);
  if (gyroMagnitude > peakGyro) peakGyro = gyroMagnitude;
  float accelDelta = fabsf(accelMagnitude - 1.0f);
  if (accelDelta > peakAccelDelta) peakAccelDelta = accelDelta;
  if (angularPath > diagnostic.maxAngularPath) {
    diagnostic.maxAngularPath = angularPath;
  }
  if (peakGyro > diagnostic.maxGyro) diagnostic.maxGyro = peakGyro;
  if (peakAccelDelta > diagnostic.maxAccelDelta) {
    diagnostic.maxAccelDelta = peakAccelDelta;
  }

  if (gyroMagnitude > HOLD_MAX_DPS) {
    resetHold();
    return false;
  }

  if (holdAt == 0) holdAt = sample.at;
  if (gravityLike(accelMagnitude)) addHoldSample(sample);
  uint32_t holdDuration = sample.at - holdAt;
  if (holdDuration > diagnostic.longestHoldMs) {
    diagnostic.longestHoldMs = holdDuration;
  }
  if (holdDuration < HOLD_MS || holdCount == 0) return false;

  diagnostic.completedHolds++;
  float orientation = orientationDelta();
  if (orientation > diagnostic.maxOrientationDelta) {
    diagnostic.maxOrientationDelta = orientation;
  }
  if (angularPath < MIN_ANGULAR_PATH_DEG ||
      peakGyro < MIN_PEAK_GYRO_DPS ||
      peakAccelDelta < MIN_ACCEL_DELTA_G ||
      orientation < MIN_ORIENTATION_DELTA_DEG) {
    resetPreparation(sample.at, false);
    return false;
  }

  fired = true;
  return true;
}

const ThrowGestureDiagnostics &ThrowGestureDetector::diagnostics() const {
  return diagnostic;
}

bool motionBegin() {
#if defined(ARDUINO)
  qmiReady = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL) &&
             qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                     SensorQMI8658::ACC_ODR_125Hz,
                                     SensorQMI8658::LPF_MODE_3) &&
             qmi.configGyroscope(SensorQMI8658::GYR_RANGE_1024DPS,
                                 SensorQMI8658::GYR_ODR_112_1Hz,
                                 SensorQMI8658::LPF_MODE_3);
  if (qmiReady) {
    qmi.disableAccelerometer();
    qmi.disableGyroscope();
  }
  return qmiReady;
#else
  return false;
#endif
}

bool motionStart() {
#if defined(ARDUINO)
  return qmiReady && qmi.enableGyroscope() && qmi.enableAccelerometer();
#else
  return false;
#endif
}

void motionStop() {
#if defined(ARDUINO)
  if (!qmiReady) return;
  qmi.disableAccelerometer();
  qmi.disableGyroscope();
#endif
}

bool motionRead(MotionSample &sample, uint32_t now) {
#if defined(ARDUINO)
  if (!qmiReady || !qmi.getDataReady()) return false;
  sample.at = now;
  return qmi.getAccelerometer(sample.ax, sample.ay, sample.az) &&
         qmi.getGyroscope(sample.gx, sample.gy, sample.gz);
#else
  (void)sample;
  (void)now;
  return false;
#endif
}
