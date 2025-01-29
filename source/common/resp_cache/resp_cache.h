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
  // ttl_seconds -1 means use the default TTL
  virtual bool Insert(const Envoy::Http::RequestHeaderMap& headers, const T& value,
                      int ttl_seconds = -1) = 0;

  // Method to get a cached response based on the cache key
  virtual absl::optional<T> Get(const Envoy::Http::RequestHeaderMap& headers) = 0;

  // Method to erase a value from the cache
  virtual bool Erase(const Envoy::Http::RequestHeaderMap& headers) = 0;

  // Public getter method for max cache size (number of objects that can be in cache)
  virtual std::size_t getMaxCacheSize() const = 0;

  // Publig getter method for current cache size (number of objects in cache)
  virtual size_t Size() const = 0;
};

} // namespace RespCache
} // namespace Common
} // namespace Envoy
