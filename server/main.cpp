#include <iostream>
#include <string>
#include <cstdlib>
#include <chrono>

#include "../third_party/httplib.h"
#include "../third_party/json.hpp"

#include "../cache/lru_cache.hpp"
#include "../cache/metrics.hpp"
#include "../cache/timer.hpp"
#include "../cache/key_stats.hpp"
#include "../cache/eviction.hpp"
#include "../cache/logger.hpp"

using json = nlohmann::json;

static inline uint64_t since_us(std::chrono::high_resolution_clock::time_point t0) {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - t0).count();
    return static_cast<uint64_t>(us);
}

int main() {
    // Cache with capacity 100 items
    LruCache cache(100);

    // CSV logger (writes to ../data/access_log.csv)
    CsvLogger::instance().init();

    // Choose eviction policy via env:
    //   EVICTION_MODE=ML (optional ML_HOST, ML_PORT), otherwise LRU
    const char* mode = std::getenv("EVICTION_MODE");
    if (mode && std::string(mode) == "ML") {
        const char* host = std::getenv("ML_HOST"); if (!host) host = "127.0.0.1";
        int port = 5000; if (const char* p = std::getenv("ML_PORT")) port = std::atoi(p);
        cache.set_strategy(std::make_shared<MLEvictionStrategy>(host, port));
        std::cout << "Eviction policy: ML (" << host << ":" << port << ")\n";
    } else {
        cache.set_strategy(std::make_shared<LRUStrategy>());
        std::cout << "Eviction policy: LRU (default)\n";
    }

    httplib::Server svr;

    // Health
    svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    // GET value
    svr.Get("/get", [&](const httplib::Request& req, httplib::Response& res) {
        Metrics::instance().inc_get_requests();
        ScopedGetTimer _timer; // feeds latency histogram
        auto t0 = std::chrono::high_resolution_clock::now();

        if (!req.has_param("key")) {
            res.status = 400;
            res.set_content("missing key", "text/plain");
            return;
        }

        const auto key = req.get_param_value("key");
        auto val = cache.get(key);
        if (!val) {
            Metrics::instance().inc_misses();
            CsvLogger::instance().write("GET", key, /*hit=*/false, since_us(t0), /*size_bytes=*/0);
            res.status = 404;
            res.set_content("not found", "text/plain");
            return;
        }

        Metrics::instance().inc_hits();
        KeyStatsStore::instance().touch(key, val->size());
        CsvLogger::instance().write("GET", key, /*hit=*/true, since_us(t0), val->size());

        json out = { {"key", key}, {"value", *val} };
        res.set_content(out.dump(), "application/json");
    });

    // PUT (insert/update)
    svr.Put("/put", [&](const httplib::Request& req, httplib::Response& res) {
        Metrics::instance().inc_put_requests();
        auto t0 = std::chrono::high_resolution_clock::now();

        try {
            auto body = json::parse(req.body);
            if (!body.contains("key") || !body.contains("value")) {
                res.status = 400;
                res.set_content("body must contain key and value", "text/plain");
                return;
            }
            std::string key = body["key"].get<std::string>();
            std::string value = body["value"].get<std::string>();

            cache.put(key, value);
            KeyStatsStore::instance().touch(key, value.size());
            Metrics::instance().set_current_size(cache.size());
            CsvLogger::instance().write("PUT", key, /*hit=*/true, since_us(t0), value.size());

            json out = { {"status", "ok"}, {"size", cache.size()} };
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("invalid json: ") + e.what(), "text/plain");
        }
    });

    // Human-readable stats (JSON)
    svr.Get("/stats", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(Metrics::instance().to_json(), "application/json");
    });

    // Prometheus exposition
    svr.Get("/metrics", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(Metrics::instance().to_prom(), "text/plain; version=0.0.4");
    });

    std::cout << "Starting cache server on http://127.0.0.1:8080\n";
    svr.listen("0.0.0.0", 8080);
    return 0;
}
