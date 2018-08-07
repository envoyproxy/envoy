#pragma once

#include "envoy/api/v2/core/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Grpc {

// Per-service factory for Grpc::AsyncClients. This factory is thread aware and will instantiate
// with thread local state. Clients will use ThreadLocal::Instance::dispatcher() for event handling.
class AsyncClientFactory {
public:
  virtual ~AsyncClientFactory() {}

  /**
   * Create a gRPC::AsyncClient.
   * @return AsyncClientPtr async client.
   */
  virtual AsyncClientPtr create() PURE;
};

typedef std::unique_ptr<AsyncClientFactory> AsyncClientFactoryPtr;

// Singleton gRPC client manager. Grpc::AsyncClientManager can be used to create per-service
// Grpc::AsyncClientFactory instances. All manufactured Grpc::AsyncClients must
// be destroyed before the AsyncClientManager can be safely destructed.
class AsyncClientManager {
public:
  virtual ~AsyncClientManager() {}

  /**
   * Create a Grpc::AsyncClients factory for a service. Validation of the service is performed and
   * will raise an exception on failure.
   * @param grpc_service envoy::api::v2::core::GrpcService configuration.
   * @param scope stats scope.
   * @param skip_cluster_check if set to true skips checks for cluster presence and being statically
   * configured.
   * @return AsyncClientFactoryPtr factory for grpc_service.
   * @throws EnvoyException when grpc_service validation fails.
   */
  virtual AsyncClientFactoryPtr
  factoryForGrpcService(const envoy::api::v2::core::GrpcService& grpc_service, Stats::Scope& scope,
                        bool skip_cluster_check) PURE;
};

typedef std::unique_ptr<AsyncClientManager> AsyncClientManagerPtr;

} // namespace Grpc
} // namespace Envoy
