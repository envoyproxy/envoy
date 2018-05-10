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
 * The object to store config and caches. It has following objects:
 * * config_: the config.
 * * jwks_cache_ for Jwks cache.
 * * extractor_ for token extraction
 * In the future, it will have token cache.
 * It is per-thread and stored in thread local.
 */
class DataStore : public ThreadLocal::ThreadLocalObject {
public:
  // Load the config from envoy config.
  DataStore(const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config)
      : config_(config), jwks_cache_(JwksCache::create(config_)),
        extractor_(Extractor::create(config_)) {}

  // Get the Config.
  const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config() const {
    return config_;
  }

  // Get the JwksStore object.
  JwksCache& getJwksCache() { return *jwks_cache_; }

  // Get the token  extractor.
  const Extractor& getExtractor() const { return *extractor_; }

private:
  // Store the config.
  const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config_;
  // The JwksCache object.
  JwksCachePtr jwks_cache_;
  // The object to extract tokens.
  ExtractorConstPtr extractor_;
};

// The factory to create per-thread auth store object.
class DataStoreFactory : public Logger::Loggable<Logger::Id::config> {
public:
  DataStoreFactory(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config,
      Server::Configuration::FactoryContext& context)
      : config_(config), tls_(context.threadLocal().allocateSlot()), cm_(context.clusterManager()) {
    tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<DataStore>(config_);
    });
    ENVOY_LOG(info, "Loaded JwtAuthConfig: {}", config_.DebugString());
  }

  // Get per-thread auth store object.
  DataStore& store() { return tls_->getTyped<DataStore>(); }

  Upstream::ClusterManager& cm() { return cm_; }

private:
  // The auth config.
  ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication config_;
  // Thread local slot to store per-thread auth store
  ThreadLocal::SlotPtr tls_;
  // the cluster manager object.
  Upstream::ClusterManager& cm_;
};
typedef std::shared_ptr<DataStoreFactory> DataStoreFactorySharedPtr;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
