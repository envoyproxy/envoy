#ifndef TTL_CACHE_IMPL_H
#define TTL_CACHE_IMPL_H

#include "ttl_cache.h"

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

#endif // TTL_CACHE_IMPL_H