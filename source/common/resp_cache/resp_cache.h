#pragma once

// Interface class for Response Cache

#pragma once

#include <string>

//#include "simple_cache.h"

namespace Envoy {
namespace Common {
namespace RespCache {

template <typename T> class RespCache {
public:
  virtual ~RespCache() = default;

  // Method to insert a value into the cache with a TTL
  virtual bool Insert(const std::string& key, const T& value, int ttl_seconds = -1) = 0;

  // Method to get a cached response based on the cache key
  virtual absl::optional<T> Get(const std::string& key) = 0;

  // Method to erase a value from the cache
  virtual bool Erase(const std::string& key) = 0;

  // Public getter method for max cache size (number of objects that can be in cache)
  virtual std::size_t getMaxCacheSize() const = 0;

  // Publig getter method for current cache size (number of objects in cache)
  virtual size_t Size() const = 0;
};

// Factory function to create RespCache instances
/*
std::unique_ptr<RespCache> createRespCache(const std::string& cache_type, std::size_t max_size, int
default_ttl_seconds, double eviction_candidate_ratio, double eviction_threshold_ratio,
Envoy::TimeSource& time_source) { if (cache_type == "simple") { return
std::make_unique<SimpleCache>(max_size, default_ttl_seconds, eviction_candidate_ratio,
eviction_threshold_ratio, time_source);
  }
  // TODO: add more cache types as needed
  return nullptr;
}*/

} // namespace RespCache
} // namespace Common
} // namespace Envoy
