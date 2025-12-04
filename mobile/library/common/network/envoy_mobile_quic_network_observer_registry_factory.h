#pragma once

#include "source/common/quic/envoy_quic_network_observer_registry_factory.h"
#include "source/common/quic/quic_network_connectivity_observer.h"

#include "library/common/network/network_types.h"

namespace Envoy {
namespace Quic {

// An interface to provide current network states.
class NetworkConnectivityTracker {
public:
  virtual ~NetworkConnectivityTracker() = default;

  virtual NetworkHandle getDefaultNetwork() PURE;
  virtual absl::flat_hash_map<NetworkHandle, ConnectionType> getAllConnectedNetworks() PURE;
};

// A mobile implementation that also handles network change events.
class EnvoyMobileQuicNetworkObserverRegistry : public EnvoyQuicNetworkObserverRegistry {
public:
  EnvoyMobileQuicNetworkObserverRegistry(Event::Dispatcher& dispatcher,
                                         NetworkConnectivityTracker& network_tracker)
      : dispatcher_(dispatcher), network_tracker_(network_tracker) {}

  // Called when the default network has changed to notify each registered observer asynchronously.
  void onNetworkMadeDefault(NetworkHandle network);

  // Called when a new network is connected to notify each registered observer asynchronously.
  void onNetworkConnected(NetworkHandle network);

  // Called when a new network is disconnected to notify each registered observer asynchronously.
  void onNetworkDisconnected(NetworkHandle network);

private:
  enum class NetworkChangeType {
    Connected,
    Disconnected,
    MadeDefault,
  };

  bool isNetworkChangeUpToDate(NetworkHandle network, NetworkChangeType change_type);

  void NotifyObserversOfNetworkChange(NetworkHandle network, NetworkChangeType change_type);

  Event::Dispatcher& dispatcher_;
  NetworkConnectivityTracker& network_tracker_;
};

class EnvoyMobileQuicNetworkObserverRegistryFactory
    : public EnvoyQuicNetworkObserverRegistryFactory {
public:
  explicit EnvoyMobileQuicNetworkObserverRegistryFactory(
      NetworkConnectivityTracker& network_tracker)
      : network_tracker_(network_tracker) {
    thread_local_observer_registries_.reserve(1);
  }

  EnvoyQuicNetworkObserverRegistryPtr
  createQuicNetworkObserverRegistry(Event::Dispatcher& dispatcher) override;

  std::vector<std::reference_wrapper<EnvoyMobileQuicNetworkObserverRegistry>>&
  getCreatedObserverRegistries() {
    return thread_local_observer_registries_;
  }

private:
  std::vector<std::reference_wrapper<EnvoyMobileQuicNetworkObserverRegistry>>
      thread_local_observer_registries_;
  NetworkConnectivityTracker& network_tracker_;
};

using EnvoyMobileQuicNetworkObserverRegistryPtr =
    std::unique_ptr<EnvoyMobileQuicNetworkObserverRegistry>;

} // namespace Quic
} // namespace Envoy
