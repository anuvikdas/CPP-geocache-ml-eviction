#pragma once
#include <list>
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <vector>
#include <memory>

#include "eviction.hpp"

class LruCache {
public:
    explicit LruCache(size_t capacity)
      : cap_(capacity), strategy_(std::make_shared<LRUStrategy>()) {}

    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }

    void put(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = value;
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        items_.emplace_front(key, value);
        map_[key] = items_.begin();

        if (map_.size() > cap_) evict_one_unlocked();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return map_.size();
    }

    void set_strategy(std::shared_ptr<EvictionStrategy> s) {
        std::lock_guard<std::mutex> lock(mu_);
        strategy_ = std::move(s);
    }

private:
    void evict_one_unlocked() {
        auto candidates = build_candidates_unlocked(8);
        std::optional<std::string> victim;
        if (strategy_) victim = strategy_->choose_victim(candidates);

        if (!victim.has_value()) {
            auto &node = items_.back();
            map_.erase(node.first);
            items_.pop_back();
            return;
        }
        auto m = map_.find(*victim);
        if (m != map_.end()) {
            items_.erase(m->second);
            map_.erase(m);
        } else {
            auto &node = items_.back();
            map_.erase(node.first);
            items_.pop_back();
        }
    }

    std::vector<std::string> build_candidates_unlocked(size_t max_n) const {
        std::vector<std::string> cands; cands.reserve(max_n);
        auto it = items_.rbegin();
        for (size_t i=0; it != items_.rend() && i<max_n; ++it, ++i) cands.push_back(it->first);
        return cands;
    }

private:
    mutable std::mutex mu_;
    size_t cap_;
    std::list<std::pair<std::string, std::string>> items_;
    std::unordered_map<std::string,
      std::list<std::pair<std::string, std::string>>::iterator> map_;
    std::shared_ptr<EvictionStrategy> strategy_;
};
