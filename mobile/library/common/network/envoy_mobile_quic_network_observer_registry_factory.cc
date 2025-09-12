#include "envoy_mobile_quic_network_observer_registry_factory.h"
#include "library/common/network/envoy_mobile_quic_network_observer_registry_factory.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Quic {

void EnvoyMobileQuicNetworkObserverRegistry::onNetworkMadeDefault(NetworkHandle network) {
  ENVOY_LOG_MISC(trace, "Default network changed.");
  ASSERT(dispatcher_.isThreadSafe());
  // Retain the existing observers in a list and iterate on the list as new
  // connections might be created and registered during iteration.
  std::vector<QuicNetworkConnectivityObserver*> existing_observers;
  existing_observers.reserve(registeredQuicObservers().size());
  for (QuicNetworkConnectivityObserver* observer : registeredQuicObservers()) {
    existing_observers.push_back(observer);
  }
  for (QuicNetworkConnectivityObserver* observer : existing_observers) {
    observer->onNetworkMadeDefault(network);
  }
}

void EnvoyMobileQuicNetworkObserverRegistry::onNetworkConnected(NetworkHandle network) {
  ENVOY_LOG_MISC(trace, "Network connected.");
  ASSERT(dispatcher_.isThreadSafe());
  // Retain the existing observers in a list and iterate on the list as new
  // connections might be created and registered during iteration.
  std::vector<QuicNetworkConnectivityObserver*> existing_observers;
  existing_observers.reserve(registeredQuicObservers().size());
  for (QuicNetworkConnectivityObserver* observer : registeredQuicObservers()) {
    existing_observers.push_back(observer);
  }
  for (QuicNetworkConnectivityObserver* observer : existing_observers) {
    observer->onNetworkConnected(network);
  }
}

EnvoyQuicNetworkObserverRegistryPtr
EnvoyMobileQuicNetworkObserverRegistryFactory::createQuicNetworkObserverRegistry(
    Event::Dispatcher& dispatcher) {
  auto result = std::make_unique<EnvoyMobileQuicNetworkObserverRegistry>(dispatcher);
  thread_local_observer_registries_.emplace_back(*result);
  return result;
}

} // namespace Quic
} // namespace Envoy
