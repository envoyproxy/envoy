#pragma once

#include "envoy/grpc/async_client_manager.h"
#include "envoy/singleton/manager.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Grpc {

class AsyncClientFactoryImpl : public AsyncClientFactory {
public:
  AsyncClientFactoryImpl(Upstream::ClusterManager& cm, const std::string& cluster_name);

  AsyncClientPtr create() override;

private:
  Upstream::ClusterManager& cm_;
  const std::string cluster_name_;
};

class AsyncClientManagerImpl : public AsyncClientManager {
public:
  AsyncClientManagerImpl(Upstream::ClusterManager& cm, ThreadLocal::Instance& tls);

  // Grpc::AsyncClientManager
  AsyncClientFactoryPtr factoryForGrpcService(const envoy::api::v2::GrpcService& grpc_service,
                                              Stats::Scope& scope) override;

private:
  Upstream::ClusterManager& cm_;
};

} // namespace Grpc
} // namespace Envoy
