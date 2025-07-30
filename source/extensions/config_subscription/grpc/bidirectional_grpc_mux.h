#pragma once

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/bidirectional_grpc_stream.h"

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
 * Bidirectional GrpcMux that extends the existing ADS stream to handle
 * reverse xDS requests from management servers.
 * 
 * This allows the same ADS stream to be truly bidirectional:
 * - Client → Server: DiscoveryRequest (asking for config)
 * - Server → Client: DiscoveryResponse (config)  
 * - Server → Client: DiscoveryRequest (asking for status)
 * - Client → Server: DiscoveryResponse (status)
 */
class BidirectionalGrpcMuxImpl : public NewGrpcMuxImpl,
                                public BidirectionalGrpcStreamCallbacks<
                                    envoy::service::discovery::v3::DiscoveryRequest,
                                    envoy::service::discovery::v3::DiscoveryResponse>,
                                public Logger::Loggable<Logger::Id::config> {
public:
  BidirectionalGrpcMuxImpl(Grpc::RawAsyncClientPtr&& async_client,
                          bool skip_subsequent_node,
                          const envoy::config::core::v3::ApiConfigSource& ads_config,
                          Event::Dispatcher& dispatcher,
                          Grpc::RawAsyncClientFactory& async_client_factory,
                          Stats::Scope& scope,
                          const RateLimitSettings& rate_limit_settings,
                          const LocalInfo::LocalInfo& local_info,
                          CustomConfigValidatorsPtr&& config_validators,
                          BackOffStrategyPtr&& backoff_strategy,
                          OptRef<XdsConfigTracker> xds_config_tracker,
                          OptRef<XdsResourcesDelegate> xds_resources_delegate,
                          bool enable_type_url_downgrade_and_upgrade);

  /**
   * Register a client resource provider for reverse xDS.
   */
  void registerClientResourceProvider(const std::string& type_url,
                                     ClientResourceProviderPtr provider);

  // BidirectionalGrpcStreamCallbacks
  void onDiscoveryRequest(std::unique_ptr<envoy::service::discovery::v3::DiscoveryRequest>&& request) override;

protected:
  /**
   * Create a bidirectional gRPC stream instead of a regular one.
   */
  GrpcStreamInterfacePtr<envoy::service::discovery::v3::DiscoveryRequest,
                        envoy::service::discovery::v3::DiscoveryResponse>
  createGrpcStreamObject(Grpc::RawAsyncClientPtr&& async_client,
                        const Protobuf::MethodDescriptor& service_method,
                        Stats::Scope& scope, BackOffStrategyPtr&& backoff_strategy,
                        const RateLimitSettings& rate_limit_settings) override;

private:
  // Handle reverse xDS request and generate response
  envoy::service::discovery::v3::DiscoveryResponse
  handleReverseRequest(const envoy::service::discovery::v3::DiscoveryRequest& request);

  // Generate nonce for reverse responses
  std::string generateReverseNonce();

  ClientResourceRegistry client_resource_registry_;
  std::atomic<uint64_t> reverse_nonce_counter_{0};
  
  // Store reference to bidirectional stream for sending responses
  BidirectionalGrpcStream<envoy::service::discovery::v3::DiscoveryRequest,
                         envoy::service::discovery::v3::DiscoveryResponse>* bidirectional_stream_{nullptr};
};

/**
 * Factory for creating bidirectional GrpcMux instances.
 */
class BidirectionalGrpcMuxFactory : public MuxFactory {
public:
  std::string name() const override { return "bidirectional_grpc_mux"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string configType() override { return "bidirectional_grpc_mux"; }

  GrpcMuxSharedPtr create(Grpc::RawAsyncClientPtr&& async_client,
                         Event::Dispatcher& dispatcher,
                         Grpc::RawAsyncClientFactory& async_client_factory,
                         Stats::Scope& scope,
                         const envoy::config::core::v3::ApiConfigSource& ads_config,
                         const LocalInfo::LocalInfo& local_info,
                         CustomConfigValidatorsPtr&& config_validators,
                         BackOffStrategyPtr&& backoff_strategy,
                         OptRef<XdsConfigTracker> xds_config_tracker,
                         OptRef<XdsResourcesDelegate> xds_resources_delegate,
                         bool skip_subsequent_node) override;
};

} // namespace Config
} // namespace Envoy 