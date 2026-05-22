#include "source/extensions/filters/http/gcp_authn/token_cache.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

absl::optional<std::string>
TokenCacheImpl::lookUp(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience) {
  uint64_t key = MessageUtil::hash(audience);
  typename LRUCache::ScopedLookup lookup(&lru_cache_, key);
  if (lookup.found()) {
    GcpToken* const found_token = lookup.value();
    // Verify that there is no hash collision by doing a deep comparison on the Audience message.
    if (!Protobuf::util::MessageDifferencer::Equals(found_token->audience, audience)) {
      return absl::nullopt;
    }
    // Verify the validness of the token by checking its expiration time field.
    if (found_token->expires_at > 0 &&
        DateUtil::nowToSeconds(time_source_) + JwtVerify::kClockSkewInSecond >
            found_token->expires_at) {
      // Remove the expired entry.
      lru_cache_.remove(key);
      return absl::nullopt;
    }
    // Return the valid token string.
    return found_token->token;
  }
  // Return empty/nullopt if no entry is found or it was expired.
  return absl::nullopt;
}

void TokenCacheImpl::insert(std::unique_ptr<GcpToken> token) {
  uint64_t key = MessageUtil::hash(token->audience);
  // Release the token to transfer the ownership.
  lru_cache_.insert(key, token.release(), 1);
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
