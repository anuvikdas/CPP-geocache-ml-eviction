#include <iostream>
#include <string>

#include "../third_party/httplib.h"   // HTTP server
#include "../third_party/json.hpp"    // JSON
#include "../cache/lru_cache.hpp"     // our cache

using json = nlohmann::json;

int main() {
    LruCache cache(100);
    httplib::Server svr;

    // health check
    svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });

    // fetch a value (GET)
    svr.Get("/get", [&](const httplib::Request& req, httplib::Response& res) {
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
        json out = { {"key", key}, {"value", *val} };
        res.set_content(out.dump(), "application/json");
    });

    // insert/update a value (PUT)
    svr.Put("/put", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body); // parse JSON
            if (!body.contains("key") || !body.contains("value")) {
                res.status = 400;
                res.set_content("body must contain key and value", "text/plain");
                return;
            }
            std::string key = body["key"].get<std::string>();
            std::string value = body["value"].get<std::string>();
            cache.put(key, value);
            json out = { {"status", "ok"}, {"size", cache.size()} };
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400; // bad JSON
            res.set_content(std::string("invalid json: ") + e.what(), "text/plain");
        }
    });

    std::cout << "Starting cache server on http://127.0.0.1:8080\n";
    svr.listen("0.0.0.0", 8080);
    return 0;
}