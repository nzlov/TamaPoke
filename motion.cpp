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
constexpr uint32_t STROKE_LIMIT_MS = 700;
constexpr uint32_t SAMPLE_DT_LIMIT_MS = 60;
constexpr float STROKE_START_DPS = 80.0f;
constexpr float STROKE_REVERSE_DPS = -80.0f;
constexpr float THROW_PEAK_DPS = 180.0f;
constexpr float THROW_ANGLE_DEG = 30.0f;
constexpr float THROW_ACCEL_DELTA_G = 0.45f;

float magnitude(float x, float y, float z) {
  return sqrtf(x * x + y * y + z * z);
}
}

void ThrowGestureDetector::arm(uint32_t now) {
  armedAt = now;
  fired = false;
  clearStroke();
}

void ThrowGestureDetector::clearStroke() {
  strokeAt = 0;
  lastAt = 0;
  directionX = directionY = directionZ = 0.0f;
  angularTravel = 0.0f;
  peakProjectedGyro = 0.0f;
  peakAccelDelta = 0.0f;
  tracking = false;
}

void ThrowGestureDetector::beginStroke(const MotionSample &sample,
                                       float gyroMagnitude) {
  directionX = sample.gx / gyroMagnitude;
  directionY = sample.gy / gyroMagnitude;
  directionZ = sample.gz / gyroMagnitude;
  strokeAt = lastAt = sample.at;
  tracking = true;
}

bool ThrowGestureDetector::update(const MotionSample &sample) {
  if (fired || sample.at - armedAt < TAP_SETTLE_MS) return false;

  float gyroMagnitude = magnitude(sample.gx, sample.gy, sample.gz);
  if (!tracking) {
    if (gyroMagnitude >= STROKE_START_DPS) beginStroke(sample, gyroMagnitude);
    return false;
  }

  if (sample.at - strokeAt > STROKE_LIMIT_MS) {
    clearStroke();
    if (gyroMagnitude >= STROKE_START_DPS) beginStroke(sample, gyroMagnitude);
    return false;
  }

  float projected = sample.gx * directionX + sample.gy * directionY +
                    sample.gz * directionZ;
  if (projected <= STROKE_REVERSE_DPS) {
    clearStroke();
    beginStroke(sample, gyroMagnitude);
    return false;
  }

  uint32_t dt = sample.at - lastAt;
  if (dt > SAMPLE_DT_LIMIT_MS) dt = SAMPLE_DT_LIMIT_MS;
  lastAt = sample.at;
  if (projected > 0.0f) angularTravel += projected * ((float)dt / 1000.0f);
  if (projected > peakProjectedGyro) peakProjectedGyro = projected;

  float accelDelta = fabsf(magnitude(sample.ax, sample.ay, sample.az) - 1.0f);
  if (accelDelta > peakAccelDelta) peakAccelDelta = accelDelta;

  if (angularTravel < THROW_ANGLE_DEG ||
      peakProjectedGyro < THROW_PEAK_DPS ||
      peakAccelDelta < THROW_ACCEL_DELTA_G) {
    return false;
  }
  fired = true;
  return true;
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
