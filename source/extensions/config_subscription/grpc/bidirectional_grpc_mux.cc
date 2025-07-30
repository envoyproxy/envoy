#include "source/extensions/config_subscription/grpc/bidirectional_grpc_mux.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Config {

// ClientResourceRegistry implementation
void ClientResourceRegistry::registerProvider(const std::string& type_url,
                                             ClientResourceProviderPtr provider) {
  ENVOY_LOG(info, "Registering client resource provider for type: {}", type_url);
  providers_[type_url] = std::move(provider);
}

ClientResourceProvider* ClientResourceRegistry::getProvider(const std::string& type_url) const {
  auto it = providers_.find(type_url);
  return it != providers_.end() ? it->second.get() : nullptr;
}

// BidirectionalGrpcMuxImpl implementation
BidirectionalGrpcMuxImpl::BidirectionalGrpcMuxImpl(
    Grpc::RawAsyncClientPtr&& async_client,
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
    bool enable_type_url_downgrade_and_upgrade)
    : NewGrpcMuxImpl(std::move(async_client), skip_subsequent_node, ads_config, dispatcher,
                     async_client_factory, scope, rate_limit_settings, local_info,
                     std::move(config_validators), std::move(backoff_strategy),
                     xds_config_tracker, xds_resources_delegate,
                     enable_type_url_downgrade_and_upgrade) {
  ENVOY_LOG(info, "Created bidirectional gRPC mux for reverse xDS");
}

void BidirectionalGrpcMuxImpl::registerClientResourceProvider(const std::string& type_url,
                                                             ClientResourceProviderPtr provider) {
  client_resource_registry_.registerProvider(type_url, std::move(provider));
}

GrpcStreamInterfacePtr<envoy::service::discovery::v3::DiscoveryRequest,
                      envoy::service::discovery::v3::DiscoveryResponse>
BidirectionalGrpcMuxImpl::createGrpcStreamObject(
    Grpc::RawAsyncClientPtr&& async_client,
    const Protobuf::MethodDescriptor& service_method,
    Stats::Scope& scope, BackOffStrategyPtr&& backoff_strategy,
    const RateLimitSettings& rate_limit_settings) {
  
  // Create a bidirectional stream instead of a regular one
  auto bidirectional_stream = std::make_unique<BidirectionalGrpcStream<
      envoy::service::discovery::v3::DiscoveryRequest,
      envoy::service::discovery::v3::DiscoveryResponse>>(
      this, std::move(async_client), service_method, dispatcher_, scope,
      std::move(backoff_strategy), rate_limit_settings,
      typename GrpcStream<envoy::service::discovery::v3::DiscoveryRequest,
                         envoy::service::discovery::v3::DiscoveryResponse>::ConnectedStateValue::FIRST_ENTRY);
  
  // Store reference for sending reverse responses
  bidirectional_stream_ = bidirectional_stream.get();
  
  return std::move(bidirectional_stream);
}

void BidirectionalGrpcMuxImpl::onDiscoveryRequest(
    std::unique_ptr<envoy::service::discovery::v3::DiscoveryRequest>&& request) {
  
  ENVOY_LOG(debug, "Received reverse xDS request from management server for type: {}", 
            request->type_url());

  // Detect if this is a reverse xDS request by checking the type_url
  if (request->type_url().find("envoy.admin.") != std::string::npos ||
      request->type_url().find("envoy.service.status.") != std::string::npos) {
    // This is a reverse xDS request - management server asking us for client resources
    auto response = handleReverseRequest(*request);
    
    // Send the response back on the bidirectional stream
    if (bidirectional_stream_) {
      bidirectional_stream_->sendResponse(response);
      ENVOY_LOG(debug, "Sent reverse xDS response for type: {}, version: {}", 
                response.type_url(), response.version_info());
    }
  } else {
    // This shouldn't happen - configuration requests should come as responses, not requests
    ENVOY_LOG(warn, "Received unexpected DiscoveryRequest for config type: {}", request->type_url());
  }
}

envoy::service::discovery::v3::DiscoveryResponse
BidirectionalGrpcMuxImpl::handleReverseRequest(const envoy::service::discovery::v3::DiscoveryRequest& request) {
  envoy::service::discovery::v3::DiscoveryResponse response;
  
  // Set basic response fields
  response.set_type_url(request.type_url());
  response.set_nonce(generateReverseNonce());
  
  // Find the provider for this resource type
  auto* provider = client_resource_registry_.getProvider(request.type_url());
  if (!provider) {
    ENVOY_LOG(warn, "No client resource provider registered for type: {}", request.type_url());
    response.set_version_info("0");
    return response;
  }
  
  // Get requested resource names
  std::vector<std::string> requested_names(request.resource_names().begin(), 
                                          request.resource_names().end());
  
  // Get resources from the provider
  auto resources = provider->getResources(requested_names);
  response.set_version_info(provider->getVersionInfo());
  
  // Add resources to response
  for (auto& resource : resources) {
    *response.add_resources() = std::move(resource);
  }
  
  ENVOY_LOG(debug, "Generated reverse xDS response with {} resources", resources.size());
  return response;
}

std::string BidirectionalGrpcMuxImpl::generateReverseNonce() {
  return absl::StrCat("reverse_", reverse_nonce_counter_.fetch_add(1));
}

// BidirectionalGrpcMuxFactory implementation
ProtobufTypes::MessagePtr BidirectionalGrpcMuxFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::core::v3::ApiConfigSource>();
}

GrpcMuxSharedPtr BidirectionalGrpcMuxFactory::create(
    Grpc::RawAsyncClientPtr&& async_client,
    Event::Dispatcher& dispatcher,
    Grpc::RawAsyncClientFactory& async_client_factory,
    Stats::Scope& scope,
    const envoy::config::core::v3::ApiConfigSource& ads_config,
    const LocalInfo::LocalInfo& local_info,
    CustomConfigValidatorsPtr&& config_validators,
    BackOffStrategyPtr&& backoff_strategy,
    OptRef<XdsConfigTracker> xds_config_tracker,
    OptRef<XdsResourcesDelegate> xds_resources_delegate,
    bool skip_subsequent_node) {
  
  return std::make_shared<BidirectionalGrpcMuxImpl>(
      std::move(async_client), skip_subsequent_node, ads_config, dispatcher,
      async_client_factory, scope, RateLimitSettings{}, local_info,
      std::move(config_validators), std::move(backoff_strategy),
      xds_config_tracker, xds_resources_delegate, true);
}

} // namespace Config
} // namespace Envoy 