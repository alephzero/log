#pragma once

#include <chrono>
#include <cmath>
#include <sstream>
#include <string>

namespace a0::logger {

static inline std::chrono::nanoseconds parse_duration(std::string str) {
  double val;
  std::string suffix;
  std::string remainder;

  std::stringstream(std::move(str)) >> val >> suffix >> remainder;
  if (val <= 0) {
    throw std::invalid_argument("Duration parse failed. Value may not be negative");
  }
  if (!remainder.empty()) {
    throw std::invalid_argument("Duration parse failed. Expect something like 300ms or 2.5s");
  }
  if (suffix.empty()) {
    throw std::invalid_argument("Duration parse failed. Missing units");
  }

  if (suffix == "ns") {
  } else if (suffix == "us") {
    val *= 1e3;
  } else if (suffix == "ms") {
    val *= 1e6;
  } else if (suffix == "s") {
    val *= 1e9;
  } else if (suffix == "m") {
    val *= 1e9 * 60;
  } else if (suffix == "h") {
    val *= 1e9 * 60 * 60;
  } else {
    throw std::invalid_argument("Duration parse failed. Unknown unit " + suffix + ". Known: ns, us, ms, s, m, h");
  }

  return std::chrono::nanoseconds(int64_t(val));
}

static inline uint64_t parse_filesize(std::string str) {
  double val;
  std::string suffix;
  std::string remainder;

  std::stringstream(std::move(str)) >> val >> suffix >> remainder;
  if (val <= 0) {
    throw std::invalid_argument("FileSize parse failed. Value may not be negative");
  }
  if (!remainder.empty()) {
    throw std::invalid_argument("FileSize parse failed. Expect something like 300KB or 2.5MB");
  }
  if (suffix.empty()) {
    throw std::invalid_argument("FileSize parse failed. Missing units");
  }

  if (suffix == "B") {
  } else if (suffix == "KiB") {
    val *= pow(1024, 1);
  } else if (suffix == "MiB") {
    val *= pow(1024, 2);
  } else if (suffix == "GiB") {
    val *= pow(1024, 3);
  } else if (suffix == "TiB") {
    val *= pow(1024, 4);
  } else {
    throw std::invalid_argument("FileSize parse failed. Unknown unit " + suffix + ". Known: B, KiB, MiB, GiB, TiB");
  }

  return int64_t(val);
}

}  // namespace a0::logger
