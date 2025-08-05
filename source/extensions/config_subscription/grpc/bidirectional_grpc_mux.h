#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/bidirectional_grpc_stream.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Config {

/**
 * Interface for components that can provide client resources for reverse xDS.
 */
class ClientResourceProvider {
public:
  virtual ~ClientResourceProvider() = default;

  /**
   * Get resources of this provider's type.
   * @param resource_names specific resources requested (empty = all)
   * @return vector of resources as Any protos
   */
  virtual std::vector<Protobuf::Any> getResources(
      const std::vector<std::string>& resource_names) const PURE;

  /**
   * Get the type URL for this provider's resources.
   */
  virtual std::string getTypeUrl() const PURE;

  /**
   * Get the current version for this provider's resources.
   */
  virtual std::string getVersionInfo() const PURE;
};

using ClientResourceProviderPtr = std::unique_ptr<ClientResourceProvider>;

/**
 * Registry for client resource providers by type URL.
 */
class ClientResourceRegistry {
public:
  void registerProvider(const std::string& type_url, ClientResourceProviderPtr provider);
  ClientResourceProvider* getProvider(const std::string& type_url) const;

private:
  absl::flat_hash_map<std::string, ClientResourceProviderPtr> providers_;
};

/**
 * Bidirectional GrpcMux that wraps the existing GrpcMux and adds
 * reverse xDS functionality.
 * 
 * This allows registration of client resource providers that can respond
 * to reverse xDS requests from management servers.
 */
class BidirectionalGrpcMuxImpl : public GrpcMux {
public:
  BidirectionalGrpcMuxImpl(std::shared_ptr<GrpcMux> base_mux);

  /**
   * Register a client resource provider for reverse xDS.
   */
  void registerClientResourceProvider(const std::string& type_url,
                                     ClientResourceProviderPtr provider);

  // GrpcMux interface - delegate to base mux
  void start() override;
  
  ScopedResume pause(const std::string& type_url) override;
  ScopedResume pause(const std::vector<std::string> type_urls) override;
  
  GrpcMuxWatchPtr addWatch(const std::string& type_url,
                          const absl::flat_hash_set<std::string>& resources,
                          SubscriptionCallbacks& callbacks,
                          OpaqueResourceDecoderSharedPtr resource_decoder,
                          const SubscriptionOptions& options) override;
  
  void requestOnDemandUpdate(const std::string& type_url,
                            const absl::flat_hash_set<std::string>& for_update) override;

  EdsResourcesCacheOptRef edsResourcesCache() override;
  
  absl::Status updateMuxSource(Grpc::RawAsyncClientPtr&& primary_async_client,
                              Grpc::RawAsyncClientPtr&& failover_async_client, Stats::Scope& scope,
                              BackOffStrategyPtr&& backoff_strategy,
                              const envoy::config::core::v3::ApiConfigSource& ads_config_source) override;

private:
  // The base GrpcMux that we wrap
  std::shared_ptr<GrpcMux> base_mux_;

  // Registry for reverse xDS providers
  ClientResourceRegistry client_resource_registry_;

  // Handle reverse xDS request and generate response (placeholder for now)
  void handleReverseRequest(const std::string& type_url,
                           const std::vector<std::string>& resource_names);
};

/**
 * Factory for creating bidirectional GrpcMux instances.
 */
class BidirectionalGrpcMuxFactory : public MuxFactory {
public:
  std::string name() const override { return "bidirectional_grpc_mux"; }

  void shutdownAll() override;
  std::shared_ptr<GrpcMux>
  create(std::unique_ptr<Grpc::RawAsyncClient>&& async_client,
         std::unique_ptr<Grpc::RawAsyncClient>&& async_failover_client,
         Event::Dispatcher& dispatcher, Random::RandomGenerator& random, Stats::Scope& scope,
         const envoy::config::core::v3::ApiConfigSource& ads_config,
         const LocalInfo::LocalInfo& local_info,
         std::unique_ptr<CustomConfigValidators>&& config_validators,
         BackOffStrategyPtr&& backoff_strategy, OptRef<XdsConfigTracker> xds_config_tracker,
         OptRef<XdsResourcesDelegate> xds_resources_delegate, bool use_eds_resources_cache) override;
};

} // namespace Config
} // namespace Envoy 