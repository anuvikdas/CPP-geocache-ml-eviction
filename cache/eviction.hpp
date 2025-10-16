#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <utility>
#include <cstdint>

#include "key_stats.hpp"
#include "../third_party/httplib.h"
#include "../third_party/json.hpp"

using json = nlohmann::json;

// Strategy interface
struct EvictionStrategy {
  virtual ~EvictionStrategy() = default;
  virtual std::optional<std::string>
  choose_victim(const std::vector<std::string>& candidates) = 0;
};

// Pure LRU fallback
struct LRUStrategy : EvictionStrategy {
  std::optional<std::string>
  choose_victim(const std::vector<std::string>& candidates) override {
    if (candidates.empty()) return std::nullopt;
    return candidates.back();
  }
};

// ML-driven eviction (calls FastAPI sidecar /score)
struct MLEvictionStrategy : EvictionStrategy {
  std::string host;
  int         port;
  int         timeout_ms = 30;  // very short to keep hot path safe

  explicit MLEvictionStrategy(std::string h = "127.0.0.1", int p = 5000)
  : host(std::move(h)), port(p) {}

  std::optional<std::string>
  choose_victim(const std::vector<std::string>& candidates) override {
    if (candidates.empty()) return std::nullopt;

    // Build feature payload
    auto snap = KeyStatsStore::instance().snapshot_of(candidates);
    json payload = json::array();
    const std::uint64_t now_us = nowMicros();

    for (const auto& k : candidates) {
      auto it = snap.find(k);
      std::uint64_t access = 0, last = 0, size = 0, cost = 50;
      if (it != snap.end()) {
        access = it->second.access_count;
        last   = it->second.last_access_us;
        size   = it->second.size_bytes;
        cost   = it->second.fetch_cost_ms;
      }
      const std::uint64_t recency =
          (last == 0) ? static_cast<std::uint64_t>(1000000000000ULL) : (now_us - last);

      payload.push_back({
        {"key", k},
        {"recency_us",    recency},
        {"access_count",  access},
        {"size_bytes",    size},
        {"fetch_cost_ms", cost}
      });
    }

    try {
      httplib::Client cli(host, port);

      // Use chrono-based overloads to avoid overload ambiguity in some setups
      using namespace std::chrono;
      const auto to = milliseconds(timeout_ms);
      cli.set_connection_timeout(to);
      cli.set_read_timeout(to);
      cli.set_write_timeout(to);

      auto res = cli.Post("/score", payload.dump(), "application/json");
      if (!res || res->status != 200) {
        // Sidecar unreachable or error → decline so caller falls back to LRU
        return std::nullopt;
      }

      auto resp = json::parse(res->body);
      double best = 1e9;
      std::optional<std::string> victim;

      for (const auto& row : resp) {
        const std::string key   = row.value("key", "");
        const double      reuse = row.value("reuse_prob", 0.0);
        if (key.empty()) continue;
        if (reuse < best) { best = reuse; victim = key; }
      }
      return victim; // may still be nullopt if response empty
    } catch (...) {
      return std::nullopt; // any exception → safe fallback to LRU
    }
  }

private:
  static std::uint64_t nowMicros() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
      steady_clock::now().time_since_epoch()).count();
  }
};

