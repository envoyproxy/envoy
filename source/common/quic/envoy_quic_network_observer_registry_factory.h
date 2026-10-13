#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"

#include "source/common/quic/quic_network_connectivity_observer.h"

namespace Envoy {
namespace Quic {

// A registry of network connectivity observers.
class EnvoyQuicNetworkObserverRegistry {
public:
  virtual ~EnvoyQuicNetworkObserverRegistry() = default;

  void registerObserver(QuicNetworkConnectivityObserver& observer) {
    quic_observers_.insert(&observer);
  }

  void unregisterObserver(QuicNetworkConnectivityObserver& observer) {
    quic_observers_.erase(&observer);
  }

  // Get the default network handle.
  virtual NetworkHandle getDefaultNetwork() PURE;

  // Get an alternative network handle different from the given one.
  virtual NetworkHandle getAlternativeNetwork(NetworkHandle network) PURE;

protected:
  const absl::flat_hash_set<QuicNetworkConnectivityObserver*>& registeredQuicObservers() const {
    return quic_observers_;
  }

private:
  absl::flat_hash_set<QuicNetworkConnectivityObserver*> quic_observers_;
};

class EnvoyQuicNetworkObserverRegistryFactory {
public:
  virtual ~EnvoyQuicNetworkObserverRegistryFactory() = default;

  virtual std::unique_ptr<EnvoyQuicNetworkObserverRegistry>
  createQuicNetworkObserverRegistry(Event::Dispatcher& /*dispatcher*/) PURE;
};

using EnvoyQuicNetworkObserverRegistryPtr = std::unique_ptr<EnvoyQuicNetworkObserverRegistry>;
using EnvoyQuicNetworkObserverRegistryFactoryPtr =
    std::unique_ptr<EnvoyQuicNetworkObserverRegistryFactory>;

} // namespace Quic
} // namespace Envoy
