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

using LRUCache = ::Envoy::SimpleLruCache::SimpleLRUCache<uint64_t, GcpToken>;

class TokenCacheImpl : public Logger::Loggable<Logger::Id::init> {
public:
  TokenCacheImpl(const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& config,
                 TimeSource& time_source)
      : lru_cache_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, cache_size, 0)),
        time_source_(time_source) {}

  TokenCacheImpl() = delete;

  std::optional<std::string>
  lookUp(const envoy::extensions::filters::http::gcp_authn::v3::Audience& audience,
         const std::optional<std::string>& fingerprint);
  void insert(std::unique_ptr<GcpToken> token);

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

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
