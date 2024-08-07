#include "envoy_quic_network_observer_registry_factory_impl.h"
#include "library/common/network/envoy_quic_network_observer_registry_factory_impl.h"

namespace Envoy {
namespace Quic {

void EnvoyQuicNetworkObserverRegistryImpl::registerObserver(
    QuicNetworkConnectivityObserver& observer) {
  quic_observers_.insert(&observer);
}

void EnvoyQuicNetworkObserverRegistryImpl::unregisterObserver(
    QuicNetworkConnectivityObserver& observer) {
  quic_observers_.erase(&observer);
}

void EnvoyQuicNetworkObserverRegistryImpl::onNetworkMadeDefault() {
  dispatcher_.post([this]() {
    for (QuicNetworkConnectivityObserver* observer : quic_observers_) {
      observer->onNetworkChanged();
    }
  });
}

EnvoyQuicNetworkObserverRegistryPtr
EnvoyQuicNetworkObserverRegistryFactoryImpl::createQuicNetworkObserverRegistry(
    Event::Dispatcher& dispatcher) {
  auto result = std::make_unique<EnvoyQuicNetworkObserverRegistryImpl>(dispatcher);
  thread_local_observer_registries_.emplace_back(*result);
  return result;
}

} // namespace Quic
} // namespace Envoy
