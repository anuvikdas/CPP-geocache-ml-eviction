#pragma once
#include <chrono>
#include "metrics.hpp"

class ScopedGetTimer {
public:
  ScopedGetTimer() : start_(std::chrono::high_resolution_clock::now()) {}
  ~ScopedGetTimer() {
    auto end   = std::chrono::high_resolution_clock::now();
    auto us    = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
    Metrics::instance().observe_get_latency_us(static_cast<uint64_t>(us));
  }
private:
  std::chrono::high_resolution_clock::time_point start_;
};
