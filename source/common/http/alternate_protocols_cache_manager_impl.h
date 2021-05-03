#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"

#include "common/http/alternate_protocols_cache.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

class AlternateProtocolsCacheManagerImpl : public AlternateProtocolsCacheManager,
                                           public Singleton::Instance {
public:
  AlternateProtocolsCacheManagerImpl(TimeSource& time_source, ThreadLocal::Instance& tls)
      : time_source_(time_source), tls_(tls) {}

  // AlternateProtocolsCacheManager
  AlternateProtocolsCacheSharedPtr getCache(
      const envoy::config::core::v3::AlternateProtocolsCacheOptions& config) override;

private:
  struct ActiveCache {
    ActiveCache(
        const envoy::config::core::v3::AlternateProtocolsCacheOptions& config,
        AlternateProtocolsCacheSharedPtr cache)
        : config_(config), cache_(cache) {}

    const envoy::config::core::v3::AlternateProtocolsCacheOptions& config_;
    AlternateProtocolsCacheSharedPtr cache_;
  };

  TimeSource& time_source_;
  ThreadLocal::Instance& tls_;

  // Map from config name to active cache for that config.
  absl::flat_hash_map<std::string, ActiveCache> caches_;
};

class AlternateProtocolsCacheManagerFactoryImpl : public AlternateProtocolsCacheManagerFactory {
public:
  AlternateProtocolsCacheManagerFactoryImpl(Singleton::Manager& singleton_manager,
                                            TimeSource& time_source,
                                            ThreadLocal::Instance& tls)
      : singleton_manager_(singleton_manager), time_source_(time_source), tls_(tls) {}


  AlternateProtocolsCacheManagerSharedPtr get() override;

private:
  Singleton::Manager& singleton_manager_;
  TimeSource& time_source_;
  ThreadLocal::Instance& tls_;
};

} // namespace Http
} // namespace Envoy
