#include <iostream>
#include <string>

#include "../third_party/httplib.h"   // HTTP server
#include "../third_party/json.hpp"    // JSON
#include "../cache/lru_cache.hpp"     // our cache
#include "../cache/metrics.hpp"
#include "../cache/timer.hpp"
#include "../cache/key_stats.hpp"



using json = nlohmann::json;

int main() { 
    LruCache cache(100); 
    httplib::Server svr; 

    //health check 
     svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    //fetch a value (GET)
     svr.Get("/get", [&](const httplib::Request& req, httplib::Response& res) {
        Metrics::instance().inc_get_requests();
        ScopedGetTimer _timer; // measures this request's latency
        
        if (!req.has_param("key")) {
            res.status = 400;
            res.set_content("missing key", "text/plain");
            return;
        }
        auto key = req.get_param_value("key");
        auto val = cache.get(key);
        if (!val) {
            res.status = 404; 
            res.set_content("not found", "text/plain");
            return;
        }
        KeyStatsStore::instance().touch(key, val->size());
        Metrics::instance().inc_hits();
        json out = { {"key", key}, {"value", *val} };
        res.set_content(out.dump(), "application/json");
    });

    svr.Put("/put", [&](const httplib::Request& req, httplib::Response& res) {
        Metrics::instance().inc_put_requests();

        try {
            auto body = json::parse(req.body);                   // parse JSON
            if (!body.contains("key") || !body.contains("value")) {
                res.status = 400; 
                res.set_content("body must contain key and value", "text/plain"); 
                return;
            }
            std::string key = body["key"].get<std::string>();    // type-safe
            std::string value = body["value"].get<std::string>();
            cache.put(key, value);                                // upsert
            KeyStatsStore::instance().touch(key, value.size());
            Metrics::instance().set_current_size(cache.size());
            json out = { {"status", "ok"}, {"size", cache.size()} };
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;                                     // bad JSON
            res.set_content(std::string("invalid json: ") + e.what(), "text/plain");
        }
    });


    // human-friendly stats
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