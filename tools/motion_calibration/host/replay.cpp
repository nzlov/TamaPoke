#include <stdint.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../../motion.h"

namespace {
struct Trial {
  std::string session;
  std::string label;
  uint16_t number = 0;
  bool expected = false;
  uint16_t dropped = 0;
  uint32_t startedAt = 0;
  std::vector<MotionSample> samples;
};

struct Totals {
  unsigned tp = 0;
  unsigned tn = 0;
  unsigned fp = 0;
  unsigned fn = 0;
};

std::vector<std::string> splitTabs(const std::string &line) {
  std::vector<std::string> fields;
  size_t start = 0;
  while (true) {
    size_t tab = line.find('\t', start);
    fields.push_back(line.substr(start, tab - start));
    if (tab == std::string::npos) return fields;
    start = tab + 1;
  }
}

uint32_t unsignedField(const std::string &value, const char *name) {
  size_t consumed = 0;
  unsigned long parsed = std::stoul(value, &consumed);
  if (consumed != value.size() || parsed > UINT32_MAX) {
    throw std::runtime_error(std::string("invalid ") + name);
  }
  return static_cast<uint32_t>(parsed);
}

float floatField(const std::string &value, const char *name) {
  size_t consumed = 0;
  float parsed = std::stof(value, &consumed);
  if (consumed != value.size()) {
    throw std::runtime_error(std::string("invalid ") + name);
  }
  return parsed;
}

void requireFields(const std::vector<std::string> &fields, size_t count,
                   const char *row) {
  if (fields.size() != count) {
    throw std::runtime_error(std::string(row) + " row has the wrong field count");
  }
}

bool replay(const Trial &trial, uint32_t &firedAt,
            ThrowGestureDiagnostics &diagnostic) {
  ThrowGestureDetector detector;
  detector.arm(trial.startedAt);
  for (const MotionSample &sample : trial.samples) {
    if (detector.update(sample)) {
      firedAt = sample.at;
      diagnostic = detector.diagnostics();
      return true;
    }
  }
  diagnostic = detector.diagnostics();
  firedAt = 0;
  return false;
}

void report(const Trial &trial, bool debug, Totals &totals) {
  uint32_t firedAt = 0;
  ThrowGestureDiagnostics diagnostic;
  bool predicted = replay(trial, firedAt, diagnostic);
  if (trial.expected && predicted) {
    totals.tp++;
  } else if (!trial.expected && !predicted) {
    totals.tn++;
  } else if (predicted) {
    totals.fp++;
  } else {
    totals.fn++;
  }
  std::cout << "TRIAL\t" << trial.session << '\t' << trial.label << '\t'
            << trial.number << '\t' << (trial.expected ? 1 : 0) << '\t'
            << (predicted ? 1 : 0) << '\t' << trial.samples.size() << '\t'
            << trial.dropped << '\t' << firedAt << '\n';
  if (debug) {
    std::cout << "DIAG\t" << trial.session << '\t' << trial.label << '\t'
              << trial.number << '\t' << diagnostic.events << '\t'
              << diagnostic.completedHolds << '\t'
              << diagnostic.eventTimeouts << '\t'
              << diagnostic.longestHoldMs << '\t'
              << diagnostic.maxAngularPath << '\t'
              << diagnostic.maxGyro << '\t'
              << diagnostic.maxAccelDelta << '\t'
              << diagnostic.maxOrientationDelta << '\n';
  }
}

Totals readDataset(const char *path, bool debug) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open dataset");

  Totals totals;
  Trial trial;
  bool active = false;
  std::string line;
  unsigned lineNumber = 0;
  while (std::getline(input, line)) {
    lineNumber++;
    try {
      std::vector<std::string> fields = splitTabs(line);
      if (fields.empty()) continue;
      if (fields[0] == "MOTION_META") {
        requireFields(fields, 11, "META");
        if (active) throw std::runtime_error("new META before END");
        if (fields[1] != "2") throw std::runtime_error("unsupported schema");
        trial = Trial();
        trial.session = fields[3];
        trial.label = fields[4];
        uint32_t number = unsignedField(fields[5], "trial number");
        uint32_t expected = unsignedField(fields[6], "expected value");
        if (number == 0 || number > UINT16_MAX || expected > 1) {
          throw std::runtime_error("invalid META trial metadata");
        }
        trial.number = static_cast<uint16_t>(number);
        trial.expected = expected != 0;
        active = true;
      } else if (fields[0] == "MOTION_CONFIG") {
        requireFields(fields, 6, "CONFIG");
        if (!active) throw std::runtime_error("CONFIG without META");
      } else if (fields[0] == "MOTION_SAMPLE") {
        requireFields(fields, 9, "SAMPLE");
        if (!active) throw std::runtime_error("SAMPLE without META");
        uint32_t sequence = unsignedField(fields[1], "sample sequence");
        if (sequence != trial.samples.size()) {
          throw std::runtime_error("sample sequence is not contiguous");
        }
        MotionSample sample;
        sample.at = unsignedField(fields[2], "sample timestamp");
        sample.ax = floatField(fields[3], "ax");
        sample.ay = floatField(fields[4], "ay");
        sample.az = floatField(fields[5], "az");
        sample.gx = floatField(fields[6], "gx");
        sample.gy = floatField(fields[7], "gy");
        sample.gz = floatField(fields[8], "gz");
        trial.samples.push_back(sample);
      } else if (fields[0] == "MOTION_END") {
        requireFields(fields, 5, "END");
        if (!active) throw std::runtime_error("END without META");
        uint32_t count = unsignedField(fields[1], "sample count");
        uint32_t dropped = unsignedField(fields[2], "dropped count");
        if (count != trial.samples.size() || dropped > UINT16_MAX) {
          throw std::runtime_error("END does not match trial samples");
        }
        trial.dropped = static_cast<uint16_t>(dropped);
        trial.startedAt = unsignedField(fields[3], "start timestamp");
        unsignedField(fields[4], "end timestamp");
        report(trial, debug, totals);
        active = false;
      }
    } catch (const std::exception &error) {
      throw std::runtime_error("line " + std::to_string(lineNumber) + ": " +
                               error.what());
    }
  }
  if (active) throw std::runtime_error("dataset ended before MOTION_END");
  return totals;
}
}  // namespace

int main(int argc, char **argv) {
  bool debug = false;
  const char *path = nullptr;
  if (argc == 2) {
    path = argv[1];
  } else if (argc == 3 && std::string(argv[1]) == "--debug") {
    debug = true;
    path = argv[2];
  } else {
    std::cerr << "usage: motion-replay [--debug] <dataset.tsv>\n";
    return 2;
  }
  try {
    Totals totals = readDataset(path, debug);
    std::cout << "SUMMARY\t" << totals.tp << '\t' << totals.tn << '\t'
              << totals.fp << '\t' << totals.fn << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "motion replay failed: " << error.what() << '\n';
    return 1;
  }
}
