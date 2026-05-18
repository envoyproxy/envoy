#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/common/jwt/jwt.h"
#include "source/common/jwt/simple_lru_cache_inl.h"
#include "source/common/jwt/verify.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using LRUCache = ::Envoy::SimpleLruCache::SimpleLRUCache<std::string, GcpToken>;

class TokenCacheImpl : public Logger::Loggable<Logger::Id::init> {
public:
  TokenCacheImpl(const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& config,
                 TimeSource& time_source)
      : lru_cache_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, cache_size, 0)),
        time_source_(time_source) {}

  TokenCacheImpl() = delete;

  absl::optional<std::string> lookUp(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience);
  void insert(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience, std::unique_ptr<GcpToken> token);

  uint64_t capacity() { return lru_cache_.maxSize(); }

  ~TokenCacheImpl() {
    // Remove all entries from the cache.
    lru_cache_.clear();
  }

private:
  LRUCache lru_cache_;
  TimeSource& time_source_;
};

class ThreadLocalCache : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalCache(const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& config,
                   TimeSource& time_source)
      : cache_(config, time_source) {}
  TokenCacheImpl& cache() { return cache_; }

private:
  // The lifetime and ownership of cache object is tied to ThreadLocalCache object.
  TokenCacheImpl cache_;
};

struct TokenCache {
  TokenCache(const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& cache_config,
             Envoy::Server::Configuration::FactoryContext& context)
      : tls(context.serverFactoryContext().threadLocal()) {
    tls.set([cache_config](Envoy::Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalCache>(cache_config, dispatcher.timeSource());
    });
  }
  Envoy::ThreadLocal::TypedSlot<ThreadLocalCache> tls;
};

inline absl::optional<std::string> TokenCacheImpl::lookUp(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience) {
  std::string key = audience.SerializeAsString();
  typename LRUCache::ScopedLookup lookup(&lru_cache_, key);
  if (lookup.found()) {
    GcpToken* const found_token = lookup.value();
    // Verify the validness of the token by checking its expiration time field.
    if (found_token->expires_at_ > 0 &&
        DateUtil::nowToSeconds(time_source_) + JwtVerify::kClockSkewInSecond > found_token->expires_at_) {
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

inline void TokenCacheImpl::insert(
    const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
    std::unique_ptr<GcpToken> token) {
  std::string key = audience.SerializeAsString();
  // Release the token to transfer the ownership.
  lru_cache_.insert(key, token.release(), 1);
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
