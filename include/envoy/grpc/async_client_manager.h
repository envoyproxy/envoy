#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/stats/scope.h"

namespace Envoy {
namespace Grpc {

// Per-service factory for Grpc::RawAsyncClients. This factory is thread aware and will instantiate
// with thread local state. Clients will use ThreadLocal::Instance::dispatcher() for event handling.
class AsyncClientFactory {
public:
  virtual ~AsyncClientFactory() = default;

  /**
   * Create a gRPC::RawAsyncClient.
   * @return RawAsyncClientPtr async client.
   */
  virtual RawAsyncClientPtr create() PURE;
};

using AsyncClientFactoryPtr = std::unique_ptr<AsyncClientFactory>;

enum class AsyncClientFactoryClusterChecks { Skip, ValidateStatic, ValidateStaticDuringBootstrap };

// Singleton gRPC client manager. Grpc::AsyncClientManager can be used to create per-service
// Grpc::AsyncClientFactory instances. All manufactured Grpc::AsyncClients must
// be destroyed before the AsyncClientManager can be safely destructed.
class AsyncClientManager {
public:
  virtual ~AsyncClientManager() = default;

  /**
   * Create a Grpc::AsyncClients factory for a service. Validation of the service is performed and
   * will raise an exception on failure.
   * @param grpc_service envoy::config::core::v3::GrpcService configuration.
   * @param scope stats scope.
   * @param checks Skip will skip checking cluster validity, ValidateStatic checks for
   * cluster presence and being statically configured, ValidateStaticDuringBootstrap checks for
   * cluster presence and being statically configured with methods that are not threadsafe but can
   * be used during bootstrap.
   * @return AsyncClientFactoryPtr factory for grpc_service.
   * @throws EnvoyException when grpc_service validation fails.
   */
  virtual AsyncClientFactoryPtr
  factoryForGrpcService(const envoy::config::core::v3::GrpcService& grpc_service,
                        Stats::Scope& scope, AsyncClientFactoryClusterChecks checks) PURE;
};

using AsyncClientManagerPtr = std::unique_ptr<AsyncClientManager>;

} // namespace Grpc
} // namespace Envoy
