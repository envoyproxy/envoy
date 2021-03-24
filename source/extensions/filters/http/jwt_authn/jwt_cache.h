#pragma once
#include <chrono>
#include <memory>
#include <string>

#include "envoy/common/time.h"

#include "absl/types/optional.h"
#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"
#include "simple_lru_cache/simple_lru_cache_inl.h"

using ::google::simple_lru_cache::SimpleLRUCache;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// Cache key is the JWT string, value is parsed JWT struct.

// The default number of entries in JWT cache is 100.
constexpr int kJwtCacheSize = 100;

class JwtCache {
public:
  JwtCache(int cache_size);
  ~JwtCache() { jwt_cache_.clear(); }
  ::google::jwt_verify::Jwt* find(const std::string& token);
  void add(const std::string& token, ::google::jwt_verify::Jwt* jwt);

private:
  SimpleLRUCache<std::string, ::google::jwt_verify::Jwt> jwt_cache_{kJwtCacheSize};
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy