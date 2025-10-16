#pragma once
#include <atomic>
#include <array>
#include <string>
#include <sstream>
// Removed unused: <chrono>, <mutex>, <iomanip>

class Metrics {
public:
  static Metrics& instance() {
    static Metrics m;
    return m;
  }

  // counters/gauges
  void inc_get_requests()   { get_requests_.fetch_add(1, std::memory_order_relaxed); }
  void inc_put_requests()   { put_requests_.fetch_add(1, std::memory_order_relaxed); }
  void inc_hits()           { hits_.fetch_add(1, std::memory_order_relaxed); }
  void inc_misses()         { misses_.fetch_add(1, std::memory_order_relaxed); }
  void set_current_size(size_t s) { current_size_.store(s, std::memory_order_relaxed); }

  // latency histogram (microseconds)
  // buckets (us): [100, 300, 1000, 3000, 10000, +inf]
  void observe_get_latency_us(uint64_t us) {
    // FIX 2: stop using buckets_.size(); use BUCKETS (or hist_.size()) instead.
    size_t idx = 0;
    while (idx + 1 < BUCKETS && us > bucket_bounds_us_[idx]) ++idx;
    hist_[idx].fetch_add(1, std::memory_order_relaxed);
  }

  // JSON summary for /stats
  std::string to_json() {
    std::ostringstream os;
    os << "{";
    os << "\"get_requests\":"   << get_requests_.load(std::memory_order_relaxed)   << ",";
    os << "\"put_requests\":"   << put_requests_.load(std::memory_order_relaxed)   << ",";
    os << "\"cache_hits\":"     << hits_.load(std::memory_order_relaxed)           << ",";
    os << "\"cache_misses\":"   << misses_.load(std::memory_order_relaxed)         << ",";
    os << "\"cache_current_size\":" << current_size_.load(std::memory_order_relaxed) << ",";
    os << "\"get_latency_histogram_us\":{";
    // Emit finite buckets + a label for the +Inf bucket
    for (size_t i = 0; i < bucket_bounds_us_.size(); ++i) {
      if (i) os << ",";
      os << "\"" << bucket_label(i) << "\":" << hist_[i].load(std::memory_order_relaxed);
    }
    os << ",\"gt_10000\":" << hist_[bucket_bounds_us_.size()].load(std::memory_order_relaxed);
    os << "}}";
    return os.str();
  }

  // Prometheus exposition for /metrics
  std::string to_prom() {
    std::ostringstream os;
    os << "# HELP cache_get_requests_total Total GET requests\n"
       << "# TYPE cache_get_requests_total counter\n"
       << "cache_get_requests_total " << get_requests_.load(std::memory_order_relaxed) << "\n";
    os << "# HELP cache_put_requests_total Total PUT requests\n"
       << "# TYPE cache_put_requests_total counter\n"
       << "cache_put_requests_total " << put_requests_.load(std::memory_order_relaxed) << "\n";
    os << "# HELP cache_hits_total Cache hits\n"
       << "# TYPE cache_hits_total counter\n"
       << "cache_hits_total " << hits_.load(std::memory_order_relaxed) << "\n";
    os << "# HELP cache_misses_total Cache misses\n"
       << "# TYPE cache_misses_total counter\n"
       << "cache_misses_total " << misses_.load(std::memory_order_relaxed) << "\n";
    os << "# HELP cache_current_size Current number of keys\n"
       << "# TYPE cache_current_size gauge\n"
       << "cache_current_size " << current_size_.load(std::memory_order_relaxed) << "\n";

    os << "# HELP cache_get_latency_us Latency histogram for GET (us)\n"
       << "# TYPE cache_get_latency_us histogram\n";

    // FIX 1: Correct Prometheus cumulative bucket math.
    // Accumulate ONLY finite buckets first, then add +Inf as the total.
    uint64_t cumulative = 0;

    // finite buckets (<= bound)
    for (size_t i = 0; i < bucket_bounds_us_.size(); ++i) {
      cumulative += hist_[i].load(std::memory_order_relaxed);
      os << "cache_get_latency_us_bucket{le=\"" << bucket_le_label(i) << "\"} "
         << cumulative << "\n";
    }

    // +Inf bucket includes everything
    cumulative += hist_[bucket_bounds_us_.size()].load(std::memory_order_relaxed);
    os << "cache_get_latency_us_bucket{le=\"+Inf\"} " << cumulative << "\n";

    // sum & count (sum is 0 in this MVP)
    os << "cache_get_latency_us_sum 0\n";
    os << "cache_get_latency_us_count " << cumulative << "\n";
    return os.str();
  }

private:
  Metrics() = default;

  std::string bucket_label(size_t i) const {
    if (i + 1 == BUCKETS) return "gt_10000"; // >10ms
    return std::string("le_") + std::to_string(bucket_bounds_us_[i]);
  }
  std::string bucket_le_label(size_t i) const {
    if (i + 1 == BUCKETS) return "10000";
    return std::to_string(bucket_bounds_us_[i]);
  }

  // counters/gauges
  std::atomic<uint64_t> get_requests_{0};
  std::atomic<uint64_t> put_requests_{0};
  std::atomic<uint64_t> hits_{0};
  std::atomic<uint64_t> misses_{0};
  std::atomic<uint64_t> current_size_{0};

  // histogram buckets
  static constexpr std::array<uint64_t,5> bucket_bounds_us_{100, 300, 1000, 3000, 10000};
  static constexpr size_t BUCKETS = bucket_bounds_us_.size() + 1; // +Inf
  std::array<std::atomic<uint64_t>, BUCKETS> hist_{}; // value-initialized to 0

  //Recent changes below: 

  // REMOVED: std::array<uint64_t, BUCKETS> buckets_{};
  // This array was only used to query size in loops (non-optimal).
  // Use BUCKETS or hist_.size() instead.
};
