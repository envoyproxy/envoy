#pragma once

#include "source/common/quic/envoy_quic_network_observer_registry_factory.h"
#include "source/common/quic/quic_network_connectivity_observer.h"

namespace Envoy {
namespace Quic {

// A mobile implementation that also handles network change events.
class EnvoyMobileQuicNetworkObserverRegistry : public EnvoyQuicNetworkObserverRegistry {
public:
  explicit EnvoyMobileQuicNetworkObserverRegistry(Event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {}

  void onNetworkMadeDefault();

private:
  Event::Dispatcher& dispatcher_;
};

class EnvoyMobileQuicNetworkObserverRegistryFactory
    : public EnvoyQuicNetworkObserverRegistryFactory {
public:
  EnvoyMobileQuicNetworkObserverRegistryFactory() { thread_local_observer_registries_.reserve(1); }

  EnvoyQuicNetworkObserverRegistryPtr
  createQuicNetworkObserverRegistry(Event::Dispatcher& dispatcher) override;

  std::vector<std::reference_wrapper<EnvoyMobileQuicNetworkObserverRegistry>>&
  getCreatedObserverRegistries() {
    return thread_local_observer_registries_;
  }

private:
  std::vector<std::reference_wrapper<EnvoyMobileQuicNetworkObserverRegistry>>
      thread_local_observer_registries_;
};

using EnvoyMobileQuicNetworkObserverRegistryPtr =
    std::unique_ptr<EnvoyMobileQuicNetworkObserverRegistry>;

} // namespace Quic
} // namespace Envoy
