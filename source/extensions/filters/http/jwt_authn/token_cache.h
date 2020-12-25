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

// The value of a TokenCache entry.
struct TokenCacheData {
  // jwt parsing status
  Status jwt_status_;
  // parsed JWT
  std::unique_ptr<::google::jwt_verify::Jwt> jwt_;
  // If valid, the  verification status
  absl::optional<Status> sig_status_;
};

// The key of the cache is a JWT,
// and the value is of type TokenCacheData.
class TokenCache : public SimpleLRUCache<std::string, TokenCacheData> {
public:
  TokenCache();
  ~TokenCache();
  void addTokenCacheData(const std::string& token, ::google::jwt_verify::Jwt& jwt,
                         uint64_t token_exp, Status& status);
  bool lookupTokenCacheData(const std::string& token, ::google::jwt_verify::Jwt& jwt,
                            Status& status);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy