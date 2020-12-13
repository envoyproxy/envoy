#include "extensions/filters/http/jwt_authn/token_cache.h"

using std::chrono::system_clock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
// The number of entries in JWT cache.
const int kJwtCacheSize = 100;

TokenCache::TokenCache() : SimpleLRUCache<std::string, TokenResult>(kJwtCacheSize) {}

TokenCache::~TokenCache() { clear(); }

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
