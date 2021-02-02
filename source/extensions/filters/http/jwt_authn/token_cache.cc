#include "extensions/filters/http/jwt_authn/token_cache.h"

using std::chrono::system_clock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

TokenCache::TokenCache(int cache_size)
    : SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>(cache_size) {}

TokenCache::~TokenCache() { clear(); }

bool TokenCache::lookupTokenCache(const std::string& token) {
  TokenCache::ScopedLookup lookup(this, token);
  if (lookup.found()) {
    ::google::jwt_verify::Jwt* jwt_cache = lookup.value();
    if (jwt_cache->verifyTimeConstraint(absl::ToUnixSeconds(absl::Now())) == Status::JwtExpired) {
      this->remove(token);
      return false;
    }
    return true;
  }
  return false;
}
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
