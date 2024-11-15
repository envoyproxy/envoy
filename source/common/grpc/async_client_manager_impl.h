#pragma once

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/grpc/stat_names.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Grpc {

class AsyncClientFactoryImpl : public AsyncClientFactory {
public:
  AsyncClientFactoryImpl(Upstream::ClusterManager& cm,
                         const envoy::config::core::v3::GrpcService& config,
                         bool skip_cluster_check, TimeSource& time_source,
                         absl::Status& creation_status);
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
                               const envoy::config::core::v3::GrpcService& config,
                               Server::Configuration::CommonFactoryContext& context,
                               const StatNames& stat_names, absl::Status& creation_status);
  RawAsyncClientPtr createUncachedRawAsyncClient() override;

private:
  ThreadLocal::Instance& tls_;
  ThreadLocal::Slot* google_tls_slot_;
  Stats::ScopeSharedPtr scope_;
  const envoy::config::core::v3::GrpcService config_;
  Server::Configuration::CommonFactoryContext& factory_context_;
  const StatNames& stat_names_;
};

class AsyncClientManagerImpl : public AsyncClientManager {
public:
  AsyncClientManagerImpl(
      Upstream::ClusterManager& cm, ThreadLocal::Instance& tls,
      Server::Configuration::CommonFactoryContext& context, const StatNames& stat_names,
      const envoy::config::bootstrap::v3::Bootstrap::GrpcAsyncClientManagerConfig& config);
  absl::StatusOr<RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
                            bool skip_cluster_check) override;

  absl::StatusOr<RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClientWithHashKey(const GrpcServiceConfigWithHashKey& config_with_hash_key,
                                       Stats::Scope& scope, bool skip_cluster_check) override;

  absl::StatusOr<AsyncClientFactoryPtr>
  factoryForGrpcService(const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
                        bool skip_cluster_check) override;
  class RawAsyncClientCache : public ThreadLocal::ThreadLocalObject {
  public:
    explicit RawAsyncClientCache(Event::Dispatcher& dispatcher,
                                 std::chrono::milliseconds max_cached_entry_idle_duration);
    void setCache(const GrpcServiceConfigWithHashKey& config_with_hash_key,
                  const RawAsyncClientSharedPtr& client);

    RawAsyncClientSharedPtr getCache(const GrpcServiceConfigWithHashKey& config_with_hash_key);

  private:
    void evictEntriesAndResetEvictionTimer();
    struct CacheEntry {
      CacheEntry(const GrpcServiceConfigWithHashKey& config_with_hash_key,
                 RawAsyncClientSharedPtr const& client, MonotonicTime create_time)
          : config_with_hash_key_(config_with_hash_key), client_(client),
            accessed_time_(create_time) {}
      GrpcServiceConfigWithHashKey config_with_hash_key_;
      RawAsyncClientSharedPtr client_;
      MonotonicTime accessed_time_;
    };
    using LruList = std::list<CacheEntry>;
    LruList lru_list_;
    absl::flat_hash_map<GrpcServiceConfigWithHashKey, LruList::iterator> lru_map_;
    Event::Dispatcher& dispatcher_;
    Envoy::Event::TimerPtr cache_eviction_timer_;
    const std::chrono::milliseconds max_cached_entry_idle_duration_;
  };

private:
  ThreadLocal::Instance& tls_;
  Upstream::ClusterManager& cm_; // Need to track outside of `context_` due to startup ordering.
  Server::Configuration::CommonFactoryContext& context_;
  ThreadLocal::SlotPtr google_tls_slot_;
  const StatNames& stat_names_;
  ThreadLocal::TypedSlot<RawAsyncClientCache> raw_async_client_cache_;
};

} // namespace Grpc
} // namespace Envoy
