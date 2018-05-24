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
 * Making cache as a thread local object, its read/write operations don't need to be protected.
 * Now it only has jwks_cache, but in the future, it will have token cache: to cache the tokens
 * with their verification results.
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

/**
 * The filer config object to hold config and relavant objects.
 */
class FilterConfig : public Logger::Loggable<Logger::Id::config> {
public:
  FilterConfig(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& proto_config,
      Server::Configuration::FactoryContext& context)
      : proto_config_(proto_config), tls_(context.threadLocal().allocateSlot()),
        cm_(context.clusterManager()) {
    tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<ThreadLocalCache>(proto_config_);
    });
    ENVOY_LOG(info, "Loaded JwtAuthConfig: {}", proto_config_.DebugString());
    extractor_ = Extractor::create(proto_config_);
  }

  // Get the Config.
  const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication&
  getProtoConfig() const {
    return proto_config_;
  }

  // Get per-thread cache object.
  ThreadLocalCache& getCache() { return tls_->getTyped<ThreadLocalCache>(); }

  Upstream::ClusterManager& cm() { return cm_; }

  // Get the token  extractor.
  const Extractor& getExtractor() const { return *extractor_; }

private:
  // The proto config.
  ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication proto_config_;
  // Thread local slot to store per-thread auth store
  ThreadLocal::SlotPtr tls_;
  // the cluster manager object.
  Upstream::ClusterManager& cm_;
  // The object to extract tokens.
  ExtractorConstPtr extractor_;
};
typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
