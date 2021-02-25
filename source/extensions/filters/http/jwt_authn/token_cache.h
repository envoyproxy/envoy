#pragma once
#include <chrono>
#include <memory>
#include <string>

#include "envoy/common/time.h"

#include "absl/types/optional.h"
#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"
#include "simple_lru_cache/simple_lru_cache_inl.h"

using ::google::jwt_verify::Status;
using ::google::simple_lru_cache::SimpleLRUCache;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// Cache key is the JWT string, value is parsed JWT struct.

class TokenCache : public SimpleLRUCache<std::string, ::google::jwt_verify::Jwt> {
public:
  TokenCache(int cache_size);
  ~TokenCache();
  ::google::jwt_verify::Jwt* find(const std::string& token, bool& cache_hit);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy