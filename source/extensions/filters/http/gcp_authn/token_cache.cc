#include "source/extensions/filters/http/gcp_authn/token_cache.h"

#include "source/common/common/hash.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

namespace {
uint64_t generateCacheKey(absl::string_view serialized_audience,
                          const std::optional<std::string>& fingerprint) {
  uint64_t key = HashUtil::xxHash64(serialized_audience);
  if (fingerprint.has_value()) {
    key = HashUtil::xxHash64(fingerprint.value(), key);
  }
  return key;
}
} // namespace

std::optional<std::string>
TokenCacheImpl::lookUp(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
                       const std::optional<std::string>& fingerprint) {
  const std::string serialized_audience = MessageUtil::serializeDeterministically(audience);
  const uint64_t key = generateCacheKey(serialized_audience, fingerprint);
  typename LRUCache::ScopedLookup lookup(&lru_cache_, key);
  if (lookup.found()) {
    CachedToken* const found_token = lookup.value();
    // Verify that there is no hash collision by comparing the serialized audience and the
    // fingerprint.
    if (found_token->fingerprint != fingerprint ||
        found_token->serialized_audience != serialized_audience) {
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
  std::string serialized_audience = MessageUtil::serializeDeterministically(token->audience);
  const uint64_t key = generateCacheKey(serialized_audience, token->fingerprint);
  lru_cache_.insert(key,
                    new CachedToken{std::move(token->token), token->expires_at,
                                    std::move(token->fingerprint), std::move(serialized_audience)},
                    1);
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
