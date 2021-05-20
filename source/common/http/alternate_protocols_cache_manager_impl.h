#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/alternate_protocols_cache.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Http {

class AlternateProtocolsCacheManagerImpl : public AlternateProtocolsCacheManager,
                                           public Singleton::Instance {
public:
  AlternateProtocolsCacheManagerImpl(TimeSource& time_source, ThreadLocal::Instance& tls);

  // AlternateProtocolsCacheManager
  AlternateProtocolsCacheSharedPtr
  getCache(const envoy::config::core::v3::AlternateProtocolsCacheOptions& options) override;

private:
  // Contains a cache and the options associated with it.
  struct CacheWithOptions {
    CacheWithOptions(const envoy::config::core::v3::AlternateProtocolsCacheOptions& options,
                     AlternateProtocolsCacheSharedPtr cache)
        : options_(options), cache_(cache) {}

    const envoy::config::core::v3::AlternateProtocolsCacheOptions& options_;
    AlternateProtocolsCacheSharedPtr cache_;
  };

  // Per-thread state.
  struct State : public ThreadLocal::ThreadLocalObject {
    // Map from config name to cache for that config.
    absl::flat_hash_map<std::string, CacheWithOptions> caches_;
  };

  TimeSource& time_source_;

  // Thread local state for the cache.
  ThreadLocal::TypedSlot<State> slot_;
};

class AlternateProtocolsCacheManagerFactoryImpl : public AlternateProtocolsCacheManagerFactory {
public:
  AlternateProtocolsCacheManagerFactoryImpl(Singleton::Manager& singleton_manager,
                                            TimeSource& time_source, ThreadLocal::Instance& tls)
      : singleton_manager_(singleton_manager), time_source_(time_source), tls_(tls) {}

  AlternateProtocolsCacheManagerSharedPtr get() override;

private:
  Singleton::Manager& singleton_manager_;
  TimeSource& time_source_;
  ThreadLocal::Instance& tls_;
};

} // namespace Http
} // namespace Envoy
