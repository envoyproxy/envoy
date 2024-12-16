#ifndef TTL_CACHE_H
#define TTL_CACHE_H

#include "caches/cache.hpp"
#include "caches/cache_policy.hpp"
#include "caches/lru_cache_policy.hpp"
#include <chrono>
#include <optional>
#include <unordered_map>

// Define the cache type using LRU policy
using lru_cache_t = caches::fixed_sized_cache<std::string, int, caches::LRUCachePolicy>;

template<typename Key, typename Value, template<typename> class Policy>
class TTLCache {
public:
    TTLCache(size_t max_size, std::chrono::milliseconds ttl);

    void Put(const Key& key, const Value& value);
    std::optional<Value> Get(const Key& key);
    size_t Size() const;

private:
    caches::fixed_sized_cache<Key, Value, Policy> cache_;
    std::chrono::milliseconds ttl_;
    std::unordered_map<Key, std::chrono::steady_clock::time_point> expiration_times_;
};

template<typename Key, typename Value, template<typename> class Policy>
TTLCache<Key, Value, Policy>::TTLCache(size_t max_size, std::chrono::milliseconds ttl)
    : cache_(max_size), ttl_(ttl) {}

template<typename Key, typename Value, template<typename> class Policy>
void TTLCache<Key, Value, Policy>::Put(const Key& key, const Value& value) {
    auto now = std::chrono::steady_clock::now();
    expiration_times_[key] = now + ttl_;
    cache_.Put(key, value);
}

template<typename Key, typename Value, template<typename> class Policy>
std::optional<Value> TTLCache<Key, Value, Policy>::Get(const Key& key) {
    auto now = std::chrono::steady_clock::now();
    if (expiration_times_.find(key) != expiration_times_.end() && expiration_times_[key] > now) {
        auto value = cache_.Get(key);
        if (value) {
            return (*value);
        }
    } else {
        cache_.Remove(key);
        expiration_times_.erase(key);
    }
    return std::nullopt;
}

template<typename Key, typename Value, template<typename> class Policy>
size_t TTLCache<Key, Value, Policy>::Size() const {
    return cache_.Size();
}

#endif // TTL_CACHE_H