#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"

#include "extensions/filters/http/jwt_authn/extractor.h"
#include "extensions/filters/http/jwt_authn/jwks_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * Since cache is global and need to proect its read/write operations in multi-threads.
 * But if it is a thread local object, not need to protect read/write, but cache entries
 * could not be shared across threads. Fast accessing of cache is more important than sharing
 * cache entries. Hence makes it as thread local cache.
 * The thread local object to store caches. Now it only has jwks_cache:
 * But in the future, it will have token cache; cache the tokens with their verification results.
 */
class ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
public:
  // Load the config from envoy config.
  ThreadLocalCache(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config) {
    jwks_cache_ = JwksCache::create(config);
  }

  // Get the JwksCache object.
  JwksCache& getJwksCache() { return *jwks_cache_; }

private:
  // The JwksCache object.
  JwksCachePtr jwks_cache_;
};

// The filer config.
class Config : public Logger::Loggable<Logger::Id::config> {
public:
  Config(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config,
         Server::Configuration::FactoryContext& context)
      : config_(config), tls_(context.threadLocal().allocateSlot()), cm_(context.clusterManager()) {
    tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<ThreadLocalCache>(config_);
    });
    ENVOY_LOG(info, "Loaded JwtAuthConfig: {}", config_.DebugString());
    extractor_ = Extractor::create(config_);
  }

  // Get the Config.
  const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config() const {
    return config_;
  }

  // Get per-thread auth store object.
  ThreadLocalCache& tl_cache() { return tls_->getTyped<ThreadLocalCache>(); }

  Upstream::ClusterManager& cm() { return cm_; }

  // Get the token  extractor.
  const Extractor& getExtractor() const { return *extractor_; }

private:
  // The auth config.
  ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication config_;
  // Thread local slot to store per-thread auth store
  ThreadLocal::SlotPtr tls_;
  // the cluster manager object.
  Upstream::ClusterManager& cm_;
  // The object to extract tokens.
  ExtractorConstPtr extractor_;
};
typedef std::shared_ptr<Config> ConfigSharedPtr;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
