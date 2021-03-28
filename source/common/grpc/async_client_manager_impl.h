#pragma once

#include "envoy/api/api.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/grpc/stat_names.h"
#include <unordered_map>

namespace Envoy {
namespace Grpc {

class AsyncClientFactoryImpl : public AsyncClientFactory {
public:
  AsyncClientFactoryImpl(Upstream::ClusterManager& cm,
                         const envoy::config::core::v3::GrpcService& config,
                         bool skip_cluster_check, TimeSource& time_source);

  RawAsyncClientPtr create() override;

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

  RawAsyncClientPtr create() override;

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

  // Grpc::AsyncClientManager
  AsyncClientFactoryPtr factoryForGrpcService(const envoy::config::core::v3::GrpcService& config,
                                              Stats::Scope& scope,
                                              bool skip_cluster_check) override;

  RawAsyncClientSharedPtr
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
                            bool skip_cluster_check) override {
    return async_client_cache_.getOrCreate(config, scope, skip_cluster_check);
  }

private:
  class ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
  public:
    void setCache(const envoy::config::core::v3::GrpcService& config,
                  RawAsyncClientSharedPtr client) {
      const std::uint64_t key = MessageUtil::hash(config.google_grpc());
      cache[key] = client;
    }
    RawAsyncClientSharedPtr getCache(const envoy::config::core::v3::GrpcService& config) {
      const std::uint64_t key = MessageUtil::hash(config.google_grpc());
      if (cache.find(key) == cache.end()) {
        return nullptr;
      }
      return cache[key];
    }

  private:
    absl::flat_hash_map<uint64_t, RawAsyncClientSharedPtr> cache;
  };
  class AsyncClientCache {
  public:
    AsyncClientCache(Grpc::AsyncClientManager& async_client_manager,
                     ThreadLocal::SlotAllocator& tls)
        : async_client_manager_(async_client_manager), thread_local_cache_(tls) {
      thread_local_cache_.set(
          [](Event::Dispatcher&) { return std::make_shared<ThreadLocalCache>(); });
    }
    RawAsyncClientSharedPtr getOrCreate(const envoy::config::core::v3::GrpcService& config,
                                        Stats::Scope& scope, bool skip_cluster_check) {
      RawAsyncClientSharedPtr client = thread_local_cache_->getCache(config);
      if (client != nullptr) {
        return client;
      }
      client =
          async_client_manager_.factoryForGrpcService(config, scope, skip_cluster_check)->create();
      thread_local_cache_->setCache(config, client);
      return client;
    }

  private:
    Grpc::AsyncClientManager& async_client_manager_;
    ThreadLocal::TypedSlot<ThreadLocalCache> thread_local_cache_;
  };
  Upstream::ClusterManager& cm_;
  ThreadLocal::Instance& tls_;
  ThreadLocal::SlotPtr google_tls_slot_;
  TimeSource& time_source_;
  Api::Api& api_;
  const StatNames& stat_names_;
  AsyncClientCache async_client_cache_;
};

} // namespace Grpc
} // namespace Envoy
