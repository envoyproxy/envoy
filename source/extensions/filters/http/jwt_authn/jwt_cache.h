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

class JwtCache;
using JwtCachePtr = std::unique_ptr<JwtCache>;

class JwtCache {
public:
  virtual ~JwtCache() = default;

  // Find Cache JWT string. If found return parsed JWT struct otherwise no-op.
  virtual ::google::jwt_verify::Jwt* find(const std::string& token) PURE;

  // Add good JWT string and it's parsed JWT struct in Cache.
  virtual void add(const std::string& token, std::unique_ptr<::google::jwt_verify::Jwt>&& jwt) PURE;

  // JwtCache factory function.
  static JwtCachePtr create(bool enable_cache, int cache_size);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy