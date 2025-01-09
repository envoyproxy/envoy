#ifndef FIFO_CACHE_H
#define FIFO_CACHE_H

#include <unordered_map>
#include <mutex>
#include <optional>
#include <cstdint> // For uint16_t
#include <cstring> // For strcmp
#include <iostream> // For debug output
#include <chrono> // For std::chrono

namespace fifo_cache {

/**
  A simple cache class with TTL.
  It has FIFO eviction policy. Compared to other policies like LRU, this is memory efficient, because it does not need to store the order of elements.
  It restricts stored values to 16-bit unsigned integers. This also makes it memory efficient.
 */
class FIFOEvictionCache {
public:
    // By default, TTL will be 10 seconds.
    FIFOEvictionCache(std::size_t max_size, int default_ttl_seconds = 10)
        : max_cache_size(max_size), default_ttl_seconds(default_ttl_seconds) {}

    bool Insert(const char* key, uint16_t value, int ttl_seconds = -1) {
        std::lock_guard<std::mutex> lock(safe_op);
        const char* c_key = strdup(key);
        if (ttl_seconds == -1) {
            ttl_seconds = default_ttl_seconds;
        }
        auto expiration_time = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_seconds);
        CacheItem item = {value, expiration_time};
        auto it = cache_items_map.find(c_key);
        if (it == cache_items_map.end()) {
            if (cache_items_map.size() >= max_cache_size) {
                Evict();
            }
            cache_items_map[c_key] = item;
        } else {
            cache_items_map[c_key] = item;
        }
        return true;
    }

    bool Erase(const char* key) {
        std::lock_guard<std::mutex> lock(safe_op);
        auto it = cache_items_map.find(key);
        if (it != cache_items_map.end()) {
            free(const_cast<char*>(it->first));
            cache_items_map.erase(it);
            return true;
        }
        return false;
    }

    std::optional<uint16_t> Get(const char* key) {
        std::lock_guard<std::mutex> lock(safe_op);
        auto it = cache_items_map.find(key);
        if (it != cache_items_map.end()) {
            if (std::chrono::steady_clock::now() < it->second.expiration_time) {
                return it->second.value;
            } else {
                // Item has expired
                free(const_cast<char*>(it->first));
                cache_items_map.erase(it);
            }
        }
        return std::nullopt;
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(safe_op);
        return cache_items_map.size();
    }

    ~FIFOEvictionCache() {
        for (auto& pair : cache_items_map) {
            free(const_cast<char*>(pair.first));
        }
    }

private:
    struct CacheItem {
        uint16_t value;
        std::chrono::steady_clock::time_point expiration_time;
    };

    void Evict() {
        if (!cache_items_map.empty()) {
            auto it = cache_items_map.begin();
            free(const_cast<char*>(it->first));
            cache_items_map.erase(it);
        }
    }

    struct CharPtrHash {
        std::size_t operator()(const char* str) const {
            std::size_t hash = 0;
            while (*str) {
                hash = hash * 101 + *str++;
            }
            return hash;
        }
    };

    struct CharPtrEqual {
        bool operator()(const char* lhs, const char* rhs) const {
            return std::strcmp(lhs, rhs) == 0;
        }
    };

    std::unordered_map<const char*, CacheItem, CharPtrHash, CharPtrEqual> cache_items_map;
    std::size_t max_cache_size;
    int default_ttl_seconds;
    mutable std::mutex safe_op;
};

} // namespace fifo_cache

#endif // FIFO_CACHE_H