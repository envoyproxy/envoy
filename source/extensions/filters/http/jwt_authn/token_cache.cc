#include "extensions/filters/http/jwt_authn/token_cache.h"

using std::chrono::system_clock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
// The number of entries in JWT cache.
const int kJwtCacheSize = 100;

TokenCache::TokenCache() : SimpleLRUCache<std::string, TokenCacheData>(kJwtCacheSize) {}

TokenCache::~TokenCache() { clear(); }

void TokenCache::addTokenCacheData(const std::string& token, ::google::jwt_verify::Jwt& jwt,
                                   uint64_t token_exp, Status& status) {
  TokenCacheData* token_cache_data = new TokenCacheData();
  token_cache_data->jwt_status_ = status;
  jwt.exp_ = std::min(jwt.exp_, token_exp);
  token_cache_data->jwt_.reset(&jwt);
  this->insert(token, token_cache_data, 1);
}

bool TokenCache::lookupTokenCacheData(const std::string& token, ::google::jwt_verify::Jwt& jwt,
                                      Status& status) {
  TokenCache::ScopedLookup lookup(this, token);
  if (lookup.found()) {
    TokenCacheData* token_cache_data = lookup.value();
    if (token_cache_data->jwt_->verifyTimeConstraint(absl::ToUnixSeconds(absl::Now())) ==
        Status::JwtExpired) {
      this->remove(token);
      return false;
    }
    jwt = *token_cache_data->jwt_;
    status = token_cache_data->jwt_status_;
    return true;
  }
  return false;
}
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
