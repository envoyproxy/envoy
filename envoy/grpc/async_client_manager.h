#pragma once

#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/stats/scope.h"

namespace Envoy {
// Forward-declared to avoid an include cycle.
namespace Http {
class HeaderEvaluator;
} // namespace Http

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

// A handle for a GrpcService config's `initial_metadata`, parsed using the substitution
// formatter extensions declared in its `formatters` (see
// `AsyncClientManager::parseGrpcServiceInitialMetadata()`). Formatter extensions may create state
// that can only be created on the main thread, so the metadata is parsed there, and async clients
// (which may be created on worker threads) share the parsed result. Hold it for as long as clients
// for the service may be created.
using GrpcServiceInitialMetadataPtr = std::shared_ptr<const Http::HeaderEvaluator>;

class GrpcServiceConfigWithHashKey {
public:
  GrpcServiceConfigWithHashKey() = default;

  // Bundles a `GrpcService` config with its parsed initial metadata handle (obtained on the main
  // thread via `AsyncClientManager::parseGrpcServiceInitialMetadata()`). The handle is required so
  // that every creation of an async client from a `GrpcService` must account for formatter
  // extensions; pass null when they cannot be supported (e.g. a service selected at request time on
  // a worker thread).
  GrpcServiceConfigWithHashKey(const envoy::config::core::v3::GrpcService& config,
                               GrpcServiceInitialMetadataPtr initial_metadata)
      : config_(config), pre_computed_hash_(Envoy::MessageUtil::hash(config)),
        initial_metadata_(std::move(initial_metadata)) {}

  template <typename H> friend H AbslHashValue(H h, const GrpcServiceConfigWithHashKey& wrapper) {
    return H::combine(std::move(h), wrapper.pre_computed_hash_);
  }

  std::size_t getPreComputedHash() const { return pre_computed_hash_; }

  friend bool operator==(const GrpcServiceConfigWithHashKey& lhs,
                         const GrpcServiceConfigWithHashKey& rhs) {
    if (lhs.pre_computed_hash_ == rhs.pre_computed_hash_) {
      return Protobuf::util::MessageDifferencer::Equivalent(lhs.config_, rhs.config_);
    }
    return false;
  }

  const envoy::config::core::v3::GrpcService& config() const { return config_; }

  // The parsed initial metadata for this service (may be null). Note this does not participate in
  // the hash/equality, which are derived from the config only.
  const GrpcServiceInitialMetadataPtr& initialMetadata() const { return initial_metadata_; }

  void setConfig(const envoy::config::core::v3::GrpcService& g,
                 GrpcServiceInitialMetadataPtr initial_metadata) {
    config_ = g;
    pre_computed_hash_ = Envoy::MessageUtil::hash(g);
    initial_metadata_ = std::move(initial_metadata);
  }

private:
  envoy::config::core::v3::GrpcService config_;
  std::size_t pre_computed_hash_;
  GrpcServiceInitialMetadataPtr initial_metadata_;
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
   * @param initial_metadata handle for the service's parsed `initial_metadata`, obtained on the
   * main thread via `parseGrpcServiceInitialMetadata()`. Required on the first, uncached creation.
   * @return RawAsyncClientPtr a grpc async client or an invalid status.
   */
  virtual absl::StatusOr<RawAsyncClientSharedPtr>
  getOrCreateRawAsyncClient(const envoy::config::core::v3::GrpcService& grpc_service,
                            Stats::Scope& scope, bool skip_cluster_check,
                            GrpcServiceInitialMetadataPtr initial_metadata) PURE;

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
   * @param initial_metadata handle for the service's parsed `initial_metadata`, obtained on the
   * main thread via `parseGrpcServiceInitialMetadata()`.
   * @return AsyncClientFactoryPtr factory for grpc_service or an error status.
   */
  virtual absl::StatusOr<AsyncClientFactoryPtr>
  factoryForGrpcService(const envoy::config::core::v3::GrpcService& grpc_service,
                        Stats::Scope& scope, bool skip_cluster_check,
                        GrpcServiceInitialMetadataPtr initial_metadata) PURE;

  /**
   * Parse `grpc_service.initial_metadata` using the substitution formatter extensions declared in
   * `grpc_service.formatters`, and return a handle to the result. Formatters may create
   * state that can only be created on the main thread (both when the formatter is loaded and when
   * it parses a command), so this must be called at config-load time on the main thread. Pass the
   * returned handle into async client creation, and hold it for as long as clients for the service
   * may be created; clients (which may be created lazily on worker threads, e.g. in a per-worker
   * filter-chain callback) share the parsed metadata rather than parsing it themselves.
   *
   * Must be called on the main thread.
   * @param grpc_service envoy::config::core::v3::GrpcService configuration.
   * @return a handle on success, or an error if the formatters or the initial metadata fail to
   * parse.
   */
  virtual absl::StatusOr<GrpcServiceInitialMetadataPtr>
  parseGrpcServiceInitialMetadata(const envoy::config::core::v3::GrpcService& grpc_service) PURE;
};

using AsyncClientManagerPtr = std::unique_ptr<AsyncClientManager>;

} // namespace Grpc
} // namespace Envoy
