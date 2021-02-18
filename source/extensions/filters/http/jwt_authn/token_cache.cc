#include "extensions/filters/http/jwt_authn/token_cache.h"

using std::chrono::system_clock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

TokenCache::TokenCache(int cache_size)
    : SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>(cache_size) {}

TokenCache::~TokenCache() { clear(); }

void TokenCache::find(const std::string& token, bool& cache_hit) {
  ::google::jwt_verify::Jwt* jwt_cache;
  cache_hit = false;
  TokenCache::ScopedLookup lookup(this, token);
  if (lookup.found()) {
    jwt_cache = lookup.value();
    if (jwt_cache->verifyTimeConstraint(absl::ToUnixSeconds(absl::Now())) == Status::JwtExpired) {
      this->remove(token);
      return;
    }
    cache_hit = true;
  }
}
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
