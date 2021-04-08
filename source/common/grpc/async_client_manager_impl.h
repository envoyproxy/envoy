#pragma once

#include <memory>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/grpc/stat_names.h"

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

class AsyncClientManagerImpl : public AsyncClientManager, Logger::Loggable<Logger::Id::grpc> {
public:
  AsyncClientManagerImpl(Upstream::ClusterManager& cm, ThreadLocal::Instance& tls,
                         TimeSource& time_source, Api::Api& api, const StatNames& stat_names);
  RawAsyncClientSharedPtr
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& config, Stats::Scope& scope,
                            bool skip_cluster_check) override {
    RawAsyncClientSharedPtr client;
    client = raw_async_client_cache_->getCache(config);
    if (client != nullptr) {
      ENVOY_LOG(debug, "return client cache.\n");
      return client;
    }
    client = factoryForGrpcService(config, scope, skip_cluster_check)->create();
    raw_async_client_cache_->setCache(config, client);
    return client;
  }
  // Grpc::AsyncClientManager
  AsyncClientFactoryPtr factoryForGrpcService(const envoy::config::core::v3::GrpcService& config,
                                              Stats::Scope& scope,
                                              bool skip_cluster_check) override;

private:
  class RawAsyncClientCache : public ThreadLocal::ThreadLocalObject {
  public:
    void setCache(const envoy::config::core::v3::GrpcService& config,
                  const RawAsyncClientSharedPtr& client) {
      std::uint64_t key = hashByType(config);
      cache_[key] = client;
    }
    RawAsyncClientSharedPtr getCache(const envoy::config::core::v3::GrpcService& config) const {
      std::uint64_t key = hashByType(config);
      auto it = cache_.find(key);
      if (it == cache_.end()) {
        return nullptr;
      }
      return it->second;
    }

  private:
    absl::flat_hash_map<uint64_t, RawAsyncClientSharedPtr> cache_;
    uint64_t hashByType(const envoy::config::core::v3::GrpcService& config) const {
      uint64_t key = 0;
      switch (config.target_specifier_case()) {
      case envoy::config::core::v3::GrpcService::TargetSpecifierCase::kEnvoyGrpc:
        key = MessageUtil::hash(config.envoy_grpc());
        break;
      case envoy::config::core::v3::GrpcService::TargetSpecifierCase::kGoogleGrpc:
        key = MessageUtil::hash(config.google_grpc());
        break;
      case envoy::config::core::v3::GrpcService::TargetSpecifierCase::TARGET_SPECIFIER_NOT_SET:
        NOT_REACHED_GCOVR_EXCL_LINE;
      }
      return key;
    }
  };
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
