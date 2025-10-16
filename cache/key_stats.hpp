#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <chrono>

struct KeyStats {
  uint64_t access_count = 0;
  uint64_t last_access_us = 0;   // epoch micros
  uint64_t size_bytes = 0;       // value length for now
  uint64_t fetch_cost_ms = 50;   // simulate origin cost (tunable)
};

class KeyStatsStore {
public:
  static KeyStatsStore& instance() { static KeyStatsStore s; return s; }

  void touch(const std::string& key, size_t size_bytes) {
    auto now_us = nowMicros();
    std::lock_guard<std::mutex> lock(mu_);
    auto &st = map_[key];
    st.access_count++;
    st.last_access_us = now_us;
    st.size_bytes = size_bytes;
  }

  void set_fetch_cost_ms(const std::string& key, uint64_t cost) {
    std::lock_guard<std::mutex> lock(mu_);
    map_[key].fetch_cost_ms = cost;
  }

  // snapshot a subset of keys (you’ll pass the eviction candidates here)
  std::unordered_map<std::string, KeyStats>
  snapshot_of(const std::vector<std::string>& keys) {
    std::unordered_map<std::string, KeyStats> out;
    std::lock_guard<std::mutex> lock(mu_);
    for (auto &k : keys) {
      auto it = map_.find(k);
      if (it != map_.end()) out.emplace(k, it->second);
    }
    return out;
  }

private:
  static uint64_t nowMicros() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
  }
  std::mutex mu_;
  std::unordered_map<std::string, KeyStats> map_;
};
