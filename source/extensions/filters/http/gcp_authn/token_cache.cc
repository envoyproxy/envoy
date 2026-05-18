#include "source/extensions/filters/http/gcp_authn/token_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

absl::optional<std::string> TokenCacheImpl::lookUp(
    const envoy::extensions::filters::http::gcp_authn::v3::GcpTokenRequest& token_request) {
  std::string key = token_request.SerializeAsString();
  typename LRUCache::ScopedLookup lookup(&lru_cache_, key);
  if (lookup.found()) {
    GcpToken* const found_token = lookup.value();
    // Verify the validness of the token by checking its expiration time field.
    if (found_token->expires_at_ > 0 &&
        DateUtil::nowToSeconds(time_source_) + JwtVerify::kClockSkewInSecond >
            found_token->expires_at_) {
      // Remove the expired entry.
      lru_cache_.remove(key);
      return absl::nullopt;
    }
    // Return the valid token string.
    return found_token->token_;
  }
  // Return empty/nullopt if no entry is found or it was expired.
  return absl::nullopt;
}

void TokenCacheImpl::insert(
    const envoy::extensions::filters::http::gcp_authn::v3::GcpTokenRequest& token_request,
    std::unique_ptr<GcpToken> token) {
  std::string key = token_request.SerializeAsString();
  // Release the token to transfer the ownership.
  lru_cache_.insert(key, token.release(), 1);
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
