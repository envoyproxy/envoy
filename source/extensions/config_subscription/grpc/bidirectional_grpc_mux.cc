#include "source/extensions/config_subscription/grpc/bidirectional_grpc_mux.h"

#include <memory>
#include <string>

#include "source/common/protobuf/protobuf.h"
#include "source/common/config/utility.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_context.h"
#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Config {

// ClientResourceRegistry implementation
void ClientResourceRegistry::registerProvider(const std::string& type_url,
                                             ClientResourceProviderPtr provider) {
  providers_[type_url] = std::move(provider);
}

ClientResourceProvider* ClientResourceRegistry::getProvider(const std::string& type_url) const {
  auto it = providers_.find(type_url);
  return it != providers_.end() ? it->second.get() : nullptr;
}

// BidirectionalGrpcMuxImpl implementation using composition
BidirectionalGrpcMuxImpl::BidirectionalGrpcMuxImpl(std::shared_ptr<GrpcMux> base_mux)
    : base_mux_(std::move(base_mux)) {
  // Bidirectional functionality will be added in future iterations
}

void BidirectionalGrpcMuxImpl::registerClientResourceProvider(const std::string& type_url,
                                                             ClientResourceProviderPtr provider) {
  client_resource_registry_.registerProvider(type_url, std::move(provider));
}

// Delegate all GrpcMux methods to the base implementation
void BidirectionalGrpcMuxImpl::start() {
  base_mux_->start();
}

ScopedResume BidirectionalGrpcMuxImpl::pause(const std::string& type_url) {
  return base_mux_->pause(type_url);
}

ScopedResume BidirectionalGrpcMuxImpl::pause(const std::vector<std::string> type_urls) {
  return base_mux_->pause(type_urls);
}

GrpcMuxWatchPtr BidirectionalGrpcMuxImpl::addWatch(const std::string& type_url,
                                                  const absl::flat_hash_set<std::string>& resources,
                                                  SubscriptionCallbacks& callbacks,
                                                  OpaqueResourceDecoderSharedPtr resource_decoder,
                                                  const SubscriptionOptions& options) {
  return base_mux_->addWatch(type_url, resources, callbacks, resource_decoder, options);
}

void BidirectionalGrpcMuxImpl::requestOnDemandUpdate(const std::string& type_url,
                                                    const absl::flat_hash_set<std::string>& for_update) {
  base_mux_->requestOnDemandUpdate(type_url, for_update);
}

EdsResourcesCacheOptRef BidirectionalGrpcMuxImpl::edsResourcesCache() {
  return base_mux_->edsResourcesCache();
}

absl::Status BidirectionalGrpcMuxImpl::updateMuxSource(Grpc::RawAsyncClientPtr&& primary_async_client,
                                                      Grpc::RawAsyncClientPtr&& failover_async_client, Stats::Scope& scope,
                                                      BackOffStrategyPtr&& backoff_strategy,
                                                      const envoy::config::core::v3::ApiConfigSource& ads_config_source) {
  return base_mux_->updateMuxSource(std::move(primary_async_client), std::move(failover_async_client), 
                                   scope, std::move(backoff_strategy), ads_config_source);
}

void BidirectionalGrpcMuxImpl::handleReverseRequest(const std::string& type_url,
                                                   const std::vector<std::string>& resource_names) {
  // Placeholder for future reverse xDS functionality
  // This would be called when the management server sends a request to the client
  // For now, we just log that a reverse request was received
  ENVOY_LOG_MISC(debug, "Received reverse xDS request for type: {} with {} resources", 
                type_url, resource_names.size());
}

// BidirectionalGrpcMuxFactory implementation
void BidirectionalGrpcMuxFactory::shutdownAll() {
  // Delegate to the base NewGrpcMuxImpl shutdown functionality
  NewGrpcMuxImpl::shutdownAll();
}

std::shared_ptr<GrpcMux> BidirectionalGrpcMuxFactory::create(
    std::unique_ptr<Grpc::RawAsyncClient>&& async_client,
    std::unique_ptr<Grpc::RawAsyncClient>&& async_failover_client,
    Event::Dispatcher& dispatcher, Random::RandomGenerator& /*random*/, Stats::Scope& scope,
    const envoy::config::core::v3::ApiConfigSource& /*ads_config*/,
    const LocalInfo::LocalInfo& local_info,
    std::unique_ptr<CustomConfigValidators>&& config_validators,
    BackOffStrategyPtr&& backoff_strategy, OptRef<XdsConfigTracker> xds_config_tracker,
    OptRef<XdsResourcesDelegate> xds_resources_delegate, bool /*use_eds_resources_cache*/) {
  
  // Create the service method descriptor
  const auto* method_descriptor = 
      Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources");
  
  if (!method_descriptor) {
    throw EnvoyException("Could not find ADS method descriptor");
  }
  
  // Create GrpcMuxContext for the new API
  GrpcMuxContext grpc_mux_context{
      std::move(async_client),
      std::move(async_failover_client),
      dispatcher,
      *method_descriptor,
      local_info,
      RateLimitSettings{}, // Default rate limit settings
      scope,
      std::move(config_validators),
      xds_resources_delegate,
      xds_config_tracker,
      std::move(backoff_strategy),
      "", // target_xds_authority - empty for now
      nullptr // eds_resources_cache - will be created if needed
  };
  
  // Create base NewGrpcMuxImpl first
  auto base_mux = std::make_shared<NewGrpcMuxImpl>(grpc_mux_context);
  
  // Wrap it with bidirectional functionality
  return std::make_shared<BidirectionalGrpcMuxImpl>(base_mux);
}

REGISTER_FACTORY(BidirectionalGrpcMuxFactory, MuxFactory);

} // namespace Config
} // namespace Envoy