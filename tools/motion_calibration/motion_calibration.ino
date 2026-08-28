#include <Arduino.h>
#include <SensorQMI8658.hpp>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

#include "../../pin_config.h"

namespace {
constexpr char FIRMWARE_ID[] = "motion-calibration-v1";
constexpr uint16_t SAMPLE_CAPACITY = 384;
constexpr uint32_t TRACE_DURATION_MS = 3000;
constexpr uint8_t SESSION_MAX = 15;
constexpr uint8_t LABEL_MAX = 23;
constexpr uint16_t DUMP_CHUNK_SIZE = 16;

struct MotionSample {
  uint32_t at;
  float ax;
  float ay;
  float az;
  float gx;
  float gy;
  float gz;
};

enum TraceState : uint8_t {
  TRACE_IDLE = 0,
  TRACE_RUNNING,
  TRACE_COMPLETE,
};

SensorQMI8658 qmi;
bool qmiReady = false;
MotionSample *samples = nullptr;
TraceState traceState = TRACE_IDLE;
uint16_t sampleCount = 0;
uint16_t droppedCount = 0;
uint32_t startedAt = 0;
uint32_t endedAt = 0;
char sessionName[SESSION_MAX + 1] = {};
char traceLabel[LABEL_MAX + 1] = {};
uint16_t traceTrial = 0;
bool traceExpected = false;
bool calibrationValid = false;
uint16_t calibrationX = 0;
uint16_t calibrationY = 0;
uint16_t calibrationZ = 0;

const char *traceStateName() {
  switch (traceState) {
    case TRACE_RUNNING:
      return "RUNNING";
    case TRACE_COMPLETE:
      return "COMPLETE";
    default:
      return "IDLE";
  }
}

void stopSensor() {
  if (!qmiReady) return;
  qmi.disableAccelerometer();
  qmi.disableGyroscope();
}

bool startSensor() {
  return qmiReady && qmi.enableGyroscope() && qmi.enableAccelerometer();
}

bool beginSensor() {
  bool ready = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  // GLUE: Keep these settings aligned with motion.cpp so standalone traces
  // match production input. Remove this duplication if both sketches later
  // consume a shared sensor configuration without changing game firmware.
  ready = ready &&
          qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                  SensorQMI8658::ACC_ODR_125Hz,
                                  SensorQMI8658::LPF_MODE_3) &&
          qmi.configGyroscope(SensorQMI8658::GYR_RANGE_1024DPS,
                              SensorQMI8658::GYR_ODR_112_1Hz,
                              SensorQMI8658::LPF_MODE_3);
  if (ready) {
    qmi.disableAccelerometer();
    qmi.disableGyroscope();
  }
  return ready;
}

void finishTrace(uint32_t now) {
  stopSensor();
  endedAt = now;
  traceState = TRACE_COMPLETE;
  Serial.println("MOTION_COMPLETE");
}

void updateTrace() {
  uint32_t now = millis();
  if (now - startedAt >= TRACE_DURATION_MS) {
    finishTrace(now);
    return;
  }
  if (!qmi.getDataReady()) return;

  MotionSample sample = {};
  sample.at = now;
  if (!qmi.getAccelerometer(sample.ax, sample.ay, sample.az) ||
      !qmi.getGyroscope(sample.gx, sample.gy, sample.gz)) {
    if (droppedCount < UINT16_MAX) droppedCount++;
    return;
  }
  if (sampleCount < SAMPLE_CAPACITY) {
    samples[sampleCount++] = sample;
  } else if (droppedCount < UINT16_MAX) {
    droppedCount++;
  }
}

void clearTrace() {
  if (traceState == TRACE_RUNNING) stopSensor();
  traceState = TRACE_IDLE;
  sampleCount = 0;
  droppedCount = 0;
  startedAt = 0;
  endedAt = 0;
  sessionName[0] = '\0';
  traceLabel[0] = '\0';
  traceTrial = 0;
  traceExpected = false;
}

void printDone() {
  Serial.println("DONE");
}

void handleStatus() {
  Serial.printf("MOTION_STATUS\t%s\t%u\t%u\n", traceStateName(),
                sampleCount, droppedCount);
  printDone();
}

void handleCalibration() {
  if (!qmiReady) {
    Serial.println("ERR MOTION_SENSOR_UNAVAILABLE");
    return;
  }
  if (traceState != TRACE_IDLE) {
    Serial.println("ERR MOTION_BUSY");
    return;
  }

  stopSensor();
  uint16_t gx = 0;
  uint16_t gy = 0;
  uint16_t gz = 0;
  if (!qmi.calibration(&gx, &gy, &gz) ||
      !qmi.writeCalibration(gx, gy, gz)) {
    Serial.println("ERR MOTION_CAL_FAILED");
    return;
  }
  calibrationValid = true;
  calibrationX = gx;
  calibrationY = gy;
  calibrationZ = gz;
  Serial.printf("MOTION_CAL\t%u\t%u\t%u\n", gx, gy, gz);
  printDone();
}

void handleTrace(const String &line) {
  char session[SESSION_MAX + 1] = {};
  char label[LABEL_MAX + 1] = {};
  unsigned trial = 0;
  unsigned expected = 0;
  char extra = '\0';
  int parsed = sscanf(line.c_str() + 13, "%15s %23s %u %u %c", session,
                      label, &trial, &expected, &extra);
  if (parsed != 4 || trial == 0 || trial > UINT16_MAX || expected > 1) {
    Serial.println("ERR MOTION_TRACE_REQUEST");
    return;
  }
  if (!qmiReady || !samples) {
    Serial.println("ERR MOTION_SENSOR_UNAVAILABLE");
    return;
  }
  if (traceState != TRACE_IDLE) {
    Serial.println("ERR MOTION_BUSY");
    return;
  }
  if (!startSensor()) {
    stopSensor();
    Serial.println("ERR MOTION_START_FAILED");
    return;
  }

  strncpy(sessionName, session, sizeof(sessionName) - 1);
  strncpy(traceLabel, label, sizeof(traceLabel) - 1);
  traceTrial = static_cast<uint16_t>(trial);
  traceExpected = expected != 0;
  sampleCount = 0;
  droppedCount = 0;
  startedAt = millis();
  endedAt = 0;
  traceState = TRACE_RUNNING;
  Serial.printf("MOTION_READY\t%s\t%s\t%u\t%u\n", sessionName,
                traceLabel, traceTrial, traceExpected ? 1 : 0);
  printDone();
}

void handleDump(const String &line) {
  if (traceState != TRACE_COMPLETE) {
    Serial.println("ERR MOTION_NOT_COMPLETE");
    return;
  }

  unsigned offset = 0;
  char extra = '\0';
  if (line != "MOTION DUMP" &&
      sscanf(line.c_str() + 12, "%u %c", &offset, &extra) != 1) {
    Serial.println("ERR MOTION_DUMP_REQUEST");
    return;
  }
  if (offset > sampleCount) {
    Serial.println("ERR MOTION_DUMP_OFFSET");
    return;
  }

  uint16_t end = static_cast<uint16_t>(offset) + DUMP_CHUNK_SIZE;
  if (end > sampleCount) end = sampleCount;
  if (offset == 0) {
    Serial.printf("MOTION_META\t2\t%s\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%u\n",
                  FIRMWARE_ID, sessionName, traceLabel, traceTrial,
                  traceExpected ? 1 : 0, calibrationValid ? 1 : 0,
                  calibrationX, calibrationY, calibrationZ);
    Serial.println("MOTION_CONFIG\t4\t125.0\t1024\t112.1\t3");
  }
  for (uint16_t i = static_cast<uint16_t>(offset); i < end; i++) {
    const MotionSample &sample = samples[i];
    Serial.printf(
        "MOTION_SAMPLE\t%u\t%lu\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n",
        i, static_cast<unsigned long>(sample.at), sample.ax, sample.ay,
        sample.az, sample.gx, sample.gy, sample.gz);
  }
  if (end == sampleCount) {
    Serial.printf("MOTION_END\t%u\t%u\t%lu\t%lu\n", sampleCount,
                  droppedCount, static_cast<unsigned long>(startedAt),
                  static_cast<unsigned long>(endedAt));
  }
  Serial.printf("MOTION_CHUNK\t%u\t%u\n", end, sampleCount);
  printDone();
}

void handleCommand(String line) {
  line.trim();
  if (line.isEmpty()) return;
  if (line == "MOTION STATUS") {
    handleStatus();
  } else if (line == "MOTION CAL") {
    handleCalibration();
  } else if (line.startsWith("MOTION TRACE ")) {
    handleTrace(line);
  } else if (line.startsWith("MOTION DUMP")) {
    handleDump(line);
  } else if (line == "MOTION CLEAR") {
    clearTrace();
    printDone();
  } else {
    Serial.println("ERR MOTION_COMMAND");
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  Wire.begin(IIC_SDA, IIC_SCL);
  qmiReady = beginSensor();
  samples = static_cast<MotionSample *>(
      ps_malloc(sizeof(MotionSample) * SAMPLE_CAPACITY));
  Serial.printf("MOTION_BOOT\t%s\t%s\t%s\n", FIRMWARE_ID,
                qmiReady ? "SENSOR_READY" : "SENSOR_ERROR",
                samples ? "BUFFER_READY" : "BUFFER_ERROR");
}

void loop() {
  if (traceState == TRACE_RUNNING) {
    updateTrace();
    return;
  }
  if (Serial.available()) handleCommand(Serial.readStringUntil('\n'));
  delay(1);
}
