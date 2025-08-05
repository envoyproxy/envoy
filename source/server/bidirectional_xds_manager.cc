#include "source/server/bidirectional_xds_manager.h"

#include <memory>
#include <set>
#include <string>

#include "source/common/config/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Server {

BidirectionalXdsManager::BidirectionalXdsManager(Instance& server) : server_(server) {
  ENVOY_LOG(debug, "Created bidirectional xDS manager");
}

void BidirectionalXdsManager::initialize() {
  if (initialized_) {
    return;
  }

  initialized_ = true;

  if (!shouldEnable()) {
    ENVOY_LOG(debug, "Bidirectional xDS not enabled");
    return;
  }

  // Set configuration from bootstrap
  auto& bootstrap = server_.bootstrap();
  if (bootstrap.dynamic_resources().has_ads_config()) {
    config_ = Config::BidirectionalXdsConfig::fromBootstrap(
        bootstrap.dynamic_resources().ads_config());
  }

  ENVOY_LOG(info, "Initializing bidirectional xDS");

  // Get bidirectional mux from ADS
  auto bidirectional_mux = getBidirectionalAdsMux();
  if (!bidirectional_mux) {
    ENVOY_LOG(warn, "Could not get bidirectional ADS mux, reverse xDS will not be available");
    return;
  }

  // Setup resource providers based on configuration
  if (config_.provide_listener_status) {
    setupListenerStatusProvider();
    
    if (listener_status_provider_) {
      ENVOY_LOG(info, "Registered listener status provider for reverse xDS");
    }
  }

  // TODO: Add cluster health status provider when implemented
  // TODO: Add configuration snapshot provider when implemented

  // Update initial listener status
  updateListenerStatusFromManager();

  enabled_ = true;
  ENVOY_LOG(info, "Bidirectional xDS initialization complete");
}

void BidirectionalXdsManager::setupListenerStatusProvider() {
  auto provider = std::make_unique<ListenerStatusProvider>();
  listener_status_provider_ = provider.get(); // Keep weak reference
  
  // Get bidirectional mux and register the provider
  auto bidirectional_mux = getBidirectionalAdsMux();
  if (bidirectional_mux) {
    bidirectional_mux->registerClientResourceProvider(
        provider->getTypeUrl(),
        std::move(provider));
    
    ENVOY_LOG(debug, "Created and registered listener status provider");
  } else {
    ENVOY_LOG(warn, "Could not register listener status provider - no bidirectional mux");
  }
}

std::shared_ptr<Config::BidirectionalGrpcMuxImpl> BidirectionalXdsManager::getBidirectionalAdsMux() {
  // Try to get the ADS mux from xDS manager
  auto& xds_manager = server_.xdsManager();
  
  // Get the ADS mux - this returns a GrpcMuxSharedPtr which might be bidirectional
  auto ads_mux = xds_manager.adsMux();
  if (!ads_mux) {
    ENVOY_LOG(debug, "No ADS mux available");
    return nullptr;
  }

  // Try to cast to bidirectional mux
  auto bidirectional_mux = std::dynamic_pointer_cast<Config::BidirectionalGrpcMuxImpl>(ads_mux);
  if (!bidirectional_mux) {
    ENVOY_LOG(debug, "ADS mux is not bidirectional, reverse xDS not available");
    return nullptr;
  }

  ENVOY_LOG(debug, "Found bidirectional ADS mux");
  return bidirectional_mux;
}

bool BidirectionalXdsManager::shouldEnable() const {
  // Check if the runtime feature is enabled
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.enable_bidirectional_xds")) {
    return false;
  }

  // Check if ADS is configured 
  auto& bootstrap = server_.bootstrap();
  if (!bootstrap.dynamic_resources().has_ads_config()) {
    ENVOY_LOG(debug, "ADS not configured, bidirectional xDS disabled");
    return false;
  }

  // Create config from bootstrap (but don't modify member variable in const method)
  auto temp_config = Config::BidirectionalXdsConfig::fromBootstrap(
      bootstrap.dynamic_resources().ads_config());

  return temp_config.enabled;
}

void BidirectionalXdsManager::onListenerAdded(const std::string& name) {
  if (enabled_ && listener_status_provider_) {
    listener_status_provider_->onListenerAdded(name);
    ENVOY_LOG(debug, "Forwarded listener added event for: {}", name);
  }
}

void BidirectionalXdsManager::onListenerReady(const std::string& name, const std::string& bound_address) {
  if (enabled_ && listener_status_provider_) {
    listener_status_provider_->onListenerReady(name, bound_address);
    ENVOY_LOG(debug, "Forwarded listener ready event for: {} at {}", name, bound_address);
  }
}

void BidirectionalXdsManager::onListenerFailed(const std::string& name, const std::string& error) {
  if (enabled_ && listener_status_provider_) {
    listener_status_provider_->onListenerFailed(name, error);
    ENVOY_LOG(debug, "Forwarded listener failed event for: {} - {}", name, error);
  }
}

void BidirectionalXdsManager::onListenerDraining(const std::string& name) {
  if (enabled_ && listener_status_provider_) {
    listener_status_provider_->onListenerDraining(name);
    ENVOY_LOG(debug, "Forwarded listener draining event for: {}", name);
  }
}

void BidirectionalXdsManager::onListenerRemoved(const std::string& name) {
  if (enabled_ && listener_status_provider_) {
    listener_status_provider_->removeListener(name);
    ENVOY_LOG(debug, "Forwarded listener removed event for: {}", name);
  }
}

void BidirectionalXdsManager::updateListenerStatusFromManager() {
  if (!enabled_ || !listener_status_provider_) {
    return;
  }

  auto& listener_manager = server_.listenerManager();
  
  // Get all listeners in different states
  auto active_listeners = listener_manager.listeners(ListenerManager::ListenerState::ACTIVE);
  auto warming_listeners = listener_manager.listeners(ListenerManager::ListenerState::WARMING);
  auto draining_listeners = listener_manager.listeners(ListenerManager::ListenerState::DRAINING);

  // Track all current listeners
  std::set<std::string> current_listeners;

  // Process active listeners (ready)
  for (const auto& listener_ref : active_listeners) {
    auto& listener = const_cast<Network::ListenerConfig&>(listener_ref.get());
    const std::string& name = listener.name();
    current_listeners.insert(name);
    
    // For active listeners, we assume they're ready
    // In a real implementation, we'd check if the socket is actually bound
    const auto& socket_factories = listener.listenSocketFactories();
    std::string address = socket_factories.empty() ? "unknown" : 
                         socket_factories[0]->localAddress()->asString();
    onListenerReady(name, address);
  }

  // Process warming listeners (pending)
  for (const auto& listener_ref : warming_listeners) {
    const auto& listener = listener_ref.get();
    const std::string& name = listener.name();
    current_listeners.insert(name);
    
    onListenerAdded(name);
  }

  // Process draining listeners
  for (const auto& listener_ref : draining_listeners) {
    const auto& listener = listener_ref.get();
    const std::string& name = listener.name();
    current_listeners.insert(name);
    
    onListenerDraining(name);
  }

  // TODO: Remove listeners that no longer exist
  // This would require tracking the previous set of listeners
  
  ENVOY_LOG(debug, "Updated status for {} listeners", current_listeners.size());
}

} // namespace Server
} // namespace Envoy