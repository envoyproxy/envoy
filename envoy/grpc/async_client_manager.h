#pragma once

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/stats/scope.h"

#include "source/common/common/hash.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Grpc {

class AsyncClientFactoryImpl;

// Per-service factory for Grpc::RawAsyncClients. This factory is thread aware and will instantiate
// with thread local state. Clients will use ThreadLocal::Instance::dispatcher() for event handling.
class AsyncClientFactory {
public:
  virtual ~AsyncClientFactory() = default;

  /**
   * Create a gRPC::RawAsyncClient.
   * Prefer AsyncClientManager::getOrCreateRawAsyncClient() to creating uncached raw async client
   * from factory directly. Only call this method when the raw async client must be owned
   * exclusively. For example, some filters pass *this reference to raw client. In this case, the
   * client must be destroyed before the filter instance. In this case, the grpc client must be
   * owned by the filter instance exclusively.
   * @return RawAsyncClientPtr async client or an error status.
   */
  virtual absl::StatusOr<RawAsyncClientPtr> createUncachedRawAsyncClient() PURE;

private:
  friend class AsyncClientFactoryImpl;
};

using AsyncClientFactoryPtr = std::unique_ptr<AsyncClientFactory>;

// The key of the raw gRPC client cache. Two keys are equal when their configs serialize to the same
// bytes.
class GrpcServiceConfigWithHashKey {
public:
  GrpcServiceConfigWithHashKey() { updateKey(); }

  explicit GrpcServiceConfigWithHashKey(envoy::config::core::v3::GrpcService config)
      : config_(std::move(config)) {
    updateKey();
  }

  template <typename H> friend H AbslHashValue(H h, const GrpcServiceConfigWithHashKey& wrapper) {
    return H::combine(std::move(h), wrapper.pre_computed_hash_);
  }

  std::size_t getPreComputedHash() const { return pre_computed_hash_; }

  friend bool operator==(const GrpcServiceConfigWithHashKey& lhs,
                         const GrpcServiceConfigWithHashKey& rhs) {
    return lhs.pre_computed_hash_ == rhs.pre_computed_hash_ &&
           lhs.serialized_config_ == rhs.serialized_config_;
  }

  const envoy::config::core::v3::GrpcService& config() const { return config_; }

  void setConfig(const envoy::config::core::v3::GrpcService& g) {
    config_ = g;
    updateKey();
  }

private:
  friend class GrpcServiceConfigWithHashKeyPeer;

  // Serializes the config deterministically and hashes the bytes. Equal bytes mean equal configs.
  void updateKey() {
    serialized_config_ = MessageUtil::serializeDeterministically(config_);
    pre_computed_hash_ = HashUtil::xxHash64(serialized_config_);
  }

  envoy::config::core::v3::GrpcService config_;
  std::string serialized_config_;
  std::size_t pre_computed_hash_;
};

// Singleton gRPC client manager. Grpc::AsyncClientManager can be used to create per-service
// Grpc::AsyncClientFactory instances. All manufactured Grpc::AsyncClients must
// be destroyed before the AsyncClientManager can be safely destructed.
class AsyncClientManager {
public:
  virtual ~AsyncClientManager() = default;

  // TODO(diazalan) deprecate old getOrCreateRawAsyncClient once all filters have been transitioned
  /**
   * Create a Grpc::RawAsyncClient. The async client is cached thread locally and shared across
   * different filter instances.
   * @param grpc_service envoy::config::core::v3::GrpcService configuration.
   * @param scope stats scope.
   * @param skip_cluster_check if set to true skips checks for cluster presence and being statically
   * configured.
   * @param cache_option always use cache or use cache when runtime is enabled.
   * @return RawAsyncClientPtr a grpc async client or an invalid status.
   */
  virtual absl::StatusOr<RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& grpc_service,
                            Stats::Scope& scope, bool skip_cluster_check) PURE;

  /**
   * Create a Grpc::RawAsyncClient. The async client is cached thread locally and shared across
   * different filter instances.
   * @param grpc_service Envoy::Grpc::GrpcServiceConfigWithHashKey which contains config and
   * hash key.
   * @param scope stats scope.
   * @param skip_cluster_check if set to true skips checks for cluster presence and being statically
   * configured.
   * @param cache_option always use cache or use cache when runtime is enabled.
   * @return RawAsyncClientPtr a grpc async client.
   * @throws EnvoyException when grpc_service validation fails.
   */
  virtual absl::StatusOr<RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClientWithHashKey(const GrpcServiceConfigWithHashKey& grpc_service,
                                       Stats::Scope& scope, bool skip_cluster_check) PURE;

  /**
   * Create a Grpc::AsyncClients factory for a service. Validation of the service is performed and
   * will raise an exception on failure.
   * @param grpc_service envoy::config::core::v3::GrpcService configuration.
   * @param scope stats scope.
   * @param skip_cluster_check if set to true skips checks for cluster presence and being statically
   * configured.
   * @return AsyncClientFactoryPtr factory for grpc_service or an error status.
   */
  virtual absl::StatusOr<AsyncClientFactoryPtr>
  factoryForGrpcService(const envoy::config::core::v3::GrpcService& grpc_service,
                        Stats::Scope& scope, bool skip_cluster_check) PURE;
};

using AsyncClientManagerPtr = std::unique_ptr<AsyncClientManager>;

} // namespace Grpc
} // namespace Envoy
