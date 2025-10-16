#pragma once
#include <list>
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>

/*
LRU design:
- Doubly-linked list `items_` holds (key, value). Front = MRU, back = LRU.
- Hash map `map_` maps key -> iterator into list for O(1) lookup/move.
- Mutex guards all operations for Day 1 simplicity.
*/

class LruCache {
public:
    explicit LruCache(size_t capacity) : cap_(capacity) {}

    // Return value if present; move node to MRU
    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        // move node to front (MRU)
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second; // value
    }

    // Insert or update key; evict LRU if capacity exceeded
    void put(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = value;                   // update
            items_.splice(items_.begin(), items_, it->second); // move to MRU
            return;
        }
        // new item at MRU
        items_.emplace_front(key, value);
        map_[key] = items_.begin();

        if (map_.size() > cap_) {
            // evict LRU at back
            auto &node = items_.back();
            map_.erase(node.first);
            items_.pop_back();
        }
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return map_.size();
    }

private:
    mutable std::mutex mu_;
    size_t cap_;
    // FRONT = MRU, BACK = LRU
    std::list<std::pair<std::string, std::string>> items_;
    std::unordered_map<std::string,
        std::list<std::pair<std::string, std::string>>::iterator> map_;
};
