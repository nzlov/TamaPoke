#include "perf.h"

#if defined(ESP32)
#include <esp_timer.h>
#else
#include <chrono>
#endif

static PerfSample gSamples[PERF_METRIC_COUNT];

uint32_t perfNowUs() {
#if defined(ESP32)
  return (uint32_t)esp_timer_get_time();
#else
  using namespace std::chrono;
  return (uint32_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

void perfRecord(PerfMetric metric, uint32_t elapsedUs, uint32_t nvsWrites) {
  if (metric >= PERF_METRIC_COUNT) return;
  PerfSample &sample = gSamples[metric];
  sample.count++;
  sample.totalUs += elapsedUs;
  if (elapsedUs > sample.maxUs) sample.maxUs = elapsedUs;
  sample.nvsWrites += nvsWrites;
}

const PerfSample &perfSample(PerfMetric metric) {
  static const PerfSample empty;
  return metric < PERF_METRIC_COUNT ? gSamples[metric] : empty;
}
