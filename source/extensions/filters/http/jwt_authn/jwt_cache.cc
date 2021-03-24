#include "extensions/filters/http/jwt_authn/jwt_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

JwtCache::JwtCache(int cache_size) { jwt_cache_.setMaxSize(cache_size); }

::google::jwt_verify::Jwt* JwtCache::find(const std::string& token) {
  ::google::jwt_verify::Jwt* jwt_cache_lookup{};
  SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>::ScopedLookup lookup(&jwt_cache_, token);
  if (lookup.found()) {
    jwt_cache_lookup = lookup.value();
    if (jwt_cache_lookup) {
      if (jwt_cache_lookup->verifyTimeConstraint(absl::ToUnixSeconds(absl::Now())) ==
          ::google::jwt_verify::Status::JwtExpired) {
        jwt_cache_.remove(token);
      }
    }
  }
  return jwt_cache_lookup;
}

void JwtCache::add(const std::string& token, ::google::jwt_verify::Jwt* jwt) {
  jwt_cache_.insert(token, jwt, 1);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
