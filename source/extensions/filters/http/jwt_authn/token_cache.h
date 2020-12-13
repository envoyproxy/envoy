#pragma once
#include <chrono>
#include <string>

#include "envoy/common/time.h"

#include "extensions/filters/http/jwt_authn/simple_lru_cache_impl.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// The value of a TokenCache entry.
struct TokenResult {
  // cache expire time, default cache for 5 minutes
  MonotonicTime expiration_time_;
  // verification status
  Status status_;
};

// The key of the cache is a JWT,
// and the value is of type TokenResult.
class TokenCache : public SimpleLRUCache<std::string, TokenResult> {
public:
  TokenCache();
  ~TokenCache();
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy