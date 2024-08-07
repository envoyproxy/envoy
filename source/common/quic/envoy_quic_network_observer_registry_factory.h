#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"

#include "source/common/quic/quic_network_connectivity_observer.h"

namespace Envoy {
namespace Quic {

class EnvoyQuicNetworkObserverRegistry {
public:
  virtual ~EnvoyQuicNetworkObserverRegistry() = default;

  virtual void registerObserver(QuicNetworkConnectivityObserver& observer) PURE;
  virtual void unregisterObserver(QuicNetworkConnectivityObserver& observer) PURE;
};

using EnvoyQuicNetworkObserverRegistryPtr = std::unique_ptr<EnvoyQuicNetworkObserverRegistry>;

class EnvoyQuicNetworkObserverRegistryFactory {
public:
  virtual ~EnvoyQuicNetworkObserverRegistryFactory() = default;

  virtual EnvoyQuicNetworkObserverRegistryPtr
  createQuicNetworkObserverRegistry(Event::Dispatcher& dispatcher) PURE;
};

using EnvoyQuicNetworkObserverRegistryFactoryPtr =
    std::unique_ptr<EnvoyQuicNetworkObserverRegistryFactory>;

} // namespace Quic
} // namespace Envoy
