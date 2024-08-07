#pragma once

#include "source/common/quic/envoy_quic_network_observer_registry_factory.h"
#include "source/common/quic/quic_network_connectivity_observer.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicNetworkObserverRegistryImpl : public EnvoyQuicNetworkObserverRegistry {
public:
  explicit EnvoyQuicNetworkObserverRegistryImpl(Event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {}

  void registerObserver(QuicNetworkConnectivityObserver& observer) override;
  void unregisterObserver(QuicNetworkConnectivityObserver& observer) override;

  void onNetworkMadeDefault();

private:
  Event::Dispatcher& dispatcher_;
  absl::flat_hash_set<QuicNetworkConnectivityObserver*> quic_observers_;
};

class EnvoyQuicNetworkObserverRegistryFactoryImpl : public EnvoyQuicNetworkObserverRegistryFactory {
public:
  EnvoyQuicNetworkObserverRegistryPtr
  createQuicNetworkObserverRegistry(Event::Dispatcher& dispatcher) override;

  std::list<std::reference_wrapper<EnvoyQuicNetworkObserverRegistryImpl>>&
  getCreatedObserverRegistries() {
    return thread_local_observer_registries_;
  }

private:
  std::list<std::reference_wrapper<EnvoyQuicNetworkObserverRegistryImpl>>
      thread_local_observer_registries_;
};

using EnvoyQuicNetworkObserverRegistryFactoryImplPtr =
    std::unique_ptr<EnvoyQuicNetworkObserverRegistryFactoryImpl>;

} // namespace Quic
} // namespace Envoy
