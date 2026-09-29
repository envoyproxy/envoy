#pragma once

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/stat_names.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Grpc {

class AsyncClientFactoryImpl : public AsyncClientFactory {
public:
  AsyncClientFactoryImpl(const envoy::config::core::v3::GrpcService& config,
                         bool skip_cluster_check,
                         Server::Configuration::ServerFactoryContext& context,
                         GrpcServiceInitialMetadataPtr initial_metadata,
                         absl::Status& creation_status);
  absl::StatusOr<RawAsyncClientPtr> createUncachedRawAsyncClient() override;

private:
  const envoy::config::core::v3::GrpcService config_;
  Server::Configuration::ServerFactoryContext& context_;
  const GrpcServiceInitialMetadataPtr initial_metadata_;
};

class GoogleAsyncClientFactoryImpl : public AsyncClientFactory {
public:
  GoogleAsyncClientFactoryImpl(const envoy::config::core::v3::GrpcService& config,
                               ThreadLocal::Slot* google_tls_slot, Stats::Scope& scope,
                               Server::Configuration::ServerFactoryContext& context,
                               const StatNames& stat_names,
                               GrpcServiceInitialMetadataPtr initial_metadata,
                               absl::Status& creation_status);
  absl::StatusOr<RawAsyncClientPtr> createUncachedRawAsyncClient() override;

private:
  ThreadLocal::Slot* google_tls_slot_;
  Stats::ScopeSharedPtr scope_;
  const envoy::config::core::v3::GrpcService config_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  const StatNames& stat_names_;
  const GrpcServiceInitialMetadataPtr initial_metadata_;
};

class AsyncClientManagerImpl : public AsyncClientManager, Logger::Loggable<Logger::Id::grpc> {
public:
  AsyncClientManagerImpl(
      const envoy::config::bootstrap::v3::Bootstrap::GrpcAsyncClientManagerConfig& config,
      Server::Configuration::ServerFactoryContext& context, const StatNames& stat_names);
  absl::StatusOr<RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
                            bool skip_cluster_check,
                            GrpcServiceInitialMetadataPtr initial_metadata) override;

  absl::StatusOr<RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClientWithHashKey(const GrpcServiceConfigWithHashKey& config_with_hash_key,
                                       Stats::Scope& scope, bool skip_cluster_check) override;

  absl::StatusOr<AsyncClientFactoryPtr>
  factoryForGrpcService(const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
                        bool skip_cluster_check,
                        GrpcServiceInitialMetadataPtr initial_metadata) override;

  absl::StatusOr<GrpcServiceInitialMetadataPtr>
  parseGrpcServiceInitialMetadata(const envoy::config::core::v3::GrpcService& config,
                                  Server::Configuration::GenericFactoryContext& context) override;
  absl::StatusOr<GrpcServiceInitialMetadataPtr> parseGrpcServiceInitialMetadataForServer(
      const envoy::config::core::v3::GrpcService& config) override;
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
  Server::Configuration::ServerFactoryContext& context_;
  ThreadLocal::SlotSharedPtr google_tls_slot_;
  const StatNames& stat_names_;
  ThreadLocal::TypedSlot<RawAsyncClientCache> raw_async_client_cache_;
};

} // namespace Grpc
} // namespace Envoy
