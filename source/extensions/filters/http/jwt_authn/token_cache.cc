#include "extensions/filters/http/jwt_authn/token_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

TokenCache::TokenCache(int cache_size)
    : SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>(cache_size) {}

::google::jwt_verify::Jwt* TokenCache::find(const std::string& token) {
  auto jwt_cache = lookup(token);
  if (jwt_cache) {
    if (jwt_cache->verifyTimeConstraint(absl::ToUnixSeconds(absl::Now())) ==
        ::google::jwt_verify::Status::JwtExpired) {
      remove(token);
    }
  }
  return jwt_cache;
}
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
