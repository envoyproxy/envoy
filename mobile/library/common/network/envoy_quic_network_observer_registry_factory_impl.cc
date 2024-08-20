#include "envoy_quic_network_observer_registry_factory_impl.h"
#include "library/common/network/envoy_quic_network_observer_registry_factory_impl.h"

namespace Envoy {
namespace Quic {

void EnvoyMobileQuicNetworkObserverRegistry::onNetworkMadeDefault() {
  ENVOY_LOG_MISC(trace, "Default network changed.");
  dispatcher_.post([this]() {
    // Retain the existing observers in a list and iterate on the list.
    std::list<quic::QuicNetworkConnectivityObserver*> existing_observers;
    for (quic::QuicNetworkConnectivityObserver* observer : registeredQuicObservers()) {
      existing_observers.push_back(observer);
    }
    for (auto* observer : existing_observers) {
      observer->onNetworkChanged();
    }
  });
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
