#pragma once

#include <stdint.h>

enum PerfMetric : uint8_t {
  PERF_FRAME = 0,
  PERF_FLUSH,
  PERF_PET_SAVE,
  PERF_PLAYER_SAVE,
  PERF_TEAM_SAVE,
  PERF_INVENTORY_SAVE,
  PERF_METRIC_COUNT,
};

struct PerfSample {
  uint32_t count = 0;
  uint64_t totalUs = 0;
  uint32_t maxUs = 0;
  uint32_t nvsWrites = 0;
};

uint32_t perfNowUs();
void perfRecord(PerfMetric metric, uint32_t elapsedUs, uint32_t nvsWrites = 0);
const PerfSample &perfSample(PerfMetric metric);
