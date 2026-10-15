#include "source/extensions/filters/http/gcp_authn/token_cache.h"

#include "source/common/common/hash.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

namespace {
uint64_t generateCacheKey(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                          const std::optional<std::string>& fingerprint) {
  uint64_t key = MessageUtil::hash(audience);
  if (fingerprint.has_value()) {
    key = HashUtil::xxHash64(fingerprint.value(), key);
  }
  return key;
}
} // namespace

std::optional<std::string>
TokenCacheImpl::lookUp(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                       const std::optional<std::string>& fingerprint) {
  uint64_t key = generateCacheKey(audience, fingerprint);
  typename LRUCache::ScopedLookup lookup(&lru_cache_, key);
  if (lookup.found()) {
    GcpToken* const found_token = lookup.value();
    // Verify that there is no hash collision by doing a deep comparison on both Audience and
    // fingerprint.
    if (found_token->fingerprint != fingerprint ||
        !Protobuf::util::MessageDifferencer::Equals(found_token->audience, audience)) {
      return std::nullopt;
    }
    // Verify the validness of the token by checking its expiration time field.
    if (found_token->expires_at > 0 &&
        DateUtil::nowToSeconds(time_source_) + JwtVerify::kClockSkewInSecond >
            found_token->expires_at) {
      // Remove the expired entry.
      lru_cache_.remove(key);
      return std::nullopt;
    }
    // Return the valid token string.
    return found_token->token;
  }
  // Return empty/nullopt if no entry is found or it was expired.
  return std::nullopt;
}

void TokenCacheImpl::insert(std::unique_ptr<GcpToken> token) {
  uint64_t key = generateCacheKey(token->audience, token->fingerprint);
  // Release the token to transfer the ownership.
  lru_cache_.insert(key, token.release(), 1);
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
