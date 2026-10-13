#pragma once

#include "source/common/quic/envoy_quic_network_observer_registry_factory.h"
#include "source/common/quic/quic_network_connectivity_observer.h"

#include "absl/synchronization/mutex.h"

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

  quic::QuicNetworkHandle getDefaultNetwork() override {
    return network_tracker_.getDefaultNetwork();
  }

  quic::QuicNetworkHandle getAlternativeNetwork(quic::QuicNetworkHandle network) override {
    auto networks = network_tracker_.getAllConnectedNetworks();
    for (const auto& [handle, type] : networks) {
      if (handle != network) {
        return handle;
      }
    }
    return quic::kInvalidNetworkHandle;
  }

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
      NetworkConnectivityTracker& network_tracker);

  EnvoyQuicNetworkObserverRegistryPtr
  createQuicNetworkObserverRegistry(Event::Dispatcher& dispatcher) override;

  std::vector<std::reference_wrapper<EnvoyMobileQuicNetworkObserverRegistry>>
  getCreatedObserverRegistries();

private:
  absl::Mutex mutex_;
  std::vector<std::reference_wrapper<EnvoyMobileQuicNetworkObserverRegistry>>
      thread_local_observer_registries_ ABSL_GUARDED_BY(mutex_);
  NetworkConnectivityTracker& network_tracker_;
};

using EnvoyMobileQuicNetworkObserverRegistryPtr =
    std::unique_ptr<EnvoyMobileQuicNetworkObserverRegistry>;

} // namespace Quic
} // namespace Envoy
