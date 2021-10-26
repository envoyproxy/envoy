#pragma once

#include "envoy/api/api.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/grpc/stat_names.h"

namespace Envoy {
namespace Grpc {

class AsyncClientFactoryImpl : public AsyncClientFactory {
public:
  AsyncClientFactoryImpl(Upstream::ClusterManager& cm,
                         const envoy::config::core::v3::GrpcService& config,
                         bool skip_cluster_check, TimeSource& time_source);
  RawAsyncClientPtr createUncachedRawAsyncClient() override;

private:
  Upstream::ClusterManager& cm_;
  const envoy::config::core::v3::GrpcService config_;
  TimeSource& time_source_;
};

class GoogleAsyncClientFactoryImpl : public AsyncClientFactory {
public:
  GoogleAsyncClientFactoryImpl(ThreadLocal::Instance& tls, ThreadLocal::Slot* google_tls_slot,
                               Stats::Scope& scope,
                               const envoy::config::core::v3::GrpcService& config, Api::Api& api,
                               const StatNames& stat_names);
  RawAsyncClientPtr createUncachedRawAsyncClient() override;

private:
  ThreadLocal::Instance& tls_;
  ThreadLocal::Slot* google_tls_slot_;
  Stats::ScopeSharedPtr scope_;
  const envoy::config::core::v3::GrpcService config_;
  Api::Api& api_;
  const StatNames& stat_names_;
};

class AsyncClientManagerImpl : public AsyncClientManager {
public:
  AsyncClientManagerImpl(Upstream::ClusterManager& cm, ThreadLocal::Instance& tls,
                         TimeSource& time_source, Api::Api& api, const StatNames& stat_names);
  RawAsyncClientSharedPtr
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
                            bool skip_cluster_check, CacheOption cache_option) override;

  AsyncClientFactoryPtr factoryForGrpcService(const envoy::config::core::v3::GrpcService& config,
                                              Stats::Scope& scope,
                                              bool skip_cluster_check) override;
  class RawAsyncClientCache : public ThreadLocal::ThreadLocalObject {
  public:
    explicit RawAsyncClientCache(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {
      cache_eviction_timer_ = dispatcher.createTimer([this] { evictIdleEntries(); });
    }
    void setCache(const envoy::config::core::v3::GrpcService& config,
                  const RawAsyncClientSharedPtr& client);

    RawAsyncClientSharedPtr getCache(const envoy::config::core::v3::GrpcService& config);

  private:
    void evictIdleEntries();
    void resetEvictionTimer();
    struct CacheEntry {
      CacheEntry(const envoy::config::core::v3::GrpcService& config,
                 RawAsyncClientSharedPtr const& client)
          : config_(config), client_(client) {}
      envoy::config::core::v3::GrpcService config_;
      RawAsyncClientSharedPtr client_;
      MonotonicTime accessed_time_;
    };
    absl::flat_hash_map<envoy::config::core::v3::GrpcService, std::list<CacheEntry>::iterator,
                        MessageUtil, MessageUtil>
        look_up_;
    std::list<CacheEntry> cache_;
    Event::Dispatcher& dispatcher_;
    // The timer fire every 50s, and clean up cache entries that haven't been accesses since last
    // eviction. Therefore, any entry that haven't been used for 50s will be evicted when the timer
    // fires but some entries can linger for up to 100s.
    Envoy::Event::TimerPtr cache_eviction_timer_;
    static constexpr std::chrono::milliseconds RefreshInterval{50000};
  };

private:
  Upstream::ClusterManager& cm_;
  ThreadLocal::Instance& tls_;
  ThreadLocal::SlotPtr google_tls_slot_;
  TimeSource& time_source_;
  Api::Api& api_;
  const StatNames& stat_names_;
  ThreadLocal::TypedSlot<RawAsyncClientCache> raw_async_client_cache_;
};

} // namespace Grpc
} // namespace Envoy
