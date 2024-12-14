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

#endif // TTL_CACHE_H